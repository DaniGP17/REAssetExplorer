using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Core.Render;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.Core;
using REAssetExplorer.RenderTest2.DX12;
using REAssetExplorer.RenderTest2.Engine;
using REAssetExplorer.RenderTest2.Interop.Structures;
using REAssetExplorer.RenderTest2.Scene;
using Vortice.Direct3D12;
using Vortice.DXGI;
using Vortice.Mathematics;
using Matrix4x4 = System.Numerics.Matrix4x4;

namespace REAssetExplorer.RenderTest2;

#region Configuration Structures

public enum ColorSpace
{
    RgbFullRangeG22NoneP709 = 0,
    RgbFullRangeG10NoneP709 = 1,
    RgbFullRangeG2084NoneP2020 = 2,
    RgbFullRangeG22NoneP2020 = 3,
    YccFullRangeG22LeftP709 = 4,
    YccStudioRangeG22LeftP709 = 5,
    RgbStudioRangeG22NoneP709 = 6,
    YccStudioRangeG22LeftP2020 = 7,
    RgbFullRangeG2084NoneP709 = 8,
}

public enum WindowMode
{
    Windowed = 0,
    Borderless = 1,
    Fullscreen = 2,
}

public class RenderConfig
{
    public int VSync { get; set; } = 0; // 0=off, 1=on, 2=adaptive
    public bool AsyncRenderingEnabled { get; set; } = false;
    public bool DisableLod { get; set; } = false;
    public float OcclusionCullingCut { get; set; } = 1.0f;
    public bool ShadowCacheEnabled { get; set; } = true;
    public int TextureLoadLevelBias { get; set; } = 0; // -2 to +2
    public bool StatisticsEnabled { get; set; } = true;
}

public class DisplayConfig
{
    public ColorSpace ColorSpace { get; set; } = ColorSpace.RgbFullRangeG22NoneP709;
    public float Gamma { get; set; } = 2.2f;
    public float WhitePaperNits { get; set; } = 80.0f;
    public float DisplayMaxNits { get; set; } = 300.0f;
    public System.Numerics.Vector2 MdrOutRangeMinXY { get; set; } = System.Numerics.Vector2.Zero;
    public System.Numerics.Vector2 MdrOutRangeMaxXY { get; set; } = System.Numerics.Vector2.One;
}

public class RenderStatistics
{
    public uint UsedVramSize32 { get; set; }
    public ulong UsedVramSize64 { get; set; }
    public double RenderTimeMs { get; set; }
    public ulong RenderFrame { get; set; }
    public uint PrimitiveCount { get; set; }
    public uint DrawCallCount { get; set; }
    
    public void Reset()
    {
        PrimitiveCount = 0;
        DrawCallCount = 0;
    }
}

#endregion

public class Renderer : IDisposable
{
    // Singleton instance
    public static Renderer? Instance { get; private set; }
    
    private static Dictionary<string, PakFile>? _pakFiles;
    private static IGameProvider? _gameProvider;
    private static ResourceManager _resourceManager = null!;
    private bool _isInitialized;
    private bool _isShutdown;
    
    // DirectX 12 components
    private DX12Device? _device;
    private DX12SwapChain? _swapChain;
    private DX12CommandQueue? _commandQueue;
    private Grid? _grid;
    private readonly List<MeshRenderer> _meshRenderers = new();
    private IntPtr _windowHandle;
    private int _width;
    private int _height;

    // Scene mesh management — render-thread owned, fed from any thread via concurrent queue
    private volatile bool _clearSceneRequested;
    private volatile int  _sceneGeneration;
    private readonly ConcurrentQueue<(RenderMesh Mesh, Matrix4x4 WorldTransform, int NodeId)> _pendingMeshes = new();
    private readonly List<(MeshRenderer Renderer, string LayerName, int NodeId, RenderMesh Mesh)> _sceneMeshRenderers = new();
    
    // G-Buffers for deferred rendering
    private ID3D12Resource?[] _gBuffers = new ID3D12Resource?[3]; // RT1, RT2, RT3 (RT0 is swap chain backbuffer)
    private ID3D12DescriptorHeap? _rtvHeap; // RTV heap for G-Buffers
    private CpuDescriptorHandle[]? _gBufferRtvHandles;

    // ── Outline (CodeXRE-style outer glow: mask → horizontal blur → composite) ──
    private const Format OutlineMaskFormat = Format.R8_UNorm;

    private ID3D12Resource?       _outlineMaskTex;          // RT/SRV — silhouette mask
    private ID3D12Resource?       _outlineBlurTex;          // RT/SRV — horizontally blurred mask
    private ResourceStates        _outlineMaskState = ResourceStates.RenderTarget;
    private ResourceStates        _outlineBlurState = ResourceStates.RenderTarget;
    private ID3D12DescriptorHeap? _outlineRtvHeap;          // 2 RTVs (mask, blur)
    private CpuDescriptorHandle   _outlineMaskRtv;
    private CpuDescriptorHandle   _outlineBlurRtv;
    private ID3D12DescriptorHeap? _outlineSrvHeap;          // shader-visible, 2 SRVs (mask=slot0, blur=slot1)

    private ID3D12RootSignature?  _outlineMaskRootSig;      // CBV b0
    private ID3D12RootSignature?  _outlineBlurRootSig;      // CBV b0 + 2 SRV tables (t0, t1)
    private ID3D12PipelineState?  _outlineMaskPso;          // mesh → mask RT
    private ID3D12PipelineState?  _outlineBlurHorzPso;      // fullscreen tri → blur RT
    private ID3D12PipelineState?  _outlineBlurCompositePso; // fullscreen tri → backbuffer (alpha blend)

    [StructLayout(LayoutKind.Sequential)]
    private struct OutlineBlurConsts
    {
        public Vector4 OutlineColour;
        public Vector4 FillColour;
        public int     StepDirectionX;
        public int     StepDirectionY;
        public int     Stage;
        public int     Width;
    }
    // Two separate CBs: passes 1 and 2 are in the same command list, so they
    // can't share a slot — the second CPU update would clobber the first
    // before the GPU reads it.
    private ConstantBuffer<OutlineBlurConsts>? _outlineBlurCbHorz;      // stage 0
    private ConstantBuffer<OutlineBlurConsts>? _outlineBlurCbComposite; // stage 1

    // Outline appearance (tweak here to change look).
    private readonly Vector4 _outlineColour = new(1.0f, 0.55f, 0.0f, 1.0f);   // orange-amber glow
    private readonly Vector4 _outlineFill   = new(1.0f, 0.55f, 0.0f, 0.0f);   // fill alpha = 0 → discard inside (ring-only)
    private const int        _outlineBlurWidth = 4;                          // glow radius (max 8 in shader)
    
    // Rendering state
    private ulong _renderFrame = 0;
    private readonly Stopwatch _frameStopwatch = new Stopwatch();
    private readonly Stopwatch _fpsStopwatch = Stopwatch.StartNew();
    private readonly Stopwatch _deltaTimeStopwatch = Stopwatch.StartNew();
    private int _frameCount = 0;
    private double _fps = 0;
    private double _frameTime = 0;
    
    // Configuration
    private readonly RenderConfig _config = new RenderConfig();
    private readonly DisplayConfig _displayConfig = new DisplayConfig();
    private readonly RenderStatistics _statistics = new RenderStatistics();
    private readonly LayerManager _layerManager = new LayerManager();
    
    // Scene
    private Camera? _camera;

    // Selected scene node (set from UI thread, read on render thread)
    private volatile int _selectedNodeId = -1;

    // Cached DXGI adapter for VRAM queries (avoids COM object allocation every frame).
    private Vortice.DXGI.IDXGIAdapter3? _cachedDxgiAdapter;

    public double Fps => _fps;
    public double FrameTime => _frameTime;
    public bool IsInitialized => _isInitialized;
    public RenderConfig Config => _config;
    public DisplayConfig DisplayConfig => _displayConfig;
    public RenderStatistics Statistics => _statistics;
    
    public static Dictionary<string, PakFile>? PakFiles => _pakFiles;
    public static IGameProvider? GameProvider => _gameProvider;

    public Renderer(Dictionary<string, PakFile> pakFiles, IGameProvider provider)
    {
        _pakFiles = pakFiles ?? throw new ArgumentNullException(nameof(pakFiles));
        _gameProvider = provider ?? throw new ArgumentNullException(nameof(provider));

        Instance = this;

        _resourceManager = new ResourceManager(pakFiles, provider);

        LoadSystemDeps();
    }

    private void LoadSystemDeps()
    {
        if (_gameProvider?.ShaderSystemDeps is not IShaderSystemDeps shaderDeps)
        {
            Console.WriteLine("No shader system dependencies provided by the game provider");
            return;
        }

        foreach (var dep in shaderDeps.GetShaderSystemDeps().ToList())
            _resourceManager.Load(dep, ResourceManager.GetResourceTypeFromExtension(dep), loadDependencies: true);
    }
    
    public bool Initialize(IntPtr windowHandle, int width, int height)
    {
        if (windowHandle == IntPtr.Zero || width <= 0 || height <= 0)
            return false;

        _windowHandle = windowHandle;
        _width = width;
        _height = height;

        try
        {
            _device = new DX12Device();
            if (!_device.Initialize())
            {
                Console.WriteLine("Failed to initialize DX12 device");
                return false;
            }

            _commandQueue = new DX12CommandQueue();
            if (!_commandQueue.Initialize(_device.Device))
            {
                Console.WriteLine("Failed to initialize command queue");
                return false;
            }

            _swapChain = new DX12SwapChain();
            if (!_swapChain.Initialize(_device.Device, _device.Factory, _commandQueue.CommandQueue, windowHandle, width, height))
            {
                Console.WriteLine("Failed to initialize swap chain");
                return false;
            }
            
            // Create G-Buffers for deferred rendering
            if (!CreateGBuffers(width, height))
            {
                Console.WriteLine("Failed to create G-Buffers");
                return false;
            }

            // Outline pipeline (mask draw + 2-pass blur)
            if (!CreateOutlineResources())
                Console.WriteLine("Outline pipeline disabled (resource creation failed)");
            else if (!CreateOutlineRenderTargets(width, height))
                Console.WriteLine("Outline render targets failed");

            _grid = new Grid();
            if (!_grid.Initialize(_device.Device, 100.0f, 100))
            {
                Console.WriteLine("Failed to initialize grid");
                return false;
            }

            var gridLayer = new SceneLayer("Grid", 100, (cmdList, w, h) =>
            {
                if (_grid != null && _camera != null)
                {
                    float aspectRatio = h > 0 ? (float)w / h : 16.0f / 9.0f;
                    var cameraData = _camera.GetCameraData(aspectRatio);
                    _grid.Render(cmdList, cameraData, w, h);
                    
                    if (_config.StatisticsEnabled)
                    {
                        _statistics.DrawCallCount++;
                    }
                }
            });
            _layerManager.AddLayer(gridLayer);

            var meshes = _resourceManager.GetAllResources<RenderMesh>();
            foreach (var mesh in meshes)
            {
                var meshRenderer = new MeshRenderer(_device.Device, mesh);
                    
                var translation = Matrix4x4.CreateTranslation(0.0f, 0.0f, 0.0f);
                var scale = Matrix4x4.CreateScale(1.0f);
                meshRenderer.Transform = translation * scale;
                    
                if (meshRenderer.Initialize())
                {
                    meshRenderer.LoadMaterialsFromMesh(_commandQueue);
                    _meshRenderers.Add(meshRenderer); // Add to list
                        
                    var meshLayer = new SceneLayer("Mesh-" + mesh.Name, 150, (cmdList, w, h) =>
                    {
                        var renderer = meshRenderer;
                        if (renderer != null && _camera != null)
                        {
                            float aspectRatio = h > 0 ? (float)w / h : 16.0f / 9.0f;
                            var cameraData = _camera.GetCameraData(aspectRatio);
                            int frameIndex = _swapChain?.CurrentBackBufferIndex ?? 0;
                            renderer.Render(cmdList, cameraData, frameIndex);
                                
                            if (_config.StatisticsEnabled)
                            {
                                _statistics.DrawCallCount++;
                            }
                        }
                    });
                    _layerManager.AddLayer(meshLayer);
                }
                else
                {
                    Console.WriteLine("Failed to initialize mesh renderer");
                    meshRenderer?.Dispose();
                }
            }

            _isInitialized = true;
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during renderer initialization: {ex.Message}");
            Console.WriteLine(ex.StackTrace);
            return false;
        }
    }

    public void SetCamera(Camera camera)
    {
        _camera = camera;
    }

    /// <summary>
    /// Creates G-Buffer render targets for deferred rendering
    /// </summary>
    private bool CreateGBuffers(int width, int height)
    {
        if (_device?.Device == null)
            return false;
            
        try
        {
            // Clean up existing G-Buffers
            CleanupGBuffers();
            
            // Create RTV descriptor heap for G-Buffers (3 descriptors)
            var heapDesc = new DescriptorHeapDescription
            {
                Type = DescriptorHeapType.RenderTargetView,
                DescriptorCount = 3,
                Flags = DescriptorHeapFlags.None
            };
            _rtvHeap = _device.Device.CreateDescriptorHeap(heapDesc);
            
            if (_rtvHeap == null)
            {
                Console.WriteLine("Failed to create RTV heap for G-Buffers");
                return false;
            }
            
            // Get descriptor size
            int rtvDescriptorSize = _device.Device.GetDescriptorHandleIncrementSize(DescriptorHeapType.RenderTargetView);
            var cpuHandle = _rtvHeap.GetCPUDescriptorHandleForHeapStart();
            
            _gBufferRtvHandles = new CpuDescriptorHandle[3];
            
            // Create 3 G-Buffers
            var gBufferFormats = new[]
            {
                Format.R8G8B8A8_UNorm,      // RT1: Albedo + Metallic
                Format.R16G16B16A16_Float,  // RT2: Normal + Roughness
                Format.R16G16B16A16_Float   // RT3: AO + UV
            };
            
            for (int i = 0; i < 3; i++)
            {
                var heapProps = new HeapProperties(HeapType.Default);
                var resourceDesc = ResourceDescription.Texture2D(
                    gBufferFormats[i],
                    (uint)width,
                    (uint)height,
                    1, 1, 1, 0,
                    ResourceFlags.AllowRenderTarget);
                
                var clearValue = new ClearValue
                {
                    Format = gBufferFormats[i],
                    Color = new Color4(0, 0, 0, 0)
                };
                
                _gBuffers[i] = _device.Device.CreateCommittedResource(
                    heapProps,
                    HeapFlags.None,
                    resourceDesc,
                    ResourceStates.RenderTarget,
                    clearValue);
                
                if (_gBuffers[i] == null)
                {
                    Console.WriteLine($"Failed to create G-Buffer {i}");
                    return false;
                }
                
                // Create RTV
                var rtvDesc = new RenderTargetViewDescription
                {
                    Format = gBufferFormats[i],
                    ViewDimension = RenderTargetViewDimension.Texture2D,
                    Texture2D = new Texture2DRenderTargetView { MipSlice = 0 }
                };
                
                _gBufferRtvHandles[i] = cpuHandle;
                _device.Device.CreateRenderTargetView(_gBuffers[i], rtvDesc, cpuHandle);
                cpuHandle.Ptr += (nuint)rtvDescriptorSize;
            }
            
            Console.WriteLine($"Created G-Buffers for deferred rendering: {width}x{height}");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating G-Buffers: {ex.Message}");
            return false;
        }
    }
    
    /// <summary>
    /// Cleans up G-Buffer resources
    /// </summary>
    private void CleanupGBuffers()
    {
        for (int i = 0; i < 3; i++)
        {
            _gBuffers[i]?.Dispose();
            _gBuffers[i] = null;
        }

        _rtvHeap?.Dispose();
        _rtvHeap = null;
        _gBufferRtvHandles = null;
    }

    // ── Outline (mask + 2-pass blur) ─────────────────────────────────────────

    /// <summary>
    /// Creates the device-resolution-independent outline resources:
    /// root signatures, PSOs, descriptor heaps, and the per-blur constant buffer.
    /// Render targets are sized in CreateOutlineRenderTargets.
    /// </summary>
    private bool CreateOutlineResources()
    {
        if (_device?.Device == null) return false;
        try
        {
            string baseDir       = AppDomain.CurrentDomain.BaseDirectory;
            string maskVsPath    = System.IO.Path.Combine(baseDir, "Shaders/OutlineMaskVS.hlsl");
            string maskPsPath    = System.IO.Path.Combine(baseDir, "Shaders/OutlineMaskPS.hlsl");
            string blurVsPath    = System.IO.Path.Combine(baseDir, "Shaders/OutlineBlurVS.hlsl");
            string blurPsPath    = System.IO.Path.Combine(baseDir, "Shaders/OutlineBlurPS.hlsl");

            if (!System.IO.File.Exists(maskVsPath) || !System.IO.File.Exists(maskPsPath) ||
                !System.IO.File.Exists(blurVsPath) || !System.IO.File.Exists(blurPsPath))
            {
                Console.WriteLine("Outline shader files missing.");
                return false;
            }

            var maskVsBlob = Vortice.D3DCompiler.Compiler.Compile(System.IO.File.ReadAllText(maskVsPath), "main", string.Empty, "vs_5_1");
            var maskPsBlob = Vortice.D3DCompiler.Compiler.Compile(System.IO.File.ReadAllText(maskPsPath), "main", string.Empty, "ps_5_1");
            var blurVsBlob = Vortice.D3DCompiler.Compiler.Compile(System.IO.File.ReadAllText(blurVsPath), "main", string.Empty, "vs_5_1");
            var blurPsBlob = Vortice.D3DCompiler.Compiler.Compile(System.IO.File.ReadAllText(blurPsPath), "main", string.Empty, "ps_5_1");

            if (maskVsBlob.Length == 0 || maskPsBlob.Length == 0 || blurVsBlob.Length == 0 || blurPsBlob.Length == 0)
            {
                Console.WriteLine("Outline shader compile failed.");
                return false;
            }

            // ── Mask root signature: just a per-object CBV at b0 (matches OutlineMaskVS) ──
            _outlineMaskRootSig = _device.Device.CreateRootSignature(
                new RootSignatureDescription1(
                    RootSignatureFlags.AllowInputAssemblerInputLayout,
                    new[]
                    {
                        new RootParameter1(
                            RootParameterType.ConstantBufferView,
                            new RootDescriptor1(0, 0),
                            ShaderVisibility.Vertex),
                    }));

            // ── Blur root signature: CBV b0 + 2 single-SRV descriptor tables (t0, t1) ──
            // Two tables let stage 0 alias t1 to the mask SRV instead of touching the in-flight blur RT.
            var t0Range = new DescriptorRange1(DescriptorRangeType.ShaderResourceView, 1, 0, 0, 0, DescriptorRangeFlags.None);
            var t1Range = new DescriptorRange1(DescriptorRangeType.ShaderResourceView, 1, 1, 0, 0, DescriptorRangeFlags.None);

            var staticSampler = new StaticSamplerDescription
            {
                Filter           = Filter.MinMagMipPoint,
                AddressU         = TextureAddressMode.Clamp,
                AddressV         = TextureAddressMode.Clamp,
                AddressW         = TextureAddressMode.Clamp,
                ShaderRegister   = 0,
                RegisterSpace    = 0,
                ShaderVisibility = ShaderVisibility.Pixel,
            };

            _outlineBlurRootSig = _device.Device.CreateRootSignature(
                new RootSignatureDescription1(
                    RootSignatureFlags.AllowInputAssemblerInputLayout,
                    new[]
                    {
                        new RootParameter1(
                            RootParameterType.ConstantBufferView,
                            new RootDescriptor1(0, 0),
                            ShaderVisibility.Pixel),
                        new RootParameter1(new RootDescriptorTable1(t0Range), ShaderVisibility.Pixel),
                        new RootParameter1(new RootDescriptorTable1(t1Range), ShaderVisibility.Pixel),
                    },
                    new[] { staticSampler }));

            if (_outlineMaskRootSig == null || _outlineBlurRootSig == null)
            {
                Console.WriteLine("Outline root signatures failed.");
                return false;
            }

            // ── Mask PSO: draws mesh geometry into R8 mask RT ──
            var meshInputLayout = new[]
            {
                new InputElementDescription("POSITION", 0, Format.R32G32B32_Float,    0,  0),
                new InputElementDescription("NORMAL",   0, Format.R32G32B32_Float,    12, 0),
                new InputElementDescription("TANGENT",  0, Format.R32G32B32A32_Float, 24, 0),
                new InputElementDescription("TEXCOORD", 0, Format.R32G32_Float,       40, 0),
                new InputElementDescription("TEXCOORD", 1, Format.R32G32_Float,       48, 0),
            };

            var maskPsoDesc = new GraphicsPipelineStateDescription
            {
                RootSignature         = _outlineMaskRootSig,
                VertexShader          = maskVsBlob,
                PixelShader           = maskPsBlob,
                InputLayout           = new Vortice.Direct3D12.InputLayoutDescription(meshInputLayout),
                PrimitiveTopologyType = PrimitiveTopologyType.Triangle,
                RasterizerState       = RasterizerDescription.CullClockwise,
                BlendState            = BlendDescription.Opaque,
                DepthStencilState     = DepthStencilDescription.None, // no DSV bound for mask pass
                SampleMask            = uint.MaxValue,
                RenderTargetFormats   = new[] { OutlineMaskFormat },
                SampleDescription     = new SampleDescription(1, 0),
            };
            _outlineMaskPso = _device.Device.CreateGraphicsPipelineState(maskPsoDesc);

            // ── Blur PSO #1 (horizontal): fullscreen tri, writes blurred mask to R8 blur RT ──
            var blurHorzPsoDesc = new GraphicsPipelineStateDescription
            {
                RootSignature         = _outlineBlurRootSig,
                VertexShader          = blurVsBlob,
                PixelShader           = blurPsBlob,
                InputLayout           = new Vortice.Direct3D12.InputLayoutDescription(Array.Empty<InputElementDescription>()),
                PrimitiveTopologyType = PrimitiveTopologyType.Triangle,
                RasterizerState       = RasterizerDescription.CullNone,
                BlendState            = BlendDescription.Opaque,
                DepthStencilState     = DepthStencilDescription.None,
                SampleMask            = uint.MaxValue,
                RenderTargetFormats   = new[] { OutlineMaskFormat },
                SampleDescription     = new SampleDescription(1, 0),
            };
            _outlineBlurHorzPso = _device.Device.CreateGraphicsPipelineState(blurHorzPsoDesc);

            // ── Blur PSO #2 (composite): fullscreen tri, alpha-blends colour into backbuffer ──
            var compositeBlend = BlendDescription.Opaque;
            compositeBlend.RenderTarget[0] = new RenderTargetBlendDescription
            {
                BlendEnable            = true,
                LogicOpEnable          = false,
                SourceBlend            = Blend.SourceAlpha,
                DestinationBlend       = Blend.InverseSourceAlpha,
                BlendOperation         = BlendOperation.Add,
                SourceBlendAlpha       = Blend.One,
                DestinationBlendAlpha  = Blend.InverseSourceAlpha,
                BlendOperationAlpha    = BlendOperation.Add,
                LogicOp                = LogicOp.Noop,
                RenderTargetWriteMask  = ColorWriteEnable.All,
            };

            var compositePsoDesc = new GraphicsPipelineStateDescription
            {
                RootSignature         = _outlineBlurRootSig,
                VertexShader          = blurVsBlob,
                PixelShader           = blurPsBlob,
                InputLayout           = new Vortice.Direct3D12.InputLayoutDescription(Array.Empty<InputElementDescription>()),
                PrimitiveTopologyType = PrimitiveTopologyType.Triangle,
                RasterizerState       = RasterizerDescription.CullNone,
                BlendState            = compositeBlend,
                DepthStencilState     = DepthStencilDescription.None,
                SampleMask            = uint.MaxValue,
                // Backbuffer is sampled through an sRGB RTV (see DX12SwapChain).
                RenderTargetFormats   = new[] { Format.R8G8B8A8_UNorm_SRgb },
                SampleDescription     = new SampleDescription(1, 0),
            };
            _outlineBlurCompositePso = _device.Device.CreateGraphicsPipelineState(compositePsoDesc);

            if (_outlineMaskPso == null || _outlineBlurHorzPso == null || _outlineBlurCompositePso == null)
            {
                Console.WriteLine("Outline PSOs failed.");
                return false;
            }

            // RTV heap for mask + blur (2 slots).
            _outlineRtvHeap = _device.Device.CreateDescriptorHeap(new DescriptorHeapDescription
            {
                Type            = DescriptorHeapType.RenderTargetView,
                DescriptorCount = 2,
                Flags           = DescriptorHeapFlags.None,
            });

            // SRV heap (shader-visible) for the blur passes: slot 0 = mask, slot 1 = blur.
            _outlineSrvHeap = _device.Device.CreateDescriptorHeap(new DescriptorHeapDescription
            {
                Type            = DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView,
                DescriptorCount = 2,
                Flags           = DescriptorHeapFlags.ShaderVisible,
            });

            _outlineBlurCbHorz      = new ConstantBuffer<OutlineBlurConsts>(_device.Device, 3);
            _outlineBlurCbComposite = new ConstantBuffer<OutlineBlurConsts>(_device.Device, 3);
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating outline resources: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Creates (or recreates) the screen-sized mask and blur render targets,
    /// and writes RTV/SRV descriptors into the outline descriptor heaps.
    /// </summary>
    private bool CreateOutlineRenderTargets(int width, int height)
    {
        if (_device?.Device == null || _outlineRtvHeap == null || _outlineSrvHeap == null)
            return false;

        try
        {
            // Dispose previous targets (Resize path).
            _outlineMaskTex?.Dispose(); _outlineMaskTex = null;
            _outlineBlurTex?.Dispose(); _outlineBlurTex = null;

            var heapProps = new HeapProperties(HeapType.Default);
            var desc = ResourceDescription.Texture2D(
                OutlineMaskFormat,
                (uint)width, (uint)height,
                1, 1, 1, 0,
                ResourceFlags.AllowRenderTarget);

            var clearValue = new ClearValue
            {
                Format = OutlineMaskFormat,
                Color  = new Vortice.Mathematics.Color4(0, 0, 0, 0),
            };

            _outlineMaskTex = _device.Device.CreateCommittedResource(
                heapProps, HeapFlags.None, desc, ResourceStates.RenderTarget, clearValue);
            _outlineBlurTex = _device.Device.CreateCommittedResource(
                heapProps, HeapFlags.None, desc, ResourceStates.RenderTarget, clearValue);

            _outlineMaskState = ResourceStates.RenderTarget;
            _outlineBlurState = ResourceStates.RenderTarget;

            int rtvStride = _device.Device.GetDescriptorHandleIncrementSize(DescriptorHeapType.RenderTargetView);
            var rtvBase = _outlineRtvHeap.GetCPUDescriptorHandleForHeapStart();
            _outlineMaskRtv = rtvBase;
            _outlineBlurRtv = new CpuDescriptorHandle { Ptr = rtvBase.Ptr + (nuint)rtvStride };

            var rtvDesc = new RenderTargetViewDescription
            {
                Format        = OutlineMaskFormat,
                ViewDimension = RenderTargetViewDimension.Texture2D,
                Texture2D     = new Texture2DRenderTargetView { MipSlice = 0 },
            };
            _device.Device.CreateRenderTargetView(_outlineMaskTex, rtvDesc, _outlineMaskRtv);
            _device.Device.CreateRenderTargetView(_outlineBlurTex, rtvDesc, _outlineBlurRtv);

            int srvStride = _device.Device.GetDescriptorHandleIncrementSize(
                DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView);
            var srvBase = _outlineSrvHeap.GetCPUDescriptorHandleForHeapStart();
            var maskSrvCpu = srvBase;
            var blurSrvCpu = new CpuDescriptorHandle { Ptr = srvBase.Ptr + (nuint)srvStride };

            var srvDesc = new ShaderResourceViewDescription
            {
                Format                  = OutlineMaskFormat,
                ViewDimension           = Vortice.Direct3D12.ShaderResourceViewDimension.Texture2D,
                Shader4ComponentMapping = ShaderComponentMapping.Default,
                Texture2D               = new Texture2DShaderResourceView { MipLevels = 1, MostDetailedMip = 0 },
            };
            _device.Device.CreateShaderResourceView(_outlineMaskTex, srvDesc, maskSrvCpu);
            _device.Device.CreateShaderResourceView(_outlineBlurTex, srvDesc, blurSrvCpu);

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating outline render targets: {ex.Message}");
            return false;
        }
    }

    private void CleanupOutlineResources()
    {
        _outlineMaskTex?.Dispose();           _outlineMaskTex = null;
        _outlineBlurTex?.Dispose();           _outlineBlurTex = null;
        _outlineRtvHeap?.Dispose();           _outlineRtvHeap = null;
        _outlineSrvHeap?.Dispose();           _outlineSrvHeap = null;
        _outlineMaskRootSig?.Dispose();       _outlineMaskRootSig = null;
        _outlineBlurRootSig?.Dispose();       _outlineBlurRootSig = null;
        _outlineMaskPso?.Dispose();           _outlineMaskPso = null;
        _outlineBlurHorzPso?.Dispose();       _outlineBlurHorzPso = null;
        _outlineBlurCompositePso?.Dispose();  _outlineBlurCompositePso = null;
        _outlineBlurCbHorz?.Dispose();        _outlineBlurCbHorz = null;
        _outlineBlurCbComposite?.Dispose();   _outlineBlurCbComposite = null;
    }

    public void Resize(int width, int height)
    {
        if (!_isInitialized || _swapChain == null || _commandQueue == null || _device == null)
            return;

        if (width <= 0 || height <= 0)
            return;

        try
        {
            _width = width;
            _height = height;

            _commandQueue.WaitForGPU();
            _swapChain.Resize(_device.Device, width, height);
            
            // Recreate G-Buffers with new size
            if (!CreateGBuffers(width, height))
            {
                Console.WriteLine("Failed to recreate G-Buffers after resize");
            }

            if (!CreateOutlineRenderTargets(width, height))
                Console.WriteLine("Failed to recreate outline render targets after resize");

            Console.WriteLine($"Renderer resized to {width}x{height}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during resize: {ex.Message}");
        }
    }

    public void RenderFrame(System.Drawing.Size clientSize)
    {
        if (!_isInitialized || _isShutdown || _device == null || _swapChain == null || _commandQueue == null)
            return;

        _resourceManager.ResourceReleaseLastExecute();
        ProcessPendingMeshes();

        try
        {
            // Start frame timing
            _frameStopwatch.Restart();
            
            // Update camera with actual per-frame delta time
            if (_camera != null)
            {
                float deltaTime = (float)_deltaTimeStopwatch.Elapsed.TotalSeconds;
                _deltaTimeStopwatch.Restart();
                _camera.Update(deltaTime);
            }

            // Begin rendering
            Begin();

            // Render content
            RenderContent();

            // Outline pass (drawn over the geometry, only RT0 bound)
            RenderOutlinePass();

            // End rendering
            End();

            // Present
            Present();

            // Update statistics
            _frameStopwatch.Stop();
            _statistics.RenderTimeMs = _frameStopwatch.Elapsed.TotalMilliseconds;
            _statistics.RenderFrame = _renderFrame++;
            
            UpdateFpsStats();
            UpdateVramStats();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during render: {ex.Message}");
        }
    }
    
    private void Begin()
    {
        if (_commandQueue == null || _swapChain == null)
            return;

        int frameIndex = _swapChain.CurrentBackBufferIndex;

        // Waits (if needed) for the previous submission that used this frame slot,
        // then resets that slot's allocator and the shared command list.
        _commandQueue.BeginFrame(frameIndex);

        var renderTarget = _swapChain.GetRenderTarget(frameIndex);
        if (renderTarget == null)
            return;

        var commandList = _commandQueue.CommandList;
        var barrier = new ResourceBarrier(
            new ResourceTransitionBarrier(renderTarget, ResourceStates.Present, ResourceStates.RenderTarget));
        commandList.ResourceBarrier(barrier);

        if (_config.StatisticsEnabled)
            _statistics.Reset();
    }
    
    private void RenderContent()
    {
        if (_commandQueue == null || _swapChain == null)
            return;
            
        var commandList = _commandQueue.CommandList;
        int frameIndex = _swapChain.CurrentBackBufferIndex;
        
        // Clear all render targets
        var rtvHandle = _swapChain.GetRTVHandle(frameIndex);
        var clearColor = new Color4(0.0f, 0.0f, 0.0f, 1.0f);
        commandList.ClearRenderTargetView(rtvHandle, clearColor);
        
        // Clear G-Buffers
        if (_gBufferRtvHandles != null)
        {
            var clearBlack = new Color4(0, 0, 0, 0);
            for (int i = 0; i < 3; i++)
            {
                commandList.ClearRenderTargetView(_gBufferRtvHandles[i], clearBlack);
            }
        }

        // Clear depth and stencil (stencil cleared to 0 each frame so the outline pass starts clean)
        var dsvHandle = _swapChain.GetDSVHandle();
        commandList.ClearDepthStencilView(dsvHandle, ClearFlags.Depth | ClearFlags.Stencil, 1.0f, 0);

        // Set all 4 render targets: RT0 (backbuffer) + RT1-RT3 (G-Buffers)
        if (_gBufferRtvHandles != null)
        {
            var rtvHandles = new CpuDescriptorHandle[4];
            rtvHandles[0] = rtvHandle;  // Backbuffer
            rtvHandles[1] = _gBufferRtvHandles[0];  // Albedo + Metallic
            rtvHandles[2] = _gBufferRtvHandles[1];  // Normal + Roughness
            rtvHandles[3] = _gBufferRtvHandles[2];  // AO + UV
            commandList.OMSetRenderTargets(rtvHandles, dsvHandle);
        }
        else
        {
            commandList.OMSetRenderTargets(rtvHandle, dsvHandle);
        }

        // Set viewport and scissor
        var viewport = new Vortice.Mathematics.Viewport(0, 0, _width, _height);
        commandList.RSSetViewport(viewport);

        var scissorRect = new Vortice.RawRect(0, 0, _width, _height);
        commandList.RSSetScissorRect(scissorRect);

        // Execute all render layers (geometry pass – writes to backbuffer + G-Buffers)
        _layerManager.ExecuteAll(commandList, _width, _height);
    }
    
    private void End()
    {
        if (_commandQueue == null || _swapChain == null)
            return;

        int frameIndex = _swapChain.CurrentBackBufferIndex;
        var renderTarget = _swapChain.GetRenderTarget(frameIndex);
        if (renderTarget == null)
            return;

        var commandList = _commandQueue.CommandList;
        var barrier = new ResourceBarrier(
            new ResourceTransitionBarrier(renderTarget, ResourceStates.RenderTarget, ResourceStates.Present));
        commandList.ResourceBarrier(barrier);

        commandList.Close();
        _commandQueue.CommandQueue.ExecuteCommandList(commandList);

        // Signal fence for this frame slot so BeginFrame can wait on the next cycle.
        _commandQueue.EndFrame(frameIndex);
    }
    
    private void Present()
    {
        if (_swapChain == null)
            return;
            
        _swapChain.Present(_config.VSync);
    }
    
    private void Wait()
    {
        _commandQueue?.WaitForGPU();
    }

    public void Shutdown()
    {
        if (_isShutdown)
            return;

        Dispose();
    }
    
    #region Statistics & Diagnostics
    
    /// <summary>Gets used VRAM in 32-bit (MB)</summary>
    public uint GetUsedVramSize()
    {
        return _statistics.UsedVramSize32;
    }
    
    /// <summary>Gets used VRAM in 64-bit (bytes)</summary>
    public ulong GetUsedVramSize64()
    {
        return _statistics.UsedVramSize64;
    }
    
    /// <summary>Gets last frame render time in milliseconds</summary>
    public double GetRenderTime()
    {
        return _statistics.RenderTimeMs;
    }
    
    /// <summary>Gets current render frame number</summary>
    public ulong GetRenderFrame()
    {
        return _statistics.RenderFrame;
    }
    
    /// <summary>Gets safe render frame (current - 2 for GPU lag)</summary>
    public ulong GetSafeRenderFrame()
    {
        return _statistics.RenderFrame >= 2 ? _statistics.RenderFrame - 2 : 0;
    }
    
    /// <summary>Gets primitive count from last frame</summary>
    public uint GetPrimNumber()
    {
        return _statistics.PrimitiveCount;
    }
    
    /// <summary>Gets complete statistics snapshot</summary>
    public RenderStatistics GetStatistics()
    {
        return _statistics;
    }
    
    /// <summary>Check if statistics collection is enabled</summary>
    public bool IsStatisticsEnable()
    {
        return _config.StatisticsEnabled;
    }
    
    /// <summary>Enable or disable statistics collection</summary>
    public void SetStatisticsEnable(bool enable)
    {
        _config.StatisticsEnabled = enable;
    }
    
    private void UpdateVramStats()
    {
        if (!_config.StatisticsEnabled || _device?.Device == null)
            return;

        // Query at most once per second regardless of frame rate.
        if (_renderFrame % 60 != 0)
            return;

        try
        {
            if (_cachedDxgiAdapter == null && _device.Factory != null)
            {
                _device.Factory.EnumAdapters1(0, out var adapter);
                _cachedDxgiAdapter = adapter as Vortice.DXGI.IDXGIAdapter3;
            }

            if (_cachedDxgiAdapter != null)
            {
                var memoryInfo = _cachedDxgiAdapter.QueryVideoMemoryInfo(0, Vortice.DXGI.MemorySegmentGroup.Local);
                _statistics.UsedVramSize64 = memoryInfo.CurrentUsage;
                _statistics.UsedVramSize32 = (uint)(memoryInfo.CurrentUsage / (1024 * 1024));
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to query VRAM: {ex.Message}");
            _cachedDxgiAdapter = null;
        }
    }
    
    #endregion
    
    #region Display Configuration
    
    /// <summary>Gets current color space</summary>
    public ColorSpace GetColorSpace()
    {
        return _displayConfig.ColorSpace;
    }
    
    /// <summary>Sets color space</summary>
    public void SetColorSpace(ColorSpace colorSpace)
    {
        _displayConfig.ColorSpace = colorSpace;
        
        // Apply to swap chain if available
        if (_swapChain?.SwapChain is IDXGISwapChain3 swapChain3)
        {
            try
            {
                var dxgiColorSpace = colorSpace switch
                {
                    ColorSpace.RgbFullRangeG22NoneP709 => Vortice.DXGI.ColorSpaceType.RgbFullG22NoneP709,
                    ColorSpace.RgbFullRangeG10NoneP709 => Vortice.DXGI.ColorSpaceType.RgbFullG10NoneP709,
                    ColorSpace.RgbFullRangeG2084NoneP2020 => Vortice.DXGI.ColorSpaceType.RgbFullG2084NoneP2020,
                    _ => Vortice.DXGI.ColorSpaceType.RgbFullG22NoneP709
                };
                
                swapChain3.SetColorSpace1(dxgiColorSpace);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to set color space: {ex.Message}");
            }
        }
    }
    
    /// <summary>Gets gamma value</summary>
    public float GetGamma()
    {
        return _displayConfig.Gamma;
    }
    
    /// <summary>Sets gamma value</summary>
    public void SetGamma(float gamma)
    {
        _displayConfig.Gamma = Math.Max(1.0f, Math.Min(3.0f, gamma));
    }
    
    /// <summary>Gets white paper nits (HDR reference white)</summary>
    public float GetWhitePaperNits()
    {
        return _displayConfig.WhitePaperNits;
    }
    
    /// <summary>Sets white paper nits</summary>
    public void SetWhitePaperNits(float nits)
    {
        _displayConfig.WhitePaperNits = Math.Max(80.0f, Math.Min(500.0f, nits));
    }
    
    /// <summary>Gets display maximum nits</summary>
    public float GetDisplayMaxNits()
    {
        return _displayConfig.DisplayMaxNits;
    }
    
    /// <summary>Sets MDR output range minimum XY</summary>
    public void SetDrawMDROutRangeMinXY(System.Numerics.Vector2 min)
    {
        _displayConfig.MdrOutRangeMinXY = min;
    }
    
    /// <summary>Sets MDR output range maximum XY</summary>
    public void SetDrawMDROutRangeMaxXY(System.Numerics.Vector2 max)
    {
        _displayConfig.MdrOutRangeMaxXY = max;
    }
    
    #endregion
    
    #region Performance & Quality Settings
    
    /// <summary>Gets VSync mode (0=off, 1=on, 2=adaptive)</summary>
    public int GetVSync()
    {
        return _config.VSync;
    }
    
    /// <summary>Sets VSync mode</summary>
    public void SetVSync(int vsync)
    {
        _config.VSync = Math.Max(0, Math.Min(2, vsync));
    }
    
    /// <summary>Check if async rendering is enabled</summary>
    public bool IsAsyncEnable()
    {
        return _config.AsyncRenderingEnabled;
    }
    
    /// <summary>Enable or disable async rendering</summary>
    public void SetAsyncEnable(bool enable)
    {
        _config.AsyncRenderingEnabled = enable;
    }
    
    /// <summary>Check if LOD is disabled (always use highest quality)</summary>
    public bool GetDisableLod()
    {
        return _config.DisableLod;
    }
    
    /// <summary>Set whether to disable LOD</summary>
    public void SetDisableLod(bool disable)
    {
        _config.DisableLod = disable;
    }
    
    /// <summary>Gets occlusion culling distance cut</summary>
    public float GetOcclusionCullingCut()
    {
        return _config.OcclusionCullingCut;
    }
    
    /// <summary>Sets occlusion culling distance cut</summary>
    public void SetOcclusionCullingCut(float cut)
    {
        _config.OcclusionCullingCut = Math.Max(0.0f, cut);
    }
    
    /// <summary>Check if shadow caching is enabled</summary>
    public bool GetShadowCacheEnable()
    {
        return _config.ShadowCacheEnabled;
    }
    
    /// <summary>Enable or disable shadow caching</summary>
    public void SetShadowCacheEnable(bool enable)
    {
        _config.ShadowCacheEnabled = enable;
    }
    
    /// <summary>Gets texture load level bias (-2 to +2)</summary>
    public int GetTextureLoadLevelBias()
    {
        return _config.TextureLoadLevelBias;
    }
    
    /// <summary>Sets texture load level bias</summary>
    public void SetTextureLoadLevelBias(int bias)
    {
        _config.TextureLoadLevelBias = Math.Max(-2, Math.Min(2, bias));
    }
    
    #endregion
    
    #region Rendering Lifecycle (Public API)
    
    /// <summary>Gets the current DX12 command list context</summary>
    public ID3D12GraphicsCommandList? GetContext()
    {
        return _commandQueue?.CommandList;
    }
    
    #endregion
    
    #region Layer Management
    
    /// <summary>Adds a scene view/layer to the renderer</summary>
    public void AddSceneView(string name, RenderLayer layer)
    {
        _layerManager.AddLayer(layer);
    }
    
    /// <summary>Removes a scene view/layer by name</summary>
    public bool RemoveSceneView(string name)
    {
        return _layerManager.RemoveLayer(name);
    }
    
    /// <summary>Gets the current output layer (first enabled layer)</summary>
    public RenderLayer? GetCurrentOutputLayer()
    {
        // Return the first enabled layer or null
        for (int i = 0; i < _layerManager.Count; i++)
        {
            // Simple implementation - could be improved with iteration support
        }
        return null;
    }
    
    /// <summary>Gets a layer from output layer by name and type</summary>
    public T? GetLayerFromOutputLayer<T>(string name) where T : RenderLayer
    {
        return _layerManager.GetLayer<T>(name);
    }
    
    /// <summary>Gets a layer by name</summary>
    public RenderLayer? GetLayer(string name)
    {
        return _layerManager.GetLayer(name);
    }
    
    #endregion
    
    #region Camera & FOV Management
    
    /// <summary>Gets current FOV in radians</summary>
    public float GetFov()
    {
        return _camera?.GetFov() ?? (MathF.PI / 4.0f);
    }
    
    /// <summary>Sets FOV in radians</summary>
    public void SetFov(float fov)
    {
        _camera?.SetFov(fov);
    }
    
    /// <summary>Gets current FOV in degrees</summary>
    public float GetFovDegrees()
    {
        return _camera?.GetFovDegrees() ?? 45.0f;
    }
    
    /// <summary>Sets FOV in degrees</summary>
    public void SetFovDegrees(float degrees)
    {
        _camera?.SetFovDegrees(degrees);
    }
    
    #endregion

    #region Scene Loading

    /// <summary>
    /// Loads a scene, replacing any currently loaded scene geometry.
    /// Thread-safe: can be called from any thread. Resources are enqueued
    /// and processed on the render thread at the start of the next frame.
    /// </summary>
    public void LoadScene(SceneData scene)
    {
        if (!_isInitialized) return;

        // Clear pending queue and signal the render thread to tear down the current scene.
        while (_pendingMeshes.TryDequeue(out _)) { }
        _clearSceneRequested = true;
        int myGeneration = ++_sceneGeneration;

        // Load mesh+material pairs on a background thread so we don't block the caller.
        Task.Run(() =>
        {
            foreach (var (meshPath, mdfPath, worldTransform, nodeId) in ExtractMeshPaths(scene))
            {
                if (_sceneGeneration != myGeneration) break; // superseded by a newer LoadScene call

                var renderMesh = _resourceManager.LoadMeshWithMaterial(meshPath, mdfPath);
                if (renderMesh != null && _sceneGeneration == myGeneration)
                    _pendingMeshes.Enqueue((renderMesh, worldTransform, nodeId));
            }
        });
    }

    /// <summary>
    /// Loads a single mesh, replacing any currently loaded scene geometry.
    /// <paramref name="meshPath"/> is relative to the game's FilesPath (e.g. "character/em3200/em3200.mesh.220128762").
    /// <paramref name="mdfPath"/> may be empty if no material file is available — the mesh will render with no textures.
    /// Thread-safe: enqueued and processed on the render thread.
    /// </summary>
    /// <returns>An opaque node id assigned to the loaded mesh (always 0 for single-mesh view).</returns>
    public int LoadMesh(string meshPath, string mdfPath)
    {
        if (!_isInitialized) return -1;

        while (_pendingMeshes.TryDequeue(out _)) { }
        _clearSceneRequested = true;
        int myGeneration = ++_sceneGeneration;
        const int singleMeshNodeId = 0;

        Task.Run(() =>
        {
            if (_sceneGeneration != myGeneration) return;

            var renderMesh = _resourceManager.LoadMeshWithMaterial(meshPath, mdfPath ?? string.Empty);
            if (renderMesh != null && _sceneGeneration == myGeneration)
                _pendingMeshes.Enqueue((renderMesh, Matrix4x4.Identity, singleMeshNodeId));
        });

        return singleMeshNodeId;
    }

    /// <summary>
    /// Removes all scene-loaded mesh renderers and their layers.
    /// Must only be called from the render thread (called by ProcessPendingMeshes).
    /// </summary>
    private void ClearSceneOnRenderThread()
    {
        foreach (var (mr, layerName, _, _) in _sceneMeshRenderers)
        {
            _layerManager.RemoveLayer(layerName);
            mr.Dispose();
        }
        _sceneMeshRenderers.Clear();
    }

    /// <summary>
    /// Drains _pendingMeshes and promotes them to live scene layers.
    /// Called once per frame on the render thread before Begin().
    /// </summary>
    private void ProcessPendingMeshes()
    {
        if (_device == null || _commandQueue == null) return;

        if (_clearSceneRequested)
        {
            _clearSceneRequested = false;
            ClearSceneOnRenderThread();
        }

        while (_pendingMeshes.TryDequeue(out var pending))
        {
            var (renderMesh, worldTransform, nodeId) = pending;
            var meshRenderer = new MeshRenderer(_device.Device, renderMesh);
            meshRenderer.Transform = worldTransform;

            if (!meshRenderer.Initialize())
            {
                meshRenderer.Dispose();
                Console.WriteLine($"Failed to initialize mesh renderer for {renderMesh.FilePath}");
                continue;
            }

            meshRenderer.LoadMaterialsFromMesh(_commandQueue);

            var layerName = "SceneMesh-" + renderMesh.FilePath;
            _sceneMeshRenderers.Add((meshRenderer, layerName, nodeId, renderMesh));

            var capturedRenderer = meshRenderer;
            var meshLayer = new SceneLayer(layerName, 150, (cmdList, w, h) =>
            {
                if (_camera == null) return;
                float ar = h > 0 ? (float)w / h : 16f / 9f;
                int fi = _swapChain?.CurrentBackBufferIndex ?? 0;
                capturedRenderer.Render(cmdList, _camera.GetCameraData(ar), fi);
                if (_config.StatisticsEnabled) _statistics.DrawCallCount++;
            });
            _layerManager.AddLayer(meshLayer);
        }
    }

    private IEnumerable<(string meshPath, string mdfPath, Matrix4x4 worldTransform, int nodeId)> ExtractMeshPaths(SceneData scene)
    {
        if (scene.Root == null) yield break;
        var filesPath = _gameProvider?.FilesPath?.Replace('\\', '/').ToLowerInvariant() ?? "";
        foreach (var item in WalkNodesWithTransform(scene.Root, Matrix4x4.Identity, filesPath))
            yield return item;
    }

    private static IEnumerable<(string meshPath, string mdfPath, Matrix4x4 worldTransform, int nodeId)>
        WalkNodesWithTransform(SceneNode node, Matrix4x4 parentWorld, string filesPath)
    {
        var nodeWorld = parentWorld;
        if (node is SceneGameObjectNode goNode)
        {
            if (goNode.Transform != null)
            {
                var t = goNode.Transform;
                var local = Matrix4x4.CreateScale(t.Scale)
                          * Matrix4x4.CreateFromQuaternion(t.Rotation)
                          * Matrix4x4.CreateTranslation(t.Position);
                nodeWorld = local * parentWorld;
            }

            foreach (var component in goNode.Components)
            {
                if (component.Name != "via.render.Mesh") continue;

                var meshPath = component.Get<string>("v2") ?? "";
                var mdfPath  = component.Get<string>("v3") ?? "";
                if (string.IsNullOrEmpty(meshPath)) continue;

                meshPath = StripFilesPathPrefix(meshPath, filesPath);
                mdfPath  = StripFilesPathPrefix(mdfPath,  filesPath);

                yield return (meshPath, mdfPath, nodeWorld, goNode.Id);
            }
        }

        foreach (var child in node.Children)
            foreach (var item in WalkNodesWithTransform(child, nodeWorld, filesPath))
                yield return item;
    }

    /// <summary>
    /// Picks the scene node closest to the camera along the ray through the given screen pixel.
    /// Returns the node Id, or -1 if nothing was hit.
    /// Thread-safe (reads _sceneMeshRenderers and _camera without mutation).
    /// </summary>
    public int PickObject(float pixelX, float pixelY, int viewWidth, int viewHeight)
    {
        if (_camera == null || viewWidth <= 0 || viewHeight <= 0) return -1;

        float ar  = (float)viewWidth / viewHeight;
        var   cam = _camera.GetCameraData(ar);

        var view = Matrix4x4.CreateLookAt(cam.Position, cam.Target, cam.Up);
        var proj = Matrix4x4.CreatePerspectiveFieldOfView(cam.Fov, ar, cam.NearPlane, cam.FarPlane);

        float ndcX = (pixelX / viewWidth)  * 2.0f - 1.0f;
        float ndcY = 1.0f - (pixelY / viewHeight) * 2.0f;

        Matrix4x4.Invert(view * proj, out var invVP);
        var near4 = Vector4.Transform(new Vector4(ndcX, ndcY, 0.0f, 1.0f), invVP);
        near4 /= near4.W;
        var far4  = Vector4.Transform(new Vector4(ndcX, ndcY, 1.0f, 1.0f), invVP);
        far4  /= far4.W;

        var rayOrigin = new Vector3(near4.X, near4.Y, near4.Z);
        var rayDir    = Vector3.Normalize(new Vector3(far4.X - near4.X, far4.Y - near4.Y, far4.Z - near4.Z));

        // Two-pass pick: prefer objects where the camera is OUTSIDE their AABB (tEntry > 0).
        // Fall back to "camera inside" objects (large enclosures like room meshes) only when
        // nothing smaller was hit.
        int   bestNodeId   = -1;
        float bestDist     = float.MaxValue;
        int   fallbackId   = -1;
        float fallbackDist = float.MaxValue;

        foreach (var (renderer, _, nodeId, mesh) in _sceneMeshRenderers)
        {
            if (mesh.Bounds == null) continue;
            if (!Matrix4x4.Invert(renderer.Transform, out var invWorld)) continue;

            var localOrigin = Vector3.Transform(rayOrigin, invWorld);
            var localDir    = Vector3.TransformNormal(rayDir, invWorld);

            if (!IntersectRayAABB(localOrigin, localDir, mesh.Bounds.Min, mesh.Bounds.Max,
                                  out float tEntry, out float tExit))
                continue;

            if (tEntry > 1e-4f)
            {
                // Camera is outside this AABB — preferred candidate.
                var worldEntry = Vector3.Transform(localOrigin + localDir * tEntry, renderer.Transform);
                float dist = (worldEntry - rayOrigin).LengthSquared();
                if (dist < bestDist) { bestDist = dist; bestNodeId = nodeId; }
            }
            else if (tExit > 0)
            {
                // Camera is inside this AABB (room/enclosure) — fallback only.
                var worldExit = Vector3.Transform(localOrigin + localDir * tExit, renderer.Transform);
                float dist = (worldExit - rayOrigin).LengthSquared();
                if (dist < fallbackDist) { fallbackDist = dist; fallbackId = nodeId; }
            }
        }

        return bestNodeId >= 0 ? bestNodeId : fallbackId;
    }

    // tEntry < 0 means the ray origin is inside the AABB.
    // Returns false if the AABB is entirely behind the ray (tExit < 0).
    private static bool IntersectRayAABB(Vector3 origin, Vector3 dir, Vector3 min, Vector3 max,
                                         out float tEntry, out float tExit)
    {
        float tMin = float.MinValue;
        float tMax = float.MaxValue;

        for (int i = 0; i < 3; i++)
        {
            float o  = i == 0 ? origin.X : i == 1 ? origin.Y : origin.Z;
            float d  = i == 0 ? dir.X    : i == 1 ? dir.Y    : dir.Z;
            float mn = i == 0 ? min.X    : i == 1 ? min.Y    : min.Z;
            float mx = i == 0 ? max.X    : i == 1 ? max.Y    : max.Z;

            if (MathF.Abs(d) < 1e-8f)
            {
                if (o < mn || o > mx) { tEntry = tExit = 0; return false; }
            }
            else
            {
                float t1 = (mn - o) / d;
                float t2 = (mx - o) / d;
                if (t1 > t2) (t1, t2) = (t2, t1);
                tMin = MathF.Max(tMin, t1);
                tMax = MathF.Min(tMax, t2);
                if (tMin > tMax) { tEntry = tExit = 0; return false; }
            }
        }

        tEntry = tMin;
        tExit  = tMax;
        return tMax > 0; // AABB must be at least partly in front of the ray
    }

    /// <summary>
    /// Sets the currently selected scene node. Pass -1 to clear the selection.
    /// Thread-safe: can be called from any thread.
    /// </summary>
    public void SetSelectedNode(int nodeId)
    {
        _selectedNodeId = nodeId;
    }

    /// <summary>
    /// CodeXRE-style outer-glow outline. Three passes:
    ///   1. Mask: draw the selected mesh as a flat silhouette into the R8 mask RT.
    ///   2. Horizontal blur: fullscreen draw sampling the mask, write to blur RT.
    ///   3. Vertical blur + composite: fullscreen draw sampling mask+blur, alpha-blend onto backbuffer.
    /// Inside-silhouette pixels are discarded when FillColour.a ≤ 0, producing a ring-only glow.
    /// Must be called between RenderContent() and End().
    /// </summary>
    private void RenderOutlinePass()
    {
        if (_commandQueue == null || _swapChain == null || _camera == null) return;
        if (_outlineMaskTex == null || _outlineBlurTex == null) return;
        if (_outlineMaskPso == null || _outlineBlurHorzPso == null || _outlineBlurCompositePso == null) return;
        if (_outlineMaskRootSig == null || _outlineBlurRootSig == null) return;
        if (_outlineBlurCbHorz == null || _outlineBlurCbComposite == null) return;
        if (_outlineSrvHeap == null) return;

        int nodeId = _selectedNodeId;
        if (nodeId < 0) return;

        MeshRenderer? selectedRenderer = null;
        foreach (var (renderer, _, id, _) in _sceneMeshRenderers)
            if (id == nodeId) { selectedRenderer = renderer; break; }
        if (selectedRenderer == null) return;

        var commandList = _commandQueue.CommandList;
        int frameIndex  = _swapChain.CurrentBackBufferIndex;
        float ar        = _height > 0 ? (float)_width / _height : 16f / 9f;
        var cameraData  = _camera.GetCameraData(ar);

        var viewport = new Vortice.Mathematics.Viewport(0, 0, _width, _height);
        var scissor  = new Vortice.RawRect(0, 0, _width, _height);
        commandList.RSSetViewport(viewport);
        commandList.RSSetScissorRect(scissor);

        // ── Pass 1: mesh → mask ──────────────────────────────────────────────
        TransitionOutlineMask(commandList, ResourceStates.RenderTarget);
        commandList.OMSetRenderTargets(_outlineMaskRtv);
        commandList.ClearRenderTargetView(_outlineMaskRtv, new Vortice.Mathematics.Color4(0, 0, 0, 0));

        selectedRenderer.RenderOutlineMask(commandList, cameraData, frameIndex, _outlineMaskRootSig, _outlineMaskPso);

        // ── Pass 2: horizontal blur (mask → blur) ────────────────────────────
        TransitionOutlineMask(commandList, ResourceStates.PixelShaderResource);
        TransitionOutlineBlur(commandList, ResourceStates.RenderTarget);
        commandList.OMSetRenderTargets(_outlineBlurRtv);
        commandList.ClearRenderTargetView(_outlineBlurRtv, new Vortice.Mathematics.Color4(0, 0, 0, 0));

        commandList.SetGraphicsRootSignature(_outlineBlurRootSig);
        commandList.SetPipelineState(_outlineBlurHorzPso);
        commandList.SetDescriptorHeaps(_outlineSrvHeap);

        var blurStage0 = new OutlineBlurConsts
        {
            OutlineColour  = _outlineColour,
            FillColour     = _outlineFill,
            StepDirectionX = 1,
            StepDirectionY = 0,
            Stage          = 0,
            Width          = _outlineBlurWidth,
        };
        _outlineBlurCbHorz.Update(frameIndex, ref blurStage0);
        commandList.SetGraphicsRootConstantBufferView(0, _outlineBlurCbHorz.GetGPUVirtualAddress(frameIndex));

        int srvStride = _device!.Device.GetDescriptorHandleIncrementSize(
            DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView);
        var srvGpuBase  = _outlineSrvHeap.GetGPUDescriptorHandleForHeapStart();
        var maskSrvGpu  = srvGpuBase;
        var blurSrvGpu  = new GpuDescriptorHandle { Ptr = srvGpuBase.Ptr + (ulong)srvStride };

        // Stage 0 only samples MaskTex; alias t1 to MaskTex too so the in-flight blur RT
        // is never bound as an SRV.
        commandList.SetGraphicsRootDescriptorTable(1, maskSrvGpu);
        commandList.SetGraphicsRootDescriptorTable(2, maskSrvGpu);

        commandList.IASetPrimitiveTopology(Vortice.Direct3D.PrimitiveTopology.TriangleList);
        commandList.IASetVertexBuffers(0, new VertexBufferView());
        commandList.DrawInstanced(3, 1, 0, 0);

        // ── Pass 3: vertical blur + composite onto backbuffer ────────────────
        TransitionOutlineBlur(commandList, ResourceStates.PixelShaderResource);

        var backbufferRtv = _swapChain.GetRTVHandle(frameIndex);
        commandList.OMSetRenderTargets(backbufferRtv);

        commandList.SetPipelineState(_outlineBlurCompositePso);

        var blurStage1 = blurStage0;
        blurStage1.StepDirectionX = 0;
        blurStage1.StepDirectionY = 1;
        blurStage1.Stage          = 1;
        _outlineBlurCbComposite.Update(frameIndex, ref blurStage1);
        commandList.SetGraphicsRootConstantBufferView(0, _outlineBlurCbComposite.GetGPUVirtualAddress(frameIndex));

        commandList.SetGraphicsRootDescriptorTable(1, maskSrvGpu);
        commandList.SetGraphicsRootDescriptorTable(2, blurSrvGpu);
        commandList.DrawInstanced(3, 1, 0, 0);

        // Restore depth + the deferred RT set in case any later pass expects it.
        // End() only barriers and presents, so this isn't strictly required, but it keeps things tidy.
    }

    private void TransitionOutlineMask(ID3D12GraphicsCommandList commandList, ResourceStates target)
    {
        if (_outlineMaskTex == null || _outlineMaskState == target) return;
        commandList.ResourceBarrier(new ResourceBarrier(
            new ResourceTransitionBarrier(_outlineMaskTex, _outlineMaskState, target)));
        _outlineMaskState = target;
    }

    private void TransitionOutlineBlur(ID3D12GraphicsCommandList commandList, ResourceStates target)
    {
        if (_outlineBlurTex == null || _outlineBlurState == target) return;
        commandList.ResourceBarrier(new ResourceBarrier(
            new ResourceTransitionBarrier(_outlineBlurTex, _outlineBlurState, target)));
        _outlineBlurState = target;
    }

    private static string StripFilesPathPrefix(string path, string filesPath)
    {
        if (string.IsNullOrEmpty(filesPath)) return path;
        var normalized = path.Replace('\\', '/').ToLowerInvariant();
        return normalized.StartsWith(filesPath) ? normalized[filesPath.Length..] : normalized;
    }

    #endregion

    public string GetRenderStats()
    {
        return $"FPS: {_fps:F1} ({_frameTime:F2}ms)";
    }

    private void UpdateFpsStats()
    {
        _frameCount++;
        var elapsed = _fpsStopwatch.Elapsed.TotalSeconds;

        if (elapsed >= 0.5)
        {
            _fps = _frameCount / elapsed;
            _frameTime = (elapsed * 1000.0) / _frameCount;
            _frameCount = 0;
            _fpsStopwatch.Restart();
        }
    }

    public void Dispose()
    {
        if (_isShutdown)
            return;

        // Dispose all mesh renderers (both pre-loaded and scene-loaded)
        foreach (var meshRenderer in _meshRenderers)
            meshRenderer?.Dispose();
        _meshRenderers.Clear();

        foreach (var (mr, _, _, _) in _sceneMeshRenderers)
            mr?.Dispose();
        _sceneMeshRenderers.Clear();

        // Clean up G-Buffers, outline pipeline, and cached DXGI objects
        CleanupGBuffers();
        CleanupOutlineResources();
        _cachedDxgiAdapter?.Dispose();
        _cachedDxgiAdapter = null;
        
        _grid?.Shutdown();
        _commandQueue?.Shutdown();
        _swapChain?.Shutdown();
        _device?.Shutdown();

        _grid?.Dispose();
        _commandQueue?.Dispose();
        _swapChain?.Dispose();
        _device?.Dispose();
        _resourceManager.Dispose();

        _grid = null;
        _commandQueue = null;
        _swapChain = null;
        _device = null;

        _isInitialized = false;
        _isShutdown = true;

        Console.WriteLine("Renderer shut down");
    }
}