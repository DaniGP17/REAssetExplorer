using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for RE Engine texture readers with common parsing logic.
/// </summary>
public abstract class TextureReaderBase : IAssetReader<TextureData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Texture;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<TextureData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var tex = new TextureData();

            ReadHeader(data, tex);
            ValidateTexture(tex, fileName);
            ReadMipHeaders(data, tex);
            ReadMipData(data, tex);

            return Result<TextureData>.Success(tex);
        }
        catch (Exception ex)
        {
            return Result<TextureData>.Failure($"Failed to read texture '{fileName}': {ex.Message}");
        }
    }

    protected virtual void ReadHeader(ReadOnlySpan<byte> data, TextureData tex)
    {
        tex.Magic = data.Slice(0, 4).ToArray();
        tex.Version = BitConverter.ToUInt32(data.Slice(0x04, 4));
        tex.Width = BitConverter.ToUInt16(data.Slice(0x08, 2));
        tex.Height = BitConverter.ToUInt16(data.Slice(0x0A, 2));
        tex.DepthAndType = BitConverter.ToUInt16(data.Slice(0x0C, 2));
        tex.MipInfo = BitConverter.ToUInt16(data.Slice(0x0E, 2));
        tex.Format = (TextureFormat)BitConverter.ToUInt32(data.Slice(0x10, 4));
        tex.TextureLayoutFlags = BitConverter.ToUInt64(data.Slice(0x14, 8));
        tex.StreamingFlags = BitConverter.ToUInt32(data.Slice(0x1C, 4));
        tex.DataSizeTotal = BitConverter.ToUInt32(data.Slice(0x20, 4));
        tex.TileMode = BitConverter.ToUInt16(data.Slice(0x24, 2));
        tex.Alignment = BitConverter.ToUInt16(data.Slice(0x26, 2));
    }

    protected virtual void ValidateTexture(TextureData tex, string fileName)
    {
        if (tex.Width == 0 || tex.Height == 0 || tex.Width > 16384 || tex.Height > 16384)
        {
            throw new InvalidDataException($"Invalid texture dimensions: {tex.Width}x{tex.Height}");
        }

        if (tex.MipsPerImage == 0 || tex.MipsPerImage > 16)
        {
            throw new InvalidDataException($"Invalid mipmap count: {tex.MipsPerImage}");
        }

        if (!Enum.IsDefined(typeof(TextureFormat), tex.Format))
        {
            throw new InvalidDataException($"Invalid texture format: {(uint)tex.Format}");
        }
    }

    protected virtual void ReadMipHeaders(ReadOnlySpan<byte> data, TextureData tex)
    {
        int totalMips = tex.NumImages * tex.MipsPerImage;
        tex.Mips = new MipHeader[totalMips];

        int mipBase = 0x28;
        for (int i = 0; i < totalMips; i++)
        {
            int off = mipBase + (i * 16);
            tex.Mips[i] = new MipHeader
            {
                Offset = BitConverter.ToUInt64(data.Slice(off, 8)),
                Size = BitConverter.ToUInt32(data.Slice(off + 12, 4)),
                Padding = BitConverter.ToUInt32(data.Slice(off + 8, 4))
            };
        }
    }
    
    protected virtual void ReadMipData(ReadOnlySpan<byte> data, TextureData tex)
    {
        if (tex.Mips.Length == 0)
        {
            tex.RawMipData = Array.Empty<byte>();
            return;
        }

        ulong minOffset = ulong.MaxValue;
        ulong maxEnd = 0;

        foreach (var mip in tex.Mips)
        {
            if (mip.Offset < minOffset)
                minOffset = mip.Offset;

            ulong end = mip.Offset + mip.Size;
            if (end > maxEnd)
                maxEnd = end;
        }

        ulong totalSize = maxEnd - minOffset;

        if (maxEnd <= (ulong)data.Length)
        {
            tex.RawMipData = data.Slice((int)minOffset, (int)totalSize).ToArray();
        }
        else
        {
            throw new InvalidDataException("Mip data out of bounds");
        }
    }
    
    public bool ResolveDependencies(TextureData asset) => true;
}
