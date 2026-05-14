using System;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;
using REAssetExplorer.Core.Assets.Models;
using SharpDX.Direct3D11;
using SharpDX.DXGI;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Representa una textura 2D en DirectX 11
/// </summary>
public class Texture : IDisposable
{
    public Texture2D? Texture2D { get; private set; }
    public ShaderResourceView? ShaderResourceView { get; private set; }
    public int Width { get; private set; }
    public int Height { get; private set; }

    public void CreateFromData(D3D11Device device, int width, int height, byte[] data, Format format = Format.R8G8B8A8_UNorm)
    {
        Width = width;
        Height = height;

        // Crear textura
        var textureDesc = new Texture2DDescription
        {
            Width = width,
            Height = height,
            MipLevels = 1,
            ArraySize = 1,
            Format = format,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };

        Texture2D = new Texture2D(device.Device, textureDesc);
        
        // Actualizar datos
        int rowPitch = width * 4; // 4 bytes por pixel (RGBA)
        device.Context.UpdateSubresource(data, Texture2D, 0, rowPitch);

        // Crear shader resource view
        var srvDesc = new ShaderResourceViewDescription
        {
            Format = format,
            Dimension = SharpDX.Direct3D.ShaderResourceViewDimension.Texture2D,
            Texture2D = new ShaderResourceViewDescription.Texture2DResource
            {
                MipLevels = 1,
                MostDetailedMip = 0
            }
        };

        ShaderResourceView = new ShaderResourceView(device.Device, Texture2D, srvDesc);
    }

    public void LoadFromTextureData(D3D11Device device, TextureData textureData)
    {
        if (textureData == null)
        {
            throw new ArgumentException("TextureData is null");
        }

        Width = textureData.Width;
        Height = textureData.Height;
        // Intentar cargar directamente la textura comprimida
        if (textureData.RawMipData != null && textureData.RawMipData.Length > 0)
        {
            try
            {
                CreateFromCompressedData(device, textureData);
                return;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"    ⚠ Failed to load compressed texture: {ex.Message}");
                Console.WriteLine($"    Falling back to solid color...");
            }
        }

        // Fallback: crear color sólido para debugging
        byte[] colorData = new byte[256 * 256 * 4]; // RGBA
        
        // Determinar color base según el nombre de la textura
        byte r = 200, g = 200, b = 200; // Gris por defecto
        
        string name = textureData.Name?.ToLower() ?? "";
        
        if (name.Contains("albm") || name.Contains("alb") || name.Contains("base"))
        {
            // Albedo/Base Color - Blanco
            r = 255; g = 255; b = 255;
        }
        else if (name.Contains("nrmr") || name.Contains("nrm") || name.Contains("normal"))
        {
            // Normal Map - Azul/Morado
            r = 128; g = 128; b = 255;
        }
        else if (name.Contains("rough") || name.Contains("rgh"))
        {
            // Roughness - Verde
            r = 128; g = 255; b = 128;
        }
        else if (name.Contains("met") || name.Contains("metal"))
        {
            // Metallic - Amarillo
            r = 255; g = 255; b = 128;
        }
        else if (name.Contains("ao") || name.Contains("occlusion"))
        {
            // Ambient Occlusion - Negro
            r = 64; g = 64; b = 64;
        }
        
        // Llenar la textura con el color sólido
        for (int i = 0; i < 256 * 256; i++)
        {
            colorData[i * 4 + 0] = r;
            colorData[i * 4 + 1] = g;
            colorData[i * 4 + 2] = b;
            colorData[i * 4 + 3] = 255; // Alpha
        }
        
        CreateFromData(device, 256, 256, colorData);
    }

    private void CreateFromCompressedData(D3D11Device device, TextureData textureData)
    {
        Width = textureData.Width;
        Height = textureData.Height;

        // Calcular las dimensiones reales basadas en el tamaño de los datos
        int blockSize = GetBlockSize(textureData.Format);
        
        // Asegurar que las dimensiones sean al menos 4x4 (mínimo para BC)
        int actualWidth = Math.Max(4, (int)textureData.Width);
        int actualHeight = Math.Max(4, (int)textureData.Height);
        
        // Calcular bloques esperados
        int blocksWide = Math.Max(1, (actualWidth + 3) / 4);
        int blocksHigh = Math.Max(1, (actualHeight + 3) / 4);
        int expectedBytes = blocksWide * blocksHigh * blockSize;
        
        // Si los datos son insuficientes, calcular las dimensiones reales del mip disponible
        if (textureData.RawMipData.Length < expectedBytes)
        {
            // Calcular qué nivel de mip realmente tenemos
            int mipLevel = 0;
            while (mipLevel < 16)
            {
                int mipWidth = Math.Max(4, textureData.Width >> mipLevel);
                int mipHeight = Math.Max(4, textureData.Height >> mipLevel);
                int mipBlocksWide = Math.Max(1, (mipWidth + 3) / 4);
                int mipBlocksHigh = Math.Max(1, (mipHeight + 3) / 4);
                int mipBytes = mipBlocksWide * mipBlocksHigh * blockSize;
                
                if (mipBytes <= textureData.RawMipData.Length)
                {
                    actualWidth = mipWidth;
                    actualHeight = mipHeight;
                    break;
                }
                
                mipLevel++;
            }
        }
        
        // Redondear al múltiplo de 4 más cercano para BC
        actualWidth = ((actualWidth + 3) / 4) * 4;
        actualHeight = ((actualHeight + 3) / 4) * 4;
        
        Width = actualWidth;
        Height = actualHeight;

        try
        {
            // Decodificar la textura BC a RGBA
            var bcFormat = ConvertToBCnFormat(textureData.Format);
            var decoder = new BcDecoder();
            
            var decoded2D = decoder.DecodeRaw2D(textureData.RawMipData, actualWidth, actualHeight, bcFormat);
            
            // Convertir a array de bytes RGBA
            byte[] pixels = new byte[actualWidth * actualHeight * 4];
            int index = 0;
            
            for (int y = 0; y < actualHeight; y++)
            {
                var rowSpan = decoded2D.Span.GetRowSpan(y);
                for (int x = 0; x < actualWidth; x++)
                {
                    var pixel = rowSpan[x];
                    // SharpDX usa RGBA, BCnEncoder devuelve BGRA
                    pixels[index++] = pixel.r;
                    pixels[index++] = pixel.g;
                    pixels[index++] = pixel.b;
                    pixels[index++] = pixel.a;
                }
            }

            // Crear textura con los datos decodificados
            CreateFromData(device, actualWidth, actualHeight, pixels, Format.R8G8B8A8_UNorm);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"    ⚠ BC decode failed: {ex.Message}");
            Console.WriteLine($"    Creating fallback texture...");
            
            // Fallback: crear textura sólida
            byte[] fallbackPixels = new byte[actualWidth * actualHeight * 4];
            for (int i = 0; i < fallbackPixels.Length; i += 4)
            {
                fallbackPixels[i] = 128;     // R
                fallbackPixels[i + 1] = 128; // G
                fallbackPixels[i + 2] = 128; // B
                fallbackPixels[i + 3] = 255; // A
            }
            CreateFromData(device, actualWidth, actualHeight, fallbackPixels, Format.R8G8B8A8_UNorm);
        }
    }

    private int GetBlockSize(TextureFormat format)
    {
        return format switch
        {
            TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb or
            TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm => 8,
            _ => 16
        };
    }

    private CompressionFormat ConvertToBCnFormat(TextureFormat format)
    {
        return format switch
        {
            TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb => CompressionFormat.Bc1,
            TextureFormat.Bc2Unorm or TextureFormat.Bc2UnormSrgb => CompressionFormat.Bc2,
            TextureFormat.Bc3Unorm or TextureFormat.Bc3UnormSrgb => CompressionFormat.Bc3,
            TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm => CompressionFormat.Bc4,
            TextureFormat.Bc5Unorm or TextureFormat.Bc5Snorm => CompressionFormat.Bc5,
            TextureFormat.Bc7Unorm or TextureFormat.Bc7UnormSrgb => CompressionFormat.Bc7,
            _ => CompressionFormat.Bc7
        };
    }

    public void Dispose()
    {
        ShaderResourceView?.Dispose();
        Texture2D?.Dispose();
    }
}
