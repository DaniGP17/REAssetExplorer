namespace REAssetExplorer.Core.Rsz;

/// <summary>
/// Global accessor for the currently loaded RSZ type database.
/// </summary>
public static class RszRegistry
{
    public static RszTypeDb? Current { get; set; }
}
