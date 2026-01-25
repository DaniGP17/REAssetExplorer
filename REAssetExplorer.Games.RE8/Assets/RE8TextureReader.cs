using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE8.Assets;

/// <summary>
/// Reader for RE8 texture files (.tex).
/// </summary>
public class RE8TextureReader : TextureReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".tex.30",
        ".tex"
    };

    /// <inheritdoc/>
    public override IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public override bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
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
}
