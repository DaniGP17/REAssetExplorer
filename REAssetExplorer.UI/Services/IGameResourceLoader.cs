using REAssetExplorer.Core.Games;

namespace REAssetExplorer.UI.Services;

/// <summary>
/// Interface for loading and initializing game resources.
/// </summary>
public interface IGameResourceLoader
{
    /// <summary>
    /// Gets the name of this resource loader for display purposes.
    /// </summary>
    string Name { get; }
    
    /// <summary>
    /// Gets the priority order for loading (lower values load first).
    /// </summary>
    int Priority { get; }
    
    /// <summary>
    /// Loads the resource for the specified game.
    /// </summary>
    /// <param name="gameProvider">The game provider instance.</param>
    /// <param name="onProgress">Optional progress callback.</param>
    /// <returns>True if loading was successful, false otherwise.</returns>
    Task<bool> LoadAsync(IGameProvider gameProvider, Action<string>? onProgress = null);
}
