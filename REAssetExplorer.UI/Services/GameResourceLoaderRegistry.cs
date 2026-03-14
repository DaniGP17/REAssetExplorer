using REAssetExplorer.Core.Games;

namespace REAssetExplorer.UI.Services;

/// <summary>
/// Registry for managing game resource loaders.
/// </summary>
public class GameResourceLoaderRegistry
{
    private readonly List<IGameResourceLoader> _loaders = new();
    
    public void Register(IGameResourceLoader loader)
    {
        ArgumentNullException.ThrowIfNull(loader);
        
        if (!_loaders.Contains(loader))
        {
            _loaders.Add(loader);
        }
    }
    
    public void Unregister(IGameResourceLoader loader)
    {
        _loaders.Remove(loader);
    }
    
    public async Task<bool> LoadAllAsync(IGameProvider gameProvider, Action<string>? onProgress = null)
    {
        var sortedLoaders = _loaders.OrderBy(l => l.Priority).ToList();
        
        foreach (var loader in sortedLoaders)
        {
            onProgress?.Invoke($"Loading {loader.Name}...");
            
            var success = await loader.LoadAsync(gameProvider, onProgress);
            
            if (!success)
            {
                onProgress?.Invoke($"Failed to load {loader.Name}");
                return false;
            }
        }
        
        return true;
    }
    
    public void Clear()
    {
        _loaders.Clear();
    }
}
