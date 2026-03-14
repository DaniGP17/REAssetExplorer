using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE8.Assets;

/// <summary>
/// Reader for RE8 shader definition files (.mmtr).
/// </summary>
public class RE8SdfReader : SdfReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mmtr.2102188797",
        ".mmtr"
    };

    /// <inheritdoc/>
    public override IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public override bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        if (header[0] != 'S' || header[1] != 'D' || header[2] != 'F' || header[3] != 0x00)
        {
            return false;
        }
        
        return true;
    }
}
