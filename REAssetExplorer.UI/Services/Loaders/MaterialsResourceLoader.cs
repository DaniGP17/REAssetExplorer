using REAssetExplorer.Core.Games;

namespace REAssetExplorer.UI.Services.Loaders;

/// <summary>
/// Materials are now loaded automatically when PAK files are loaded.
/// This loader is kept for backwards compatibility but does nothing.
/// </summary>
public class MaterialsResourceLoader : IGameResourceLoader
{
    public string Name => "Materials";
    
    public int Priority => 10;
    
    public async Task<bool> LoadAsync(IGameProvider gameProvider, Action<string>? onProgress = null)
    {
        // Materials are now loaded in PakLoader during PAK file loading
        onProgress?.Invoke("Materials already loaded with PAK files");
        return await Task.FromResult(true);
    }
}
