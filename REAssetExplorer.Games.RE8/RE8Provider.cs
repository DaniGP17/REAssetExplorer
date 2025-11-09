using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Games.RE8.Assets;

namespace REAssetExplorer.Games.RE8;

/// <summary>
/// Game provider for Resident Evil Village (RE8).
/// </summary>
public class RE8Provider : IGameProvider
{
    private static readonly IPakReader _pakReader = new PakReaderV4();
    private static readonly IAssetReaderRegistry _assetReaders = CreateAssetReaders();
    
    /// <inheritdoc/>
    public string Id => "re8";

    /// <inheritdoc/>
    public string Name => "Resident Evil Village";

    /// <inheritdoc/>
    public uint SteamAppId => 1196590;
    
    /// <inheritdoc/>
    public string[] PaksLocations => new[]
    {
        "re_chunk_000.pak",
    };
    
    /// <inheritdoc/>
    public IPakReader PakReader => _pakReader;
    
    /// <inheritdoc/>
    public IAssetReaderRegistry AssetReaders => _assetReaders;
    
    /// <inheritdoc/>
    public string GameDirectory { get; set; } = string.Empty;

    private static IAssetReaderRegistry CreateAssetReaders()
    {
        var registry = new AssetReaderRegistry();
        
        // Register RE8-specific asset readers
        registry.RegisterReader(new RE8MaterialReader());
        
        return registry;
    }
}