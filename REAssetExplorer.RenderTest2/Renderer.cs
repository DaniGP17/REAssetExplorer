using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using REAssetExplorer.Core.Common;
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
    public int VSync { get; set; } = 1; // 0=off, 1=on, 2=adaptive
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
    
    // G-Buffers for deferred rendering
    private ID3D12Resource?[] _gBuffers = new ID3D12Resource?[3]; // RT1, RT2, RT3 (RT0 is swap chain backbuffer)
    private ID3D12DescriptorHeap? _rtvHeap; // RTV heap for G-Buffers
    private CpuDescriptorHandle[]? _gBufferRtvHandles;
    
    // Rendering state
    private ulong _renderFrame = 0;
    private readonly Stopwatch _frameStopwatch = new Stopwatch();
    private readonly Stopwatch _fpsStopwatch = Stopwatch.StartNew();
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

    public double Fps => _fps;
    public double FrameTime => _frameTime;
    public bool IsInitialized => _isInitialized;
    public RenderConfig Config => _config;
    public DisplayConfig DisplayConfig => _displayConfig;
    public RenderStatistics Statistics => _statistics;
    
    public static Dictionary<string, PakFile>? PakFiles => _pakFiles;
    public static IGameProvider? GameProvider => _gameProvider;

    public Renderer(Dictionary<string, PakFile> pakFiles, IGameProvider provider, MaterialsCache? materialsCache = null)
    {
        _pakFiles = pakFiles ?? throw new ArgumentNullException(nameof(pakFiles));
        _gameProvider = provider ?? throw new ArgumentNullException(nameof(provider));
        
        // Set singleton instance
        Instance = this;
        
        _resourceManager = new ResourceManager(pakFiles, provider, materialsCache);
        
        LoadSystemDeps();
        
        _resourceManager.EnqueueLoad("c01_living01.mesh", ResourceType.Mesh);
        _resourceManager.EnqueueLoad("c01_corridor01.mesh", ResourceType.Mesh);
        _resourceManager.EnqueueLoad("c01_corridor02.mesh", ResourceType.Mesh);
        _resourceManager.EnqueueLoad("c01_corridor03.mesh", ResourceType.Mesh);
        _resourceManager.EnqueueLoad("c01_bathroom01.mesh", ResourceType.Mesh);
        _resourceManager.EnqueueLoad("c01_kitchen01.mesh", ResourceType.Mesh);
        //_resourceManager.EnqueueLoad("sm21_144_livingroom_00.mesh", ResourceType.Mesh);
        _resourceManager.ProcessLoadQueue();
    }

    private void LoadSystemDeps()
    {
        if (_gameProvider?.ShaderSystemDeps is not IShaderSystemDeps shaderDeps)
        {
            Console.WriteLine("No shader system dependencies provided by the game provider");
            return;
        }

        foreach (var dep in shaderDeps.GetShaderSystemDeps().ToList())
        {
            _resourceManager.EnqueueLoad(dep, ResourceManager.GetResourceTypeFromExtension(dep), loadDependencies: true);
        }
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

        _resourceManager.ProcessLoadQueue(maxItemsPerFrame: 30);
        _resourceManager.ResourceReleaseLastExecute();
        
        try
        {
            // Start frame timing
            _frameStopwatch.Restart();
            
            // Update camera
            if (_camera != null)
            {
                _camera.Update((float)_frameTime / 1000.0f);
            }

            // Begin rendering
            Begin();
            
            // Render content
            RenderContent();
            
            // End rendering
            End();
            
            // Present
            Present();
            
            // Wait for GPU
            Wait();
            
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
            
        var commandAllocator = _commandQueue.CommandAllocator;
        var commandList = _commandQueue.CommandList;

        commandAllocator.Reset();
        commandList.Reset(commandAllocator, null);

        int frameIndex = _swapChain.CurrentBackBufferIndex;
        var renderTarget = _swapChain.GetRenderTarget(frameIndex);

        if (renderTarget == null)
            return;

        // Transition to render target
        var barrier = new ResourceBarrier(
            new ResourceTransitionBarrier(renderTarget, ResourceStates.Present, ResourceStates.RenderTarget));
        commandList.ResourceBarrier(barrier);
        
        // Reset per-frame statistics
        if (_config.StatisticsEnabled)
        {
            _statistics.Reset();
        }
    }
    
    private void RenderContent()
    {
        if (_commandQueue == null || _swapChain == null)
            return;
            
        var commandList = _commandQueue.CommandList;
        int frameIndex = _swapChain.CurrentBackBufferIndex;
        
        // Clear all render targets
        var rtvHandle = _swapChain.GetRTVHandle(frameIndex);
        var clearColor = new Color4(0.1f, 0.2f, 0.4f, 1.0f);
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

        // Clear depth stencil
        var dsvHandle = _swapChain.GetDSVHandle();
        commandList.ClearDepthStencilView(dsvHandle, ClearFlags.Depth, 1.0f, 0);

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
            
        var commandList = _commandQueue.CommandList;
        int frameIndex = _swapChain.CurrentBackBufferIndex;
        var renderTarget = _swapChain.GetRenderTarget(frameIndex);

        if (renderTarget == null)
            return;

        // Transition to present
        var barrier = new ResourceBarrier(
            new ResourceTransitionBarrier(renderTarget, ResourceStates.RenderTarget, ResourceStates.Present));
        commandList.ResourceBarrier(barrier);

        // Execute command list
        commandList.Close();
        _commandQueue.CommandQueue.ExecuteCommandList(commandList);
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
            
        try
        {
            // Get adapter from device
            if (_device.Factory == null)
                return;
                
            _device.Factory.EnumAdapters1(0, out var adapter);
            if (adapter == null)
                return;

            var adapterDesc = adapter.Description1;
            
            // Query video memory info if supported (DXGI 1.4+)
            if (adapter is IDXGIAdapter3 adapter3)
            {
                var memoryInfo = adapter3.QueryVideoMemoryInfo(0, MemorySegmentGroup.Local);
                _statistics.UsedVramSize64 = memoryInfo.CurrentUsage;
                _statistics.UsedVramSize32 = (uint)(memoryInfo.CurrentUsage / (1024 * 1024)); // Convert to MB
            }
            else
            {
                // Fallback: use dedicated video memory from adapter desc
                _statistics.UsedVramSize64 = (ulong)(long)adapterDesc.DedicatedVideoMemory;
                _statistics.UsedVramSize32 = (uint)((ulong)( long)adapterDesc.DedicatedVideoMemory / (1024 * 1024));
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to query VRAM: {ex.Message}");
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

        // Dispose all mesh renderers
        foreach (var meshRenderer in _meshRenderers)
        {
            meshRenderer?.Dispose();
        }
        _meshRenderers.Clear();

        // Clean up G-Buffers
        CleanupGBuffers();
        
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