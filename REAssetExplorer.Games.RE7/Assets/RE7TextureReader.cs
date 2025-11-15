using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Games.RE7.Assets;

/// <summary>
/// Reader for RE7 material files (.mdf2).
/// </summary>
public class RE7TextureReader : IAssetReader<TextureData>
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".tex.35",
        ".tex"
    };

    /// <inheritdoc/>
    public IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Texture;

    /// <inheritdoc/>
    public bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        // The header should be at least 0x98 bytes for RE7 TEX files
        if (header.Length < 0x98)
        {
            return false;
        }

        if (header[0] != 'T' || header[1] != 'E' || header[2] != 'X' || header[3] != 0x00)
        {
            return false;
        }
        
        uint version = BitConverter.ToUInt32(header.Slice(4, 4));
        if (version != 35)
        {
            return false;
        }
        
        return true;
    }

    /// <inheritdoc/>
    public Result<TextureData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var name = Path.GetFileNameWithoutExtension(fileName);

            var tex = new TextureData();
            
            // ---- HEADER ----
            tex.Magic = data.Slice(0, 4).ToArray();

            tex.Version = BitConverter.ToUInt32(data.Slice(0x04, 4));
            tex.Width   = BitConverter.ToUInt16(data.Slice(0x08, 2));
            tex.Height  = BitConverter.ToUInt16(data.Slice(0x0A, 2));

            tex.DepthAndType = BitConverter.ToUInt16(data.Slice(0x0C, 2));
            tex.MipInfo      = BitConverter.ToUInt16(data.Slice(0x0E, 2));

            tex.Format = (TextureFormat)BitConverter.ToUInt32(data.Slice(0x10, 4));
            
            // Validate texture data
            if (tex.Width == 0 || tex.Height == 0 || tex.Width > 16384 || tex.Height > 16384)
            {
                return Result<TextureData>.Failure($"Invalid texture dimensions: {tex.Width}x{tex.Height}");
            }
            
            if (tex.MipsPerImage == 0 || tex.MipsPerImage > 16)
            {
                return Result<TextureData>.Failure($"Invalid mipmap count: {tex.MipsPerImage}");
            }
            
            if (!Enum.IsDefined(typeof(TextureFormat), tex.Format))
            {
                return Result<TextureData>.Failure($"Invalid texture format: {(uint)tex.Format}");
            }

            tex.TextureLayoutFlags = BitConverter.ToUInt64(data.Slice(0x14, 8));
            tex.StreamingFlags     = BitConverter.ToUInt32(data.Slice(0x1C, 4));
            tex.DataSizeTotal      = BitConverter.ToUInt32(data.Slice(0x20, 4));

            tex.TileMode = BitConverter.ToUInt16(data.Slice(0x24, 2));
            tex.Alignment = BitConverter.ToUInt16(data.Slice(0x26, 2));

            // ---- MIP HEADERS ----
            int totalMips    = tex.NumImages * tex.MipsPerImage;
            
            tex.Mips = new MipHeader[totalMips];

            int mipBase = 0x28;
            for (int i = 0; i < totalMips; i++)
            {
                int off = mipBase + (i * 16);

                tex.Mips[i] = new MipHeader
                {
                    Offset  = BitConverter.ToUInt64(data.Slice(off, 8)),
                    Size    = BitConverter.ToUInt32(data.Slice(off + 8, 4)),
                    Padding = BitConverter.ToUInt32(data.Slice(off + 12, 4))
                };
            }

            return Result<TextureData>.Success(tex);
        }
        catch (Exception ex)
        {
            return Result<TextureData>.Failure($"Failed to read RE7 texture '{fileName}': {ex.Message}");
        }
    }
}
