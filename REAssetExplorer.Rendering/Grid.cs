using System;
using System.Numerics;
using SharpDX.Direct3D11;
using SharpDX.DXGI;
using Buffer = SharpDX.Direct3D11.Buffer;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Grid de referencia para visualización 3D
/// </summary>
public class Grid : IDisposable
{
    private Buffer? _vertexBuffer;
    private Buffer? _constantBuffer;
    private VertexShader? _vertexShader;
    private PixelShader? _pixelShader;
    private InputLayout? _inputLayout;
    private int _vertexCount;
    
    public Grid(D3D11Device device, int size = 20, float spacing = 1.0f)
    {
        CreateGrid(device, size, spacing);
        CreateShaders(device);
    }
    
    private void CreateGrid(D3D11Device device, int size, float spacing)
    {
        // Crear vértices para las líneas del grid
        var vertices = new System.Collections.Generic.List<Vector3>();
        
        float halfSize = size * spacing / 2.0f;
        
        // Líneas paralelas al eje X (van en dirección Z)
        for (int i = -size/2; i <= size/2; i++)
        {
            float z = i * spacing;
            vertices.Add(new Vector3(-halfSize, 0, z));
            vertices.Add(new Vector3(halfSize, 0, z));
        }
        
        // Líneas paralelas al eje Z (van en dirección X)
        for (int i = -size/2; i <= size/2; i++)
        {
            float x = i * spacing;
            vertices.Add(new Vector3(x, 0, -halfSize));
            vertices.Add(new Vector3(x, 0, halfSize));
        }
        
        _vertexCount = vertices.Count;
        
        // Convertir a array de floats
        float[] vertexData = new float[vertices.Count * 3];
        for (int i = 0; i < vertices.Count; i++)
        {
            vertexData[i * 3 + 0] = vertices[i].X;
            vertexData[i * 3 + 1] = vertices[i].Y;
            vertexData[i * 3 + 2] = vertices[i].Z;
        }
        
        _vertexBuffer = Buffer.Create(device.Device, BindFlags.VertexBuffer, vertexData);
        
        Console.WriteLine($"[Grid] Created with {_vertexCount} vertices ({_vertexCount/2} lines)");
    }
    
    private void CreateShaders(D3D11Device device)
    {
        // Vertex shader simple
        string vsCode = @"
cbuffer ConstantBuffer : register(b0)
{
    matrix WorldViewProjection;
};

struct VS_INPUT
{
    float3 Position : POSITION;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.Position = mul(float4(input.Position, 1.0), WorldViewProjection);
    return output;
}
";
        
        // Pixel shader simple (grid blanco)
        string psCode = @"
struct PS_INPUT
{
    float4 Position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target
{
    return float4(0.7, 0.7, 0.7, 1.0); // Gris claro
}
";
        
        var vsCompiled = SharpDX.D3DCompiler.ShaderBytecode.Compile(vsCode, "main", "vs_5_0");
        var psCompiled = SharpDX.D3DCompiler.ShaderBytecode.Compile(psCode, "main", "ps_5_0");
        
        _vertexShader = new VertexShader(device.Device, vsCompiled);
        _pixelShader = new PixelShader(device.Device, psCompiled);
        
        // InputLayout simple
        var inputElements = new[]
        {
            new InputElement("POSITION", 0, Format.R32G32B32_Float, 0, 0)
        };
        
        _inputLayout = new InputLayout(device.Device, vsCompiled, inputElements);
        
        // Constant buffer para WVP matrix
        _constantBuffer = new Buffer(
            device.Device,
            SharpDX.Utilities.SizeOf<SharpDX.Matrix>(),
            ResourceUsage.Dynamic,
            BindFlags.ConstantBuffer,
            CpuAccessFlags.Write,
            ResourceOptionFlags.None,
            0
        );
        
        vsCompiled.Dispose();
        psCompiled.Dispose();
    }
    
    public void Draw(D3D11Device device, Matrix4x4 view, Matrix4x4 projection)
    {
        if (_vertexBuffer == null || _vertexShader == null || _pixelShader == null || 
            _inputLayout == null || _constantBuffer == null)
            return;
        
        var world = Matrix4x4.Identity;
        var wvp = world * view * projection;
        
        // Convertir y transponer para DirectX
        var wvpDX = new SharpDX.Matrix(
            wvp.M11, wvp.M12, wvp.M13, wvp.M14,
            wvp.M21, wvp.M22, wvp.M23, wvp.M24,
            wvp.M31, wvp.M32, wvp.M33, wvp.M34,
            wvp.M41, wvp.M42, wvp.M43, wvp.M44
        );
        wvpDX.Transpose();
        
        // Actualizar constant buffer
        var dataBox = device.Context.MapSubresource(
            _constantBuffer,
            0,
            MapMode.WriteDiscard,
            SharpDX.Direct3D11.MapFlags.None
        );
        SharpDX.Utilities.Write(dataBox.DataPointer, ref wvpDX);
        device.Context.UnmapSubresource(_constantBuffer, 0);
        
        // Asegurarse de que estamos dibujando al backbuffer con depth
        device.Context.OutputMerger.SetRenderTargets(device.DepthStencilView, device.RenderTargetView);
        
        // Configurar pipeline
        device.Context.VertexShader.Set(_vertexShader);
        device.Context.PixelShader.Set(_pixelShader);
        device.Context.InputAssembler.InputLayout = _inputLayout;
        device.Context.InputAssembler.PrimitiveTopology = SharpDX.Direct3D.PrimitiveTopology.LineList;
        
        device.Context.VertexShader.SetConstantBuffer(0, _constantBuffer);
        
        // Bindear vertex buffer
        device.Context.InputAssembler.SetVertexBuffers(
            0,
            new VertexBufferBinding(_vertexBuffer, sizeof(float) * 3, 0)
        );
        
        // Dibujar
        device.Context.Draw(_vertexCount, 0);
        
        // Restaurar topología a TriangleList
        device.Context.InputAssembler.PrimitiveTopology = SharpDX.Direct3D.PrimitiveTopology.TriangleList;
    }
    
    public void Dispose()
    {
        _vertexBuffer?.Dispose();
        _constantBuffer?.Dispose();
        _vertexShader?.Dispose();
        _pixelShader?.Dispose();
        _inputLayout?.Dispose();
    }
}
