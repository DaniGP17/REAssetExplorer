using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE7.Assets;

/// <summary>
/// Reader for RE7 mesh files (.mesh).
/// </summary>
public class RE7MeshReader : MeshReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mesh.220128762",
        ".mesh"
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

        if (header[0] != 'M' || header[1] != 'E' || header[2] != 'S' || header[3] != 'H')
        {
            return false;
        }

        uint version = BitConverter.ToUInt32(header.Slice(4, 2));
        if (version != 21041600)
        {
            return false;
        }

        return true;
    }
}