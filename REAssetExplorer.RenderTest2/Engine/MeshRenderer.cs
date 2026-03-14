using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.DX12;
using REAssetExplorer.RenderTest2.Scene;
using Vortice.D3DCompiler;
using Vortice.Direct3D;
using Vortice.Direct3D12;
using Vortice.DXGI;
using ShaderResourceViewDimension = Vortice.Direct3D12.ShaderResourceViewDimension;

namespace REAssetExplorer.RenderTest2.Engine;

/// <summary>
/// Constant buffer structure for mesh rendering (MVP matrices)
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct MeshConstants
{
    public Matrix4x4 Model;
    public Matrix4x4 View;
    public Matrix4x4 Projection;
    public Matrix4x4 ModelViewProjection;
}

/// <summary>
/// Material constants matching the UserMaterial cbuffer in the pixel shader
/// Automatically mapped from RenderMaterial properties by name
/// (Alias for Env_2Layer_Common_Dirt for backward compatibility)
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants
{
    // Vector4 fields
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 BaseColor1;
    public Vector4 UV_Tiling_Offset1;
    public Vector4 WearMap_Tiling_Offset1;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float ExtraLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;
    public float MaterialColor_AddBlend1;
    public float MaterialColor_Override1;
    public float MaterialColor_Intensity1;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Roughness1;
    public float MaterialTranslucency1;
    public float PhysicalMaterialIndex1;
    public float UV_Tiling1;
    public float Use_AdvancedUVSetting1;
    public float NormalBlend_Balance1;
    public float WearMap_Tiling1;
    public float Use_AdvancedUVSetting_WearMap1;
    public float WearMap_Inverse1;
    public float WearMap_Blend1;
    public float WearMap_Normal_BlendMode1;
    public float WearMap_NormalIntensity1;
    public float LayerMask_BrightnessMasking1;
    public float LayerMask_Brightness1;
    public float LayerMask_Contrast1;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

/// <summary>
/// Represents a material slot with its textures, shader, and descriptor heap offset
/// Now uses generic wrapper - no more type-specific fields needed!
/// </summary>
internal class MaterialSlot
{
    public ID3D12Resource?[] Textures { get; set; } = Array.Empty<ID3D12Resource?>();
    public int TextureCount { get; set; } = 0;
    public int HeapOffset { get; set; } = 0; // Offset in the descriptor heap
    public ID3D12PipelineState? PipelineState { get; set; } = null; // Pipeline state for this material
    public string VertexShaderPath { get; set; } = "";
    public string PixelShaderPath { get; set; } = "";
    public MaterialStructureType StructureType { get; set; } = MaterialStructureType.Env_2Layer_Common_Dirt;
    public object MaterialParams { get; set; } = new MaterialConstants_Env_2Layer_Common_Dirt();
    
    // New generic wrapper - replaces the type-specific MaterialConstantBuffer
    public IMaterialConstantBufferWrapper? MaterialConstantBuffer { get; set; } = null;
}

/// <summary>
/// Cached pipeline state for a shader combination
/// </summary>
internal class CachedPipelineState
{
    public string VertexShaderPath { get; set; } = "";
    public string PixelShaderPath { get; set; } = "";
    public ID3D12PipelineState? PipelineState { get; set; } = null;
}

/// <summary>
/// Renders a RenderMesh using DirectX 12 with deferred rendering shaders
/// Supports multiple materials per submesh
/// </summary>
public class MeshRenderer : IDisposable
{
    private const int MaxTextureSlots = 16; // Maximum number of texture slots per material
    
    // Debug: Set to specific submesh index to render only that submesh, or -1 to render all
    private readonly int _debugSubmeshIndex = -1;
    
    private readonly RenderMesh _mesh;
    private readonly ID3D12Device _device;
    
    private ID3D12Resource? _vertexBuffer;
    private ID3D12Resource? _indexBuffer;
    private VertexBufferView _vertexBufferView;
    private IndexBufferView _indexBufferView;
    
    private ID3D12RootSignature? _rootSignature;
    private ConstantBuffer<MeshConstants>? _constantBuffer;
    
    // Pipeline state cache - stores unique pipeline states by shader combination
    private readonly Dictionary<string, CachedPipelineState> _pipelineStateCache = new();
    
    // Material system - one material slot per submesh
    private MaterialSlot[] _materialSlots = Array.Empty<MaterialSlot>();
    private ID3D12DescriptorHeap? _srvHeap;
    private int _totalDescriptorCount = 0;
    
    private bool _initialized;
    private bool _disposed;
    private bool _printedTransform;
    
    public Matrix4x4 Transform { get; set; } = Matrix4x4.Identity;
    
    public MeshRenderer(ID3D12Device device, RenderMesh mesh)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
        _mesh = mesh ?? throw new ArgumentNullException(nameof(mesh));
    }
    
    public bool Initialize()
    {
        if (_initialized)
            return true;
            
        try
        {
            // Create vertex buffer
            if (!CreateVertexBuffer())
                return false;
                
            // Create index buffer
            if (!CreateIndexBuffer())
                return false;
                
            // Create constant buffer
            _constantBuffer = new ConstantBuffer<MeshConstants>(_device, 3);
            
            // Create root signature
            if (!CreateRootSignature())
                return false;
            
            // Initialize material slots (one per submesh)
            InitializeMaterialSlots();
            
            // Create pipeline states for all unique materials
            if (!CreateMaterialPipelineStates())
                return false;
            
            // Create descriptor heap for all materials
            CreateMaterialDescriptorHeap();
            
            _initialized = true;
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during mesh renderer initialization: {ex.Message}");
            Console.WriteLine(ex.StackTrace);
            return false;
        }
    }
    
    private bool CreateVertexBuffer()
    {
        if (_mesh.Vertices.Length == 0)
            return false;
            
        int vertexBufferSize = _mesh.Vertices.Length * Marshal.SizeOf<RenderVertex>();
        
        var heapProps = new HeapProperties(HeapType.Upload);
        var bufferDesc = ResourceDescription.Buffer((ulong)vertexBufferSize);
        
        _vertexBuffer = _device.CreateCommittedResource(
            heapProps,
            HeapFlags.None,
            bufferDesc,
            ResourceStates.GenericRead);
            
        if (_vertexBuffer == null)
        {
            Console.WriteLine("Failed to create vertex buffer");
            return false;
        }
        
        unsafe
        {
            IntPtr pData;
            _vertexBuffer.Map(0, null, &pData);
            Span<RenderVertex> dest = new Span<RenderVertex>(pData.ToPointer(), _mesh.Vertices.Length);
            _mesh.Vertices.CopyTo(dest);
            _vertexBuffer.Unmap(0);
        }
        
        _vertexBufferView = new VertexBufferView
        {
            BufferLocation = _vertexBuffer.GPUVirtualAddress,
            StrideInBytes = Marshal.SizeOf<RenderVertex>(),
            SizeInBytes = vertexBufferSize
        };
        
        return true;
    }
    
    private bool CreateIndexBuffer()
    {
        if (_mesh.Indices.Length == 0)
            return false;
            
        int indexBufferSize = _mesh.Indices.Length * sizeof(uint);
        
        var heapProps = new HeapProperties(HeapType.Upload);
        var bufferDesc = ResourceDescription.Buffer((ulong)indexBufferSize);
        
        _indexBuffer = _device.CreateCommittedResource(
            heapProps,
            HeapFlags.None,
            bufferDesc,
            ResourceStates.GenericRead);
            
        if (_indexBuffer == null)
        {
            Console.WriteLine("Failed to create index buffer");
            return false;
        }
        
        // Copy index data
        unsafe
        {
            IntPtr pData;
            _indexBuffer.Map(0, null, &pData);
            Span<uint> dest = new Span<uint>(pData.ToPointer(), _mesh.Indices.Length);
            _mesh.Indices.CopyTo(dest);
            _indexBuffer.Unmap(0);
        }
        
        // Create index buffer view
        _indexBufferView = new IndexBufferView
        {
            BufferLocation = _indexBuffer.GPUVirtualAddress,
            Format = Format.R32_UInt,
            SizeInBytes = indexBufferSize
        };
        
        return true;
    }
    
    /// <summary>
    /// Creates a pipeline state for specific shader files
    /// </summary>
    private ID3D12PipelineState? CreatePipelineStateForShaders(string vertexShaderPath, string pixelShaderPath)
    {
        try
        {
            // Resolve full paths
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string vsPath = Path.IsPathRooted(vertexShaderPath) 
                ? vertexShaderPath 
                : Path.Combine(baseDir, vertexShaderPath);
            string psPath = Path.IsPathRooted(pixelShaderPath)
                ? pixelShaderPath
                : Path.Combine(baseDir, pixelShaderPath);
            
            if (!File.Exists(vsPath))
            {
                Console.WriteLine($"Vertex shader not found: {vsPath}");
                return null;
            }
            
            if (!File.Exists(psPath))
            {
                Console.WriteLine($"Pixel shader not found: {psPath}");
                return null;
            }
            
            // Read shader source code
            string vsSource = File.ReadAllText(vsPath);
            string psSource = File.ReadAllText(psPath);
            
            var vsBlob = Compiler.Compile(vsSource, "main", string.Empty, "vs_5_1");
            
            if (vsBlob.Length == 0)
            {
                Console.WriteLine($"Failed to compile vertex shader: {vsPath}");
                return null;
            }
            var psBlob = Compiler.Compile(psSource, "main", string.Empty, "ps_5_1");
            
            if (psBlob.Length == 0)
            {
                Console.WriteLine($"Failed to compile pixel shader: {psPath}");
                return null;
            }
            
            // Create input layout matching our vertex structure
            var inputElements = new[]
            {
                new InputElementDescription("POSITION", 0, Format.R32G32B32_Float, 0, 0),
                new InputElementDescription("NORMAL", 0, Format.R32G32B32_Float, 12, 0),
                new InputElementDescription("TANGENT", 0, Format.R32G32B32A32_Float, 24, 0),
                new InputElementDescription("TEXCOORD", 0, Format.R32G32_Float, 40, 0),
                new InputElementDescription("TEXCOORD", 1, Format.R32G32_Float, 48, 0)
            };
            
            // Create pipeline state with 4 render targets for deferred rendering
            var psoDesc = new GraphicsPipelineStateDescription
            {
                RootSignature = _rootSignature,
                VertexShader = vsBlob,
                PixelShader = psBlob,
                InputLayout = new Vortice.Direct3D12.InputLayoutDescription(inputElements),
                PrimitiveTopologyType = PrimitiveTopologyType.Triangle,
                RasterizerState = RasterizerDescription.CullClockwise,
                BlendState = BlendDescription.Opaque,
                DepthStencilState = new DepthStencilDescription
                {
                    DepthEnable = true,
                    DepthWriteMask = DepthWriteMask.All,
                    DepthFunc = ComparisonFunction.LessEqual,
                    StencilEnable = false
                },
                DepthStencilFormat = Format.D32_Float,
                SampleMask = uint.MaxValue,
                // 4 render targets for deferred rendering G-Buffers
                // RT0: Unused (reserved)
                // RT1: Albedo.RGB + Metallic/Translucency.A
                // RT2: Encoded Normal.XY + Roughness.Z + GBuffer Type.W
                // RT3: AO.X + Screen UV.YZ + Misc.W
                RenderTargetFormats = new[] 
                { 
                    Format.R8G8B8A8_UNorm,      // RT0
                    Format.R8G8B8A8_UNorm,      // RT1: Albedo + Metallic
                    Format.R16G16B16A16_Float,  // RT2: Normal + Roughness + Type (needs higher precision)
                    Format.R16G16B16A16_Float   // RT3: AO + UV + Misc (higher precision for UV)
                },
                SampleDescription = new SampleDescription(1, 0)
            };
            
            var pipelineState = _device.CreateGraphicsPipelineState(psoDesc);
            
            if (pipelineState == null)
            {
                Console.WriteLine($"Failed to create pipeline state for VS: {vsPath}, PS: {psPath}");
                return null;
            }
            
            Console.WriteLine($"Created pipeline state: VS={Path.GetFileName(vsPath)}, PS={Path.GetFileName(psPath)}");
            return pipelineState;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating pipeline state: {ex.Message}");
            Console.WriteLine($"Stack trace: {ex.StackTrace}");
            return null;
        }
    }
    
    private bool CreateRootSignature()
    {
        try
        {
            // Root signature with CBVs and descriptor table for textures
            var rootParameters = new[]
            {
                // Parameter 0: CBV for vertex shader (MVP matrices) - register(b0) in VS
                new RootParameter1(
                    RootParameterType.ConstantBufferView,
                    new RootDescriptor1(0, 0),
                    ShaderVisibility.Vertex),
                
                // Parameter 1: CBV for pixel shader (material parameters) - register(b0) in PS
                new RootParameter1(
                    RootParameterType.ConstantBufferView,
                    new RootDescriptor1(0, 0),
                    ShaderVisibility.Pixel),
                
                // Parameter 2: Descriptor table for pixel shader textures (up to MaxTextureSlots SRVs)
                new RootParameter1(
                    new RootDescriptorTable1(
                        new DescriptorRange1(DescriptorRangeType.ShaderResourceView, MaxTextureSlots, 0)),
                    ShaderVisibility.Pixel)
            };
            
            // Static sampler for texture sampling
            var staticSamplers = new[]
            {
                new StaticSamplerDescription(
                    ShaderVisibility.Pixel,
                    0, // register s0
                    0) // register space
                {
                    Filter = Filter.MinMagMipLinear,
                    AddressU = TextureAddressMode.Wrap,
                    AddressV = TextureAddressMode.Wrap,
                    AddressW = TextureAddressMode.Wrap,
                    ComparisonFunction = ComparisonFunction.Never,
                    MaxAnisotropy = 16,
                    MinLOD = 0.0f,
                    MaxLOD = float.MaxValue
                }
            };
            
            var rootSignatureDesc = new RootSignatureDescription1(
                RootSignatureFlags.AllowInputAssemblerInputLayout,
                rootParameters,
                staticSamplers);
            
            _rootSignature = _device.CreateRootSignature(rootSignatureDesc);
            
            if (_rootSignature == null)
            {
                Console.WriteLine("Failed to create root signature");
                return false;
            }
            
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to create root signature: {ex.Message}");
            return false;
        }
    }
    
    public void Render(ID3D12GraphicsCommandList commandList, CameraData camera, int frameIndex)
    {
        if (!_initialized || _vertexBuffer == null || _indexBuffer == null || _constantBuffer == null)
            return;
        
        if (_rootSignature == null)
            return;
        
        // Set root signature (shared by all materials)
        commandList.SetGraphicsRootSignature(_rootSignature);
            
        // Set vertex and index buffers
        commandList.IASetPrimitiveTopology(PrimitiveTopology.TriangleList);
        commandList.IASetVertexBuffers(0, _vertexBufferView);
        commandList.IASetIndexBuffer(_indexBufferView);
        
        float aspectRatio = camera.AspectRatio > 0 ? camera.AspectRatio : 16.0f / 9.0f;
        var view = Matrix4x4.CreateLookAt(camera.Position, camera.Target, camera.Up);
        var projection = Matrix4x4.CreatePerspectiveFieldOfView(camera.Fov, aspectRatio, camera.NearPlane, camera.FarPlane);
        
        
        var mvp = Transform * view * projection;
        
        var constants = new MeshConstants
        {
            Model = Transform,
            View = view,
            Projection = projection,
            ModelViewProjection = mvp
        };
        
        _constantBuffer.Update(frameIndex, ref constants);
        
        // Set constant buffer view (root parameter 0)
        var cbAddress = _constantBuffer.GetGPUVirtualAddress(frameIndex);
        commandList.SetGraphicsRootConstantBufferView(0, cbAddress);
        
        // Set descriptor heap
        if (_srvHeap != null)
        {
            commandList.SetDescriptorHeaps(new[] { _srvHeap });
        }
        
        // Render all submeshes with their respective materials and shaders
        ID3D12PipelineState? lastPipelineState = null;
        
        // Determine which submeshes to render
        IEnumerable<int> submeshesToRender;
        if (_debugSubmeshIndex == -1)
        {
            submeshesToRender = Enumerable.Range(0, _mesh.SubMeshes.Count);
        }
        else
        {
            submeshesToRender = new[] { _debugSubmeshIndex };
        }
        
        foreach (int i in submeshesToRender)
        {
            if (i >= _materialSlots.Length || _materialSlots[i] == null)
                continue;
                
            var subMesh = _mesh.SubMeshes[i];
            var materialSlot = _materialSlots[i];
            
            // Change pipeline state only if different from last submesh (optimization)
            if (materialSlot.PipelineState != null && materialSlot.PipelineState != lastPipelineState)
            {
                commandList.SetPipelineState(materialSlot.PipelineState);
                lastPipelineState = materialSlot.PipelineState;
            }
            
            // Update and set material constant buffer (root parameter 1)
            // New simplified approach - no more switch statement needed!
            if (materialSlot.MaterialConstantBuffer != null)
            {
                materialSlot.MaterialConstantBuffer.UpdateConstants(frameIndex, materialSlot.MaterialParams);
                var matCbAddress = materialSlot.MaterialConstantBuffer.GetGPUVirtualAddress(frameIndex);
                commandList.SetGraphicsRootConstantBufferView(1, matCbAddress);
            }
            
            // Set descriptor table for this submesh's textures (root parameter 2)
            if (_srvHeap != null)
            {
                var baseGpuHandle = _srvHeap.GetGPUDescriptorHandleForHeapStart();
                var descriptorSize = _device.GetDescriptorHandleIncrementSize(
                    DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView);
                
                // Offset to this material's descriptor range
                baseGpuHandle.Ptr += (nuint)(materialSlot.HeapOffset * descriptorSize);
                commandList.SetGraphicsRootDescriptorTable(2, baseGpuHandle);
            }
            
            // Draw this submesh
            commandList.DrawIndexedInstanced(
                (int)subMesh.IndexCount,
                1,
                (int)subMesh.StartIndex,
                subMesh.BaseVertex,
                0);
        }
    }
    
    #region Material Management
    
    /// <summary>
    /// Initializes material slots based on submesh count
    /// </summary>
    private void InitializeMaterialSlots()
    {
        int submeshCount = _mesh.SubMeshes.Count;
        _materialSlots = new MaterialSlot[submeshCount];
        
        // Determine which submeshes to initialize
        List<int> submeshesToInit = new List<int>();
        if (_debugSubmeshIndex == -1)
        {
            // Render all submeshes
            Console.WriteLine($"Initializing all {submeshCount} submeshes");
            for (int i = 0; i < submeshCount; i++)
                submeshesToInit.Add(i);
        }
        else
        {
            // Render only specific submesh
            int targetSubmesh = _debugSubmeshIndex;
            if (targetSubmesh >= submeshCount)
            {
                Console.WriteLine($"Warning: Submesh {targetSubmesh} doesn't exist (total: {submeshCount}), using submesh 0");
                targetSubmesh = 0;
            }
            Console.WriteLine($"Initializing only submesh {targetSubmesh}");
            submeshesToInit.Add(targetSubmesh);
        }
        
        // Initialize selected submeshes
        foreach (int i in submeshesToInit)
        {
            var submesh = _mesh.SubMeshes[i];
            string vsPath, psPath;
            MaterialStructureType structureType;
            
            // Get shader info from material if available
            if (submesh.Material != null)
            {
                (vsPath, psPath, structureType) = submesh.Material.GetShaderInfo();
            }
            else
            {
                // Default shaders if no material
                vsPath = "Shaders/DeferredVertex.hlsl";
                psPath = "Shaders/Env_DefaultPS.hlsl";
                structureType = MaterialStructureType.Default;
            }
            
            // Create constant buffer wrapper using factory - no more switch needed!
            var materialCB = MaterialConstantBufferFactory.CreateWrapper(structureType, _device, 3);
            if (materialCB == null)
            {
                Console.WriteLine($"Failed to create material constant buffer for submesh {i}, type {structureType}");
                continue;
            }
            
            // Extract material parameters using the helper method
            var materialParams = ExtractMaterialParametersForType(submesh.Material, structureType);
            
            _materialSlots[i] = new MaterialSlot
            {
                Textures = new ID3D12Resource?[MaxTextureSlots],
                TextureCount = 0,
                HeapOffset = i * MaxTextureSlots,
                VertexShaderPath = vsPath,
                PixelShaderPath = psPath,
                StructureType = structureType,
                MaterialParams = materialParams,
                MaterialConstantBuffer = materialCB
            };
        }
        
        _totalDescriptorCount = submeshCount * MaxTextureSlots;
    }
    
    /// <summary>
    /// Creates pipeline states for all unique material shader combinations
    /// </summary>
    private bool CreateMaterialPipelineStates()
    {
        // Find all unique shader combinations (skip null slots)
        var uniqueShaders = new HashSet<(string vs, string ps)>();
        foreach (var slot in _materialSlots)
        {
            if (slot == null)
                continue;
                
            uniqueShaders.Add((slot.VertexShaderPath, slot.PixelShaderPath));
        }
        
        // Create pipeline state for each unique combination
        foreach (var (vsPath, psPath) in uniqueShaders)
        {
            var key = $"{vsPath}|{psPath}";
            if (_pipelineStateCache.ContainsKey(key))
                continue;
                
            var pipelineState = CreatePipelineStateForShaders(vsPath, psPath);
            if (pipelineState == null)
            {
                Console.WriteLine($"Failed to create pipeline state for VS: {vsPath}, PS: {psPath}");
                // Continue with other materials, don't fail completely
                continue;
            }
            
            _pipelineStateCache[key] = new CachedPipelineState
            {
                VertexShaderPath = vsPath,
                PixelShaderPath = psPath,
                PipelineState = pipelineState
            };
        }
        
        // Assign pipeline states to material slots (skip null slots)
        foreach (var slot in _materialSlots)
        {
            if (slot == null)
                continue;
                
            var key = $"{slot.VertexShaderPath}|{slot.PixelShaderPath}";
            if (_pipelineStateCache.TryGetValue(key, out var cached))
            {
                slot.PipelineState = cached.PipelineState;
            }
        }
        
        // At least one pipeline state must be created
        return _pipelineStateCache.Count > 0;
    }
    
    /// <summary>
    /// Creates the descriptor heap for all materials
    /// Each submesh gets MaxTextureSlots descriptors
    /// </summary>
    private void CreateMaterialDescriptorHeap()
    {
        if (_totalDescriptorCount == 0)
        {
            Console.WriteLine("Warning: No material slots to create descriptor heap for");
            return;
        }
        
        try
        {
            var heapDesc = new DescriptorHeapDescription
            {
                Type = DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView,
                DescriptorCount = _totalDescriptorCount,
                Flags = DescriptorHeapFlags.ShaderVisible
            };
            
            _srvHeap = _device.CreateDescriptorHeap(heapDesc);
            if (_srvHeap == null)
            {
                Console.WriteLine("Failed to create material descriptor heap");
                return;
            }
            
            // Initialize all descriptors with null SRVs
            UpdateAllMaterialDescriptors();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to create material descriptor heap: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Sets textures for a specific submesh material
    /// </summary>
    /// <param name="submeshIndex">Index of the submesh</param>
    /// <param name="textures">Array of texture resources (up to MaxTextureSlots)</param>
    public void SetMaterialTextures(int submeshIndex, params ID3D12Resource?[] textures)
    {
        if (submeshIndex < 0 || submeshIndex >= _materialSlots.Length)
        {
            Console.WriteLine($"Invalid submesh index: {submeshIndex}");
            return;
        }
        
        var materialSlot = _materialSlots[submeshIndex];
        
        // Copy textures to material slot
        int count = Math.Min(textures.Length, MaxTextureSlots);
        for (int i = 0; i < MaxTextureSlots; i++)
        {
            materialSlot.Textures[i] = i < count ? textures[i] : null;
        }
        materialSlot.TextureCount = count;
        
        // Update descriptors for this material slot
        UpdateMaterialDescriptors(submeshIndex);
    }
    
    /// <summary>
    /// Sets the same textures for all submeshes (legacy compatibility)
    /// </summary>
    /// <param name="textures">Array of texture resources</param>
    public void SetTextures(params ID3D12Resource?[] textures)
    {
        for (int i = 0; i < _materialSlots.Length; i++)
        {
            SetMaterialTextures(i, textures);
        }
    }
    
    /// <summary>
    /// Loads materials from RenderMesh submesh data
    /// Automatically creates GPU resources for textures and assigns them to material slots
    /// </summary>
    /// <param name="commandQueue">Optional command queue for uploading texture data to GPU. If null, textures are created but data is not uploaded.</param>
    public void LoadMaterialsFromMesh(DX12CommandQueue? commandQueue = null)
    {
        if (!_initialized)
        {
            Console.WriteLine("Cannot load materials: MeshRenderer not initialized");
            return;
        }
        
        // Determine which submeshes to load materials for
        IEnumerable<int> submeshesToLoad;
        if (_debugSubmeshIndex == -1)
        {
            submeshesToLoad = Enumerable.Range(0, Math.Min(_mesh.SubMeshes.Count, _materialSlots.Length));
        }
        else
        {
            submeshesToLoad = new[] { _debugSubmeshIndex };
        }

        foreach (int i in submeshesToLoad)
        {
            if (i >= _mesh.SubMeshes.Count || i >= _materialSlots.Length || _materialSlots[i] == null)
                continue;
                
            var submesh = _mesh.SubMeshes[i];
            
            // Skip if no material
            if (submesh.Material == null)
                continue;
            
            var textureResources = new List<ID3D12Resource?>();
            
            foreach (var matTex in submesh.Material.Textures)
            {
                if (matTex.Texture != null)
                {
                    Console.WriteLine($"  Loading texture type '{matTex.TextureType}' for submesh {i} - {submesh.Material.Name}");
                    
                    // Get or create GPU resource for this texture (with upload if commandQueue provided)
                    var gpuResource = matTex.Texture.GetOrCreateGpuResource(_device, commandQueue);
                    if (gpuResource != null)
                    {
                        textureResources.Add(gpuResource);
                    }
                    else
                    {
                        Console.WriteLine($"Warning: Failed to create GPU resource for texture in material (submesh {i})");
                    }
                }
                else
                {
                    Console.WriteLine($"  No texture found for type '{matTex.TextureType}' in submesh {i}");
                }
            }
            
            if (textureResources.Count > 0)
            {
                SetMaterialTextures(i, textureResources.ToArray());
                Console.WriteLine($"Loaded {textureResources.Count} textures for submesh {i}");
            }
        }
    }
    
    /// <summary>
    /// Extracts material parameters from MaterialProperty array for any material type
    /// Uses reflection to automatically map all properties by name
    /// Now works with any material structure type without needing generics!
    /// </summary>
    private object ExtractMaterialParametersForType(RenderMaterial? material, MaterialStructureType structureType)
    {
        var info = MaterialTypeRegistry.GetInfo(structureType);
        if (info == null)
        {
            Console.WriteLine($"Unknown material structure type: {structureType}");
            return new MaterialConstants_Default();
        }
        
        // Create instance of the material structure
        object matParams = Activator.CreateInstance(info.Type)!;
        
        if (material == null)
            return matParams;
        
        // Get all fields from the material structure using reflection
        var fields = info.Type.GetFields(
            System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance);
        
        // Create dictionary for fast lookup
        var fieldDict = fields.ToDictionary(f => f.Name, f => f);
        
        // Initialize default values
        InitializeDefaultsForType(ref matParams, fieldDict, info.Type);
        
        // Map material properties to fields by name
        int mappedCount = 0;
        foreach (var prop in material.Properties)
        {
            if (string.IsNullOrEmpty(prop.Name) || prop.Parameters == null || prop.Parameters.Length == 0)
                continue;
            
            string fieldName = prop.Name;
            
            if (!fieldDict.TryGetValue(fieldName, out var field))
            {
                //Console.WriteLine($"  [MaterialExtraction] Field not found: {prop.Name}");
                continue;
            }
            
            try
            {
                var fieldType = field.FieldType;
                
                if (fieldType == typeof(Vector4))
                {
                    if (prop.Parameters.Length >= 4)
                    {
                        field.SetValue(matParams, new Vector4(
                            prop.Parameters[0], prop.Parameters[1], 
                            prop.Parameters[2], prop.Parameters[3]));
                        mappedCount++;
                    }
                    else if (prop.Parameters.Length == 3)
                    {
                        float w = fieldName.Contains("Color") ? 1.0f : 0.0f;
                        field.SetValue(matParams, new Vector4(
                            prop.Parameters[0], prop.Parameters[1], 
                            prop.Parameters[2], w));
                        mappedCount++;
                    }
                    else if (prop.Parameters.Length == 2)
                    {
                        field.SetValue(matParams, new Vector4(
                            prop.Parameters[0], prop.Parameters[1], 0, 0));
                        mappedCount++;
                    }
                }
                else if (fieldType == typeof(float))
                {
                    field.SetValue(matParams, prop.Parameters[0]);
                    mappedCount++;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  [MaterialExtraction] ERROR setting '{prop.Name}': {ex.Message}");
            }
        }
        
        Console.WriteLine($"[MaterialExtraction] Mapped {mappedCount} properties successfully");
        
        return matParams;
    }
    
    /// <summary>
    /// Initializes default values for material structure fields
    /// Now works with any type using reflection
    /// </summary>
    private static void InitializeDefaultsForType(
        ref object matParams, 
        Dictionary<string, System.Reflection.FieldInfo> fieldDict,
        Type structType)
    {
        foreach (var kvp in fieldDict)
        {
            var field = kvp.Value;
            var fieldName = kvp.Key;
            var fieldType = field.FieldType;
            
            try
            {
                if (fieldType == typeof(Vector4))
                {
                    if (fieldName.Contains("Color"))
                    {
                        // Colors default to white or gray
                        if (fieldName.Contains("Dirt"))
                            field.SetValue(matParams, new Vector4(0.5f, 0.5f, 0.5f, 1));
                        else
                            field.SetValue(matParams, new Vector4(1, 1, 1, 1));
                    }
                    else if (fieldName.Contains("Tiling") || fieldName.Contains("Offset"))
                    {
                        // Tiling/Offset default to (1,1,0,0)
                        field.SetValue(matParams, new Vector4(1, 1, 0, 0));
                    }
                    else
                    {
                        field.SetValue(matParams, new Vector4(0, 0, 0, 0));
                    }
                }
                else if (fieldType == typeof(float))
                {
                    // Smart defaults for floats
                    if (fieldName.Contains("Intensity") || fieldName.Contains("Contrast") || 
                        fieldName.Contains("Tiling") && !fieldName.Contains("Offset"))
                        field.SetValue(matParams, 1.0f);
                    else if (fieldName.Contains("Roughness") || fieldName.Contains("Brightness"))
                        field.SetValue(matParams, 0.5f);
                    else
                        field.SetValue(matParams, 0.0f);
                }
            }
            catch
            {
                // Ignore initialization errors
            }
        }
    }
    
    /// <summary>
    /// Updates descriptor heap entries for a specific material slot
    /// </summary>
    private void UpdateMaterialDescriptors(int submeshIndex)
    {
        if (_srvHeap == null || submeshIndex < 0 || submeshIndex >= _materialSlots.Length)
            return;
        
        var materialSlot = _materialSlots[submeshIndex];
        var descriptorSize = _device.GetDescriptorHandleIncrementSize(
            DescriptorHeapType.ConstantBufferViewShaderResourceViewUnorderedAccessView);
        
        var cpuHandle = _srvHeap.GetCPUDescriptorHandleForHeapStart();
        cpuHandle.Ptr += (nuint)(materialSlot.HeapOffset * descriptorSize);
        
        for (int i = 0; i < MaxTextureSlots; i++)
        {
            var texture = materialSlot.Textures[i];
            
            if (texture != null)
            {
                // Get actual texture description to determine format and mip levels
                var textureDesc = texture.Description;
                
                var srvDesc = new ShaderResourceViewDescription
                {
                    Format = textureDesc.Format,
                    ViewDimension = ShaderResourceViewDimension.Texture2D,
                    Shader4ComponentMapping = ShaderComponentMapping.Default,
                    Texture2D = new Texture2DShaderResourceView { MipLevels = textureDesc.MipLevels }
                };
                _device.CreateShaderResourceView(texture, srvDesc, cpuHandle);
            }
            else
            {
                // Create null descriptor for unused slots
                CreateNullSrv(cpuHandle);
            }
            
            cpuHandle.Ptr += (nuint)descriptorSize;
        }
    }
    
    /// <summary>
    /// Updates descriptors for all material slots
    /// </summary>
    private void UpdateAllMaterialDescriptors()
    {
        for (int i = 0; i < _materialSlots.Length; i++)
        {
            if (_materialSlots[i] != null)
                UpdateMaterialDescriptors(i);
        }
    }
    
    /// <summary>
    /// Creates a null SRV descriptor
    /// </summary>
    private void CreateNullSrv(CpuDescriptorHandle cpuHandle)
    {
        var nullDesc = new ShaderResourceViewDescription
        {
            Format = Format.R8G8B8A8_UNorm,
            ViewDimension = ShaderResourceViewDimension.Texture2D,
            Shader4ComponentMapping = ShaderComponentMapping.Default,
            Texture2D = new Texture2DShaderResourceView { MipLevels = 1 }
        };
        
        _device.CreateShaderResourceView(null, nullDesc, cpuHandle);
    }
    
    #endregion
    
    #region Deprecated Methods (kept for compatibility)
    
    private void EnsureSrvHeapCreated()
    {
        // Deprecated: Material descriptor heap is now created in Initialize()
        // Kept for compatibility, does nothing
    }
    
    #endregion
    
    public void Dispose()
    {
        if (_disposed)
            return;
        
        _srvHeap?.Dispose();
        _constantBuffer?.Dispose();
        
        // Dispose all cached pipeline states
        foreach (var cached in _pipelineStateCache.Values)
        {
            cached.PipelineState?.Dispose();
        }
        _pipelineStateCache.Clear();
        
        _rootSignature?.Dispose();
        _indexBuffer?.Dispose();
        _vertexBuffer?.Dispose();
        
        // Clear material slots
        if (_materialSlots != null)
        {
            foreach (var slot in _materialSlots)
            {
                if (slot == null)
                    continue;
                
                // Dispose material constant buffer using the wrapper - no switch needed!
                slot.MaterialConstantBuffer?.Dispose();
                
                // Note: we don't dispose the texture resources as they are managed elsewhere
                // Pipeline states are disposed above via cache
                slot.Textures = Array.Empty<ID3D12Resource?>();
                slot.TextureCount = 0;
                slot.PipelineState = null;
                slot.MaterialConstantBuffer = null;
            }
            _materialSlots = Array.Empty<MaterialSlot>();
        }
        
        _totalDescriptorCount = 0;
        _srvHeap = null;
        _constantBuffer = null;
        _rootSignature = null;
        _indexBuffer = null;
        _vertexBuffer = null;
        
        _disposed = true;
    }
}
