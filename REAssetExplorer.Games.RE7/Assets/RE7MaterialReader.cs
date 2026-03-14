using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE7.Assets;

/// <summary>
/// Reader for RE7 material files (.mdf2).
/// </summary>
public class RE7MaterialReader : MaterialReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mdf2.21",
        ".mdf2"
    };

    /// <inheritdoc/>
    public override IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public override bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        // The header should be at least 0x10 bytes for RE7 Material files
        if (header.Length < 0x10)
        {
            return false;
        }

        if (header[0] != 'M' || header[1] != 'D' || header[2] != 'F' || header[3] != 0x00)
        {
            return false;
        }
        
        uint version = BitConverter.ToUInt32(header.Slice(4, 2));
        if (version != 1)
        {
            return false;
        }
        
        return true;
    }
}
