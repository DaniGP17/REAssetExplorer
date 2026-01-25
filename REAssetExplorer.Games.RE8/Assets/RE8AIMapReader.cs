using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE8.Assets;

/// <summary>
/// Reader for RE8 AI map files (.aimap).
/// </summary>
public class RE8AIMapReader : AIMapReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".aimap.41",
        ".aimap"
    };

    /// <inheritdoc/>
    public override IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public override bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        if (header.Length < 0x80)
        {
            return false;
        }

        if (header[0] != 'A' || header[1] != 'I' || header[2] != 'M' || header[3] != 'P')
        {
            return false;
        }

        return true;
    }

    protected override void SkipContentGroup(BinaryReader br, string typeName, int count)
    {
        if (typeName.EndsWith("ContentGroupMapBoundary"))
        {
            for (int i = 0; i < count; i++)
            {
                br.ReadBytes(16);
                br.ReadBytes(16);
                br.ReadBytes(12);
                br.ReadBytes(12);
            }

            return;
        }
        
        base.SkipContentGroup(br, typeName, count);
    }
}
