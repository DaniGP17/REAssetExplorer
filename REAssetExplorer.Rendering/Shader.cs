using System;
using System.IO;
using SharpDX;
using SharpDX.Direct3D11;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Clase simple para cargar y manejar shaders de vertex y pixel
/// </summary>
public class Shader : IDisposable
{
    public VertexShader? VertexShader { get; private set; }
    public PixelShader? PixelShader { get; private set; }
    public InputLayout? InputLayout { get; private set; }
    public SamplerState? SamplerState { get; private set; }
    
    private SharpDX.Direct3D11.Buffer? _matrixBuffer;

    /// <summary>
    /// Carga shaders desde archivos compilados (.cso)
    /// </summary>
    public void LoadFromCompiledFiles(D3D11Device device, string vertexShaderPath, string pixelShaderPath)
    {
        if (!File.Exists(vertexShaderPath))
            throw new FileNotFoundException($"Vertex shader no encontrado: {vertexShaderPath}");
        
        if (!File.Exists(pixelShaderPath))
            throw new FileNotFoundException($"Pixel shader no encontrado: {pixelShaderPath}");

        var vsBytes = File.ReadAllBytes(vertexShaderPath);
        var psBytes = File.ReadAllBytes(pixelShaderPath);

        LoadFromBytes(device, vsBytes, psBytes);
    }
    
    /// <summary>
    /// Carga shaders desde bytes con input layout opcional
    /// </summary>
    public void LoadFromBytes(D3D11Device device, byte[] vertexShaderBytes, byte[] pixelShaderBytes, Core.Assets.Models.InputLayoutDesc? inputLayoutDesc = null)
    {
        if (vertexShaderBytes == null || vertexShaderBytes.Length == 0)
            throw new ArgumentException("Los bytes del vertex shader están vacíos o son null", nameof(vertexShaderBytes));
        
        if (pixelShaderBytes == null || pixelShaderBytes.Length == 0)
            throw new ArgumentException("Los bytes del pixel shader están vacíos o son null", nameof(pixelShaderBytes));
        
        // Crear vertex shader
        VertexShader = new VertexShader(device.Device, vertexShaderBytes);
        
        // Crear pixel shader
        PixelShader = new PixelShader(device.Device, pixelShaderBytes);
        
        // Crear input layout si se proporciona
        if (inputLayoutDesc.HasValue && inputLayoutDesc.Value.Elements != null && inputLayoutDesc.Value.Elements.Count > 0)
        {
            try
            {
                var dxElements = ConvertInputLayout(inputLayoutDesc.Value);
                
                using (var signature = SharpDX.D3DCompiler.ShaderSignature.GetInputSignature(vertexShaderBytes))
                {
                    InputLayout = new InputLayout(device.Device, signature, dxElements);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Shader.LoadFromBytes] ERROR creating InputLayout: {ex.GetType().Name}: {ex.Message}");
                throw;
            }
        }
        
        // Input layout por defecto (básico)
        /*var inputElements = new[]
        {
            new InputElement("POSITION", 0, SharpDX.DXGI.Format.R32G32B32_Float, 0, 0),
            new InputElement("NORMAL", 0, SharpDX.DXGI.Format.R32G32B32_Float, 12, 0),
            new InputElement("TEXCOORD", 0, SharpDX.DXGI.Format.R32G32_Float, 24, 0)
        };
        
        using (var signature = SharpDX.D3DCompiler.ShaderSignature.GetInputSignature(vertexShaderBytes))
        {
            InputLayout = new InputLayout(device.Device, signature, inputElements);
        }
        
        // Crear constant buffer para matrices (World, View, Projection)
        _matrixBuffer = new SharpDX.Direct3D11.Buffer(device.Device, new BufferDescription
        {
            Usage = ResourceUsage.Default,
            SizeInBytes = Utilities.SizeOf<MatrixBuffer>(),
            BindFlags = BindFlags.ConstantBuffer,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None,
            StructureByteStride = 0
        });
        
        // Crear sampler state para texturas
        SamplerState = new SamplerState(device.Device, new SamplerStateDescription
        {
            Filter = Filter.MinMagMipLinear,
            AddressU = TextureAddressMode.Wrap,
            AddressV = TextureAddressMode.Wrap,
            AddressW = TextureAddressMode.Wrap,
            MipLodBias = 0,
            MaximumAnisotropy = 1,
            ComparisonFunction = Comparison.Always,
            BorderColor = new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0),
            MinimumLod = 0,
            MaximumLod = float.MaxValue
        });*/
    }
    
    /// <summary>
    /// Carga shaders predefinidos por nombre
    /// </summary>
    public void LoadShaders(D3D11Device device, string vertexShaderName, string pixelShaderName)
    {
        var shadersFolder = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Shaders");
        
        if (!Directory.Exists(shadersFolder))
            throw new DirectoryNotFoundException($"Carpeta de shaders no encontrada: {shadersFolder}");

        var vsPath = Path.Combine(shadersFolder, Path.ChangeExtension(vertexShaderName, ".cso"));
        var psPath = Path.Combine(shadersFolder, Path.ChangeExtension(pixelShaderName, ".cso"));

        LoadFromCompiledFiles(device, vsPath, psPath);
    }

    /// <summary>
    /// Establece las matrices World, View y Projection
    /// </summary>
    public void SetMatrices(D3D11Device device, System.Numerics.Matrix4x4 world, System.Numerics.Matrix4x4 view, System.Numerics.Matrix4x4 projection)
    {
        if (_matrixBuffer == null)
            return;

        var matrixBuffer = new MatrixBuffer
        {
            World = ToSharpDXMatrix(world),
            View = ToSharpDXMatrix(view),
            Projection = ToSharpDXMatrix(projection)
        };

        // Transponer para DirectX
        matrixBuffer.World.Transpose();
        matrixBuffer.View.Transpose();
        matrixBuffer.Projection.Transpose();

        device.Context.UpdateSubresource(ref matrixBuffer, _matrixBuffer);
        device.Context.VertexShader.SetConstantBuffer(0, _matrixBuffer);
    }

    /// <summary>
    /// Convierte InputLayoutDesc del RE Engine a InputElement[] de DirectX
    /// </summary>
    private InputElement[] ConvertInputLayout(Core.Assets.Models.InputLayoutDesc layoutDesc)
    {
        var elements = new InputElement[layoutDesc.Elements.Count];
        
        Console.WriteLine($"[Shader.ConvertInputLayout] Converting {layoutDesc.Elements.Count} input elements (INTERLEAVED mode):");
        
        // Calculate offsets for INTERLEAVED layout (all in slot 0)
        // IMPORTANT: Must match VertexPosition C# struct layout!
        int currentOffset = 0;
        
        for (int i = 0; i < layoutDesc.Elements.Count; i++)
        {
            var elem = layoutDesc.Elements[i];
            var semantic = GetSemanticName(elem.SemanticType);
            
            // Override format to match VertexPosition C# struct (not compressed RE Engine format)
            var format = GetInterleavedFormat(elem.SemanticType, elem.SemanticIndex);
            
            elements[i] = new InputElement(
                semantic,
                elem.SemanticIndex,
                format,
                currentOffset,     // Use cumulative offset for interleaved layout
                0,                 // All in slot 0 for interleaved
                elem.IsInstanceData ? InputClassification.PerInstanceData : InputClassification.PerVertexData,
                elem.IsInstanceData ? 1 : 0
            );
            
            // Calculate size of this element for next offset
            int elementSize = GetFormatSize(format);
            Console.WriteLine($"  [{i}] {semantic}{elem.SemanticIndex}: Format={format}, Slot=0, Offset={currentOffset}, Size={elementSize}");
            currentOffset += elementSize;
        }
        
        Console.WriteLine($"[Shader.ConvertInputLayout] Total vertex stride: {currentOffset} bytes (expected: {VertexPosition.SizeInBytes})");
        
        return elements;
    }
    
    private SharpDX.DXGI.Format GetInterleavedFormat(Core.Assets.Models.SemanticType semantic, int index)
    {
        // These formats MUST match the VertexPosition C# struct:
        // Position: Vector3 (12 bytes)
        // Normal: Vector4 (16 bytes)
        // Tangent: Vector4 (16 bytes)
        // TexCoord: Vector2 (8 bytes)
        // TexCoord2: Vector2 (8 bytes)
        return semantic switch
        {
            Core.Assets.Models.SemanticType.Position => SharpDX.DXGI.Format.R32G32B32_Float,
            Core.Assets.Models.SemanticType.Normal => SharpDX.DXGI.Format.R32G32B32A32_Float,
            Core.Assets.Models.SemanticType.Tangent => SharpDX.DXGI.Format.R32G32B32A32_Float,
            Core.Assets.Models.SemanticType.Texcoord => SharpDX.DXGI.Format.R32G32_Float,
            _ => SharpDX.DXGI.Format.R32G32B32A32_Float
        };
    }
    
    private int GetFormatSize(SharpDX.DXGI.Format format)
    {
        return format switch
        {
            SharpDX.DXGI.Format.R32G32B32A32_Float => 16,
            SharpDX.DXGI.Format.R32G32B32_Float => 12,
            SharpDX.DXGI.Format.R32G32_Float => 8,
            SharpDX.DXGI.Format.R32_Float => 4,
            SharpDX.DXGI.Format.R16G16B16A16_Float => 8,
            SharpDX.DXGI.Format.R16G16_Float => 4,
            SharpDX.DXGI.Format.R8G8B8A8_SNorm => 4,
            SharpDX.DXGI.Format.R8G8B8A8_UNorm => 4,
            SharpDX.DXGI.Format.R16G16B16A16_SNorm => 8,
            SharpDX.DXGI.Format.R16G16_SNorm => 4,
            SharpDX.DXGI.Format.R10G10B10A2_UNorm => 4,
            SharpDX.DXGI.Format.R32G32B32A32_UInt => 16,
            SharpDX.DXGI.Format.R32G32B32_UInt => 12,
            SharpDX.DXGI.Format.R32G32_UInt => 8,
            SharpDX.DXGI.Format.R32_UInt => 4,
            SharpDX.DXGI.Format.R16G16B16A16_UInt => 8,
            SharpDX.DXGI.Format.R16G16_UInt => 4,
            SharpDX.DXGI.Format.R8G8B8A8_UInt => 4,
            _ => 16 // Default to 16 bytes
        };
    }
    
    private string GetSemanticName(Core.Assets.Models.SemanticType semantic)
    {
        return semantic switch
        {
            Core.Assets.Models.SemanticType.Position => "POSITION",
            Core.Assets.Models.SemanticType.Normal => "NORMAL",
            Core.Assets.Models.SemanticType.Binormal => "BINORMAL",
            Core.Assets.Models.SemanticType.Tangent => "TANGENT",
            Core.Assets.Models.SemanticType.Texcoord => "TEXCOORD",
            Core.Assets.Models.SemanticType.Index => "BLENDINDICES",
            Core.Assets.Models.SemanticType.Weight => "BLENDWEIGHT",
            Core.Assets.Models.SemanticType.Color => "COLOR",
            Core.Assets.Models.SemanticType.VertexId => "SV_VertexID",
            Core.Assets.Models.SemanticType.InstanceId => "SV_InstanceID",
            Core.Assets.Models.SemanticType.UniqueUv => "UNIQUEUV",
            _ => "TEXCOORD"
        };
    }
    
    private SharpDX.DXGI.Format GetDXGIFormat(byte format)
    {
        // RE Engine format IDs -> DXGI_FORMAT
        return format switch
        {
            0x00 => SharpDX.DXGI.Format.R32G32B32A32_Float,
            0x01 => SharpDX.DXGI.Format.R32G32B32_Float,
            0x02 => SharpDX.DXGI.Format.R32G32B32_Float,      // Posición 3D (no 2D!)
            0x03 => SharpDX.DXGI.Format.R32_Float,
            0x04 => SharpDX.DXGI.Format.R16G16_Float,         // UVs 16-bit
            0x09 => SharpDX.DXGI.Format.R8G8B8A8_SNorm,       // Normal/Tangent comprimidos
            0x0D => SharpDX.DXGI.Format.R16G16B16A16_Float,
            0x0E => SharpDX.DXGI.Format.R16G16_Float,
            0x10 => SharpDX.DXGI.Format.R8G8B8A8_UNorm,
            0x15 => SharpDX.DXGI.Format.R16G16B16A16_SNorm,
            0x17 => SharpDX.DXGI.Format.R16G16_SNorm,
            0x19 => SharpDX.DXGI.Format.R8G8B8A8_SNorm,
            0x25 => SharpDX.DXGI.Format.R10G10B10A2_UNorm,
            0x2B => SharpDX.DXGI.Format.R32G32B32A32_UInt,
            0x2C => SharpDX.DXGI.Format.R32G32B32_UInt,
            0x2D => SharpDX.DXGI.Format.R32G32_UInt,
            0x2E => SharpDX.DXGI.Format.R32_UInt,
            0x37 => SharpDX.DXGI.Format.R16G16B16A16_UInt,
            0x39 => SharpDX.DXGI.Format.R16G16_UInt,
            0x3B => SharpDX.DXGI.Format.R8G8B8A8_UInt,
            _ => SharpDX.DXGI.Format.R32G32B32A32_Float // Default
        };
    }
    
    /// <summary>
    /// Establece texturas para el pixel shader
    /// </summary>
    public void SetTextures(D3D11Device device, params Texture[] textures)
    {
        var srvs = new ShaderResourceView?[textures.Length];
        
        for (int i = 0; i < textures.Length; i++)
        {
            srvs[i] = textures[i]?.ShaderResourceView ?? device.DummySRV;
        }

        device.Context.PixelShader.SetShaderResources(0, srvs);
        
        if (SamplerState != null)
        {
            device.Context.PixelShader.SetSampler(0, SamplerState);
        }
    }

    private static Matrix ToSharpDXMatrix(System.Numerics.Matrix4x4 matrix)
    {
        return new Matrix(
            matrix.M11, matrix.M12, matrix.M13, matrix.M14,
            matrix.M21, matrix.M22, matrix.M23, matrix.M24,
            matrix.M31, matrix.M32, matrix.M33, matrix.M34,
            matrix.M41, matrix.M42, matrix.M43, matrix.M44);
    }

    public void Dispose()
    {
        SamplerState?.Dispose();
        _matrixBuffer?.Dispose();
        InputLayout?.Dispose();
        VertexShader?.Dispose();
        PixelShader?.Dispose();
    }

    private struct MatrixBuffer
    {
        public Matrix World;
        public Matrix View;
        public Matrix Projection;
    }
}
