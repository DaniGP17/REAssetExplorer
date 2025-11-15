using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Games.RE7.Assets;

namespace REAssetExplorer.Games.RE7;

/// <summary>
/// Game provider for Resident Evil 7 Biohazard.
/// </summary>
public class RE7Provider : IGameProvider
{
    private static readonly IPakReader _pakReader = new PakReaderV4();
    private static readonly IAssetReaderRegistry _assetReaders = CreateAssetReaders();
    
    /// <inheritdoc/>
    public string Id => "re7";

    /// <inheritdoc/>
    public string Name => "Resident Evil 7 Biohazard";

    /// <inheritdoc/>
    public uint SteamAppId => 418370;
    
    /// <inheritdoc/>
    public string[] PaksLocations => new[]
    {
        "re_chunk_000.pak",
        "dlc/re_dlc_stm_529930.pak",
        "dlc/re_dlc_stm_530610.pak",
        "dlc/re_dlc_stm_530611.pak",
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
        
        // Register RE7-specific asset readers
        registry.RegisterReader(new RE7TextureReader());
        registry.RegisterReader(new RE7MaterialReader());
        
        return registry;
    }
}