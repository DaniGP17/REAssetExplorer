using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using REAssetExplorer.RenderTest2.Scene;
using Vortice.D3DCompiler;
using Vortice.Direct3D;
using Vortice.Direct3D12;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.DX12;

public class Grid : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    private struct GridVertex
    {
        public System.Numerics.Vector3 Position;
        public Vector4 Color;

        public GridVertex(System.Numerics.Vector3 position, Vector4 color)
        {
            Position = position;
            Color = color;
        }
    }

    private ID3D12Resource? _vertexBuffer;
    private ID3D12Resource? _indexBuffer;
    private ID3D12RootSignature? _rootSignature;
    private ID3D12PipelineState? _pipelineState;

    private VertexBufferView _vertexBufferView;
    private IndexBufferView _indexBufferView;

    private List<GridVertex> _vertices = new();
    private List<ushort> _indices = new();
    private int _indexCount;
    private bool _disposed;

    public bool Initialize(ID3D12Device device, float size = 100.0f, int divisions = 100)
    {
        try
        {
            // Create grid geometry
            CreateGridGeometry(size, divisions);

            // Create vertex buffer
            if (!CreateVertexBuffer(device))
                return false;

            // Create index buffer
            if (!CreateIndexBuffer(device))
                return false;

            // Create root signature
            if (!CreateRootSignature(device))
                return false;

            // Create pipeline state
            if (!CreatePipelineState(device))
                return false;

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during grid initialization: {ex.Message}");
            return false;
        }
    }

    private void CreateGridGeometry(float size, int divisions)
    {
        _vertices.Clear();
        _indices.Clear();

        float halfSize = size / 2.0f;
        float step = size / divisions;

        // Colors
        var normalColor = new Vector4(0.5f, 0.5f, 0.5f, 1.0f);
        var majorColor = new Vector4(0.7f, 0.7f, 0.7f, 1.0f);
        var xAxisColor = new Vector4(1.0f, 0.0f, 0.0f, 1.0f);
        var zAxisColor = new Vector4(0.0f, 0.0f, 1.0f, 1.0f);

        ushort vertexIndex = 0;

        // Lines parallel to X axis
        for (int i = 0; i <= divisions; i++)
        {
            float z = -halfSize + i * step;
            Vector4 color = (i == divisions / 2) ? xAxisColor :
                           (i % 10 == 0) ? majorColor : normalColor;

            _vertices.Add(new GridVertex(new System.Numerics.Vector3(-halfSize, 0.0f, z), color));
            _vertices.Add(new GridVertex(new System.Numerics.Vector3(halfSize, 0.0f, z), color));

            _indices.Add(vertexIndex++);
            _indices.Add(vertexIndex++);
        }

        // Lines parallel to Z axis
        for (int i = 0; i <= divisions; i++)
        {
            float x = -halfSize + i * step;
            Vector4 color = (i == divisions / 2) ? zAxisColor :
                           (i % 10 == 0) ? majorColor : normalColor;

            _vertices.Add(new GridVertex(new System.Numerics.Vector3(x, 0.0f, -halfSize), color));
            _vertices.Add(new GridVertex(new System.Numerics.Vector3(x, 0.0f, halfSize), color));

            _indices.Add(vertexIndex++);
            _indices.Add(vertexIndex++);
        }

        _indexCount = _indices.Count;
    }

    private bool CreateVertexBuffer(ID3D12Device device)
    {
        int vertexBufferSize = _vertices.Count * Marshal.SizeOf<GridVertex>();

        var heapProps = new HeapProperties(HeapType.Upload);
        var bufferDesc = ResourceDescription.Buffer((ulong)vertexBufferSize);

        _vertexBuffer = device.CreateCommittedResource(
            heapProps,
            HeapFlags.None,
            bufferDesc,
            ResourceStates.GenericRead);

        if (_vertexBuffer == null)
        {
            Console.WriteLine("Failed to create vertex buffer");
            return false;
        }

        // Copy vertex data
        unsafe
        {
            IntPtr pData;
            _vertexBuffer.Map(0, null, &pData);
            Span<GridVertex> dest = new Span<GridVertex>(pData.ToPointer(), _vertices.Count);
            _vertices.CopyTo(dest);
            _vertexBuffer.Unmap(0);
        }

        // Create vertex buffer view
        _vertexBufferView = new VertexBufferView
        {
            BufferLocation = _vertexBuffer.GPUVirtualAddress,
            StrideInBytes = Marshal.SizeOf<GridVertex>(),
            SizeInBytes = vertexBufferSize
        };

        return true;
    }

    private bool CreateIndexBuffer(ID3D12Device device)
    {
        int indexBufferSize = _indices.Count * sizeof(ushort);

        var heapProps = new HeapProperties(HeapType.Upload);
        var bufferDesc = ResourceDescription.Buffer((ulong)indexBufferSize);

        _indexBuffer = device.CreateCommittedResource(
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
            Span<ushort> dest = new Span<ushort>(pData.ToPointer(), _indices.Count);
            _indices.CopyTo(dest);
            _indexBuffer.Unmap(0);
        }

        // Create index buffer view
        _indexBufferView = new IndexBufferView
        {
            BufferLocation = _indexBuffer.GPUVirtualAddress,
            Format = Format.R16_UInt,
            SizeInBytes = indexBufferSize
        };

        return true;
    }

    private bool CreateRootSignature(ID3D12Device device)
    {
        try
        {
            // Root parameter for view-projection matrix (16 float4 = 64 bytes)
            var rootParameters = new[]
            {
                new RootParameter1(
                    new RootConstants(0, 0, 16),
                    ShaderVisibility.Vertex)
            };

            var rootSignatureDesc = new RootSignatureDescription1(
                RootSignatureFlags.AllowInputAssemblerInputLayout,
                rootParameters);

            _rootSignature = device.CreateRootSignature(rootSignatureDesc);
            return _rootSignature != null;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to create root signature: {ex.Message}");
            return false;
        }
    }

    private bool CreatePipelineState(ID3D12Device device)
    {
        try
        {
            // Grid shader
            const string gridShader = @"
                cbuffer TransformBuffer : register(b0)
                {
                    row_major float4x4 viewProjection;
                };

                struct VSInput
                {
                    float3 position : POSITION;
                    float4 color : COLOR;
                };

                struct PSInput
                {
                    float4 position : SV_POSITION;
                    float4 color : COLOR;
                };

                PSInput VSMain(VSInput input)
                {
                    PSInput output;
                    float4 worldPos = float4(input.position, 1.0f);
                    output.position = mul(worldPos, viewProjection);
                    output.color = input.color;
                    return output;
                }

                float4 PSMain(PSInput input) : SV_TARGET
                {
                    return input.color;
                }
            ";

            // Compile shaders
            var vsBlob = Compiler.Compile(gridShader, "VSMain", string.Empty, "vs_5_0");
            var psBlob = Compiler.Compile(gridShader, "PSMain", string.Empty, "ps_5_0");

            if (vsBlob.Length == 0 || psBlob.Length == 0)
            {
                Console.WriteLine("Failed to compile grid shaders");
                return false;
            }

            // Input layout
            var inputElements = new[]
            {
                new InputElementDescription("POSITION", 0, Format.R32G32B32_Float, 0, 0),
                new InputElementDescription("COLOR", 0, Format.R32G32B32A32_Float, 12, 0)
            };

            // Pipeline state description
            var psoDesc = new GraphicsPipelineStateDescription
            {
                RootSignature = _rootSignature,
                VertexShader = vsBlob,
                PixelShader = psBlob,
                InputLayout = new InputLayoutDescription(inputElements),
                PrimitiveTopologyType = PrimitiveTopologyType.Line,
                RasterizerState = RasterizerDescription.CullNone,
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
                RenderTargetFormats = new[] { Format.R8G8B8A8_UNorm },
                SampleDescription = new SampleDescription(1, 0)
            };

            _pipelineState = device.CreateGraphicsPipelineState(psoDesc);

            return _pipelineState != null;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to create pipeline state: {ex.Message}");
            return false;
        }
    }

    public void Render(ID3D12GraphicsCommandList commandList, CameraData? camera, int viewportWidth, int viewportHeight)
    {
        if (_pipelineState == null || _rootSignature == null || _vertexBuffer == null || _indexBuffer == null)
            return;

        // Set pipeline
        commandList.SetPipelineState(_pipelineState);
        commandList.SetGraphicsRootSignature(_rootSignature);

        // Calculate aspect ratio
        float aspectRatio = viewportHeight > 0 
            ? (float)viewportWidth / viewportHeight 
            : 16.0f / 9.0f;

        // Camera data (use provided or default)
        var position = camera.HasValue ? camera.Value.Position : new System.Numerics.Vector3(0, 50, -50);
        var target = camera.HasValue ? camera.Value.Target : new System.Numerics.Vector3(0, 0, 0);
        var up = camera.HasValue ? camera.Value.Up : new System.Numerics.Vector3(0, 1, 0);
        float fov = camera?.Fov ?? MathF.PI / 4.0f;
        float nearPlane = camera?.NearPlane ?? 0.1f;
        float farPlane = camera?.FarPlane ?? 1000.0f;

        // Create view-projection matrix
        var view = Matrix4x4.CreateLookAt(position, target, up);
        var projection = Matrix4x4.CreatePerspectiveFieldOfView(fov, aspectRatio, nearPlane, farPlane);
        var viewProjection = view * projection;

        // Set constants
        unsafe
        {
            commandList.SetGraphicsRoot32BitConstants(0, 16, new IntPtr(&viewProjection), 0);
        }

        // Set geometry
        commandList.IASetPrimitiveTopology(PrimitiveTopology.LineList);
        commandList.IASetVertexBuffers(0, _vertexBufferView);
        commandList.IASetIndexBuffer(_indexBufferView);

        // Draw
        commandList.DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
    }

    public void Shutdown()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _pipelineState?.Dispose();
        _rootSignature?.Dispose();
        _indexBuffer?.Dispose();
        _vertexBuffer?.Dispose();

        _pipelineState = null;
        _rootSignature = null;
        _indexBuffer = null;
        _vertexBuffer = null;
        _vertices.Clear();
        _indices.Clear();

        _disposed = true;
    }
}
