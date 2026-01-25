using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Games.RE7.Assets;

/// <summary>
/// Reader for RE7 bank files (.bnk).
/// </summary>
public class RE7BankReader : BankReaderBase
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".bnk.2.stm",
        ".bnk.2",
        ".bnk"
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

        if (header[0] != 'B' || header[1] != 'K' || header[2] != 'H' || header[3] != 'D')
        {
            return false;
        }

        return true;
    }
}