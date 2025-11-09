using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.Core.Games;

/// <summary>
/// Interface for game-specific providers that define how to locate and read game files.
/// </summary>
public interface IGameProvider
{
    /// <summary>
    /// Unique identifier for the game.
    /// </summary>
    string Id { get; }
    
    /// <summary>
    /// Display name of the game.
    /// </summary>
    string Name { get; }
    
    /// <summary>
    /// Steam App ID for locating the game via Steam.
    /// </summary>
    uint SteamAppId { get; }
    
    /// <summary>
    /// Relative paths to PAK files within the game directory.
    /// </summary>
    string[] PaksLocations { get; }
    
    /// <summary>
    /// Reader implementation for this game's PAK format.
    /// </summary>
    IPakReader PakReader { get; }
    
    /// <summary>
    /// Registry of asset readers specific to this game.
    /// </summary>
    IAssetReaderRegistry AssetReaders { get; }
    
    /// <summary>
    /// The root directory where the game is installed.
    /// </summary>
    string GameDirectory { get; set; }
}
