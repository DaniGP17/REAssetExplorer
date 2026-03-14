using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Interface for resolving asset dependencies.
/// Each asset type can implement its own dependency resolution strategy.
/// </summary>
public interface IDependencyResolver
{
    /// <summary>
    /// The asset type this resolver handles.
    /// </summary>
    AssetType AssetType { get; }
    
    /// <summary>
    /// Resolves dependencies for an asset.
    /// </summary>
    /// <param name="asset">The asset to resolve dependencies for.</param>
    /// <param name="context">Context containing materials cache and other resources.</param>
    /// <returns>True if dependencies were resolved successfully.</returns>
    bool ResolveDependencies(AssetData asset, DependencyResolutionContext context);
    
    /// <summary>
    /// Loads a specific dependency.
    /// </summary>
    /// <param name="dependency">The dependency to load.</param>
    /// <param name="context">Context containing the loader and resources.</param>
    /// <param name="onProgress">Optional progress callback.</param>
    /// <returns>The loaded asset data, or null if failed.</returns>
    AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null);
}

/// <summary>
/// Context for resolving dependency paths.
/// </summary>
public class DependencyResolutionContext
{
    public MaterialsCache? MaterialsCache { get; set; }
    
    // Puedes agregar más recursos según necesites:
    // public TexturesCache? TexturesCache { get; set; }
    // public ShadersCache? ShadersCache { get; set; }
}

/// <summary>
/// Context for loading dependencies.
/// </summary>
public class DependencyLoadContext
{
    public IAssetLoader AssetLoader { get; }
    public MaterialsCache? MaterialsCache { get; set; }
    public IGameProvider? GameProvider { get; set; }
    
    /// <summary>
    /// List of PAK files to search for assets.
    /// </summary>
    public List<Pak.PakFile>? PakFiles { get; set; }
    
    public DependencyLoadContext(IAssetLoader assetLoader)
    {
        AssetLoader = assetLoader ?? throw new ArgumentNullException(nameof(assetLoader));
    }
}

/// <summary>
/// Interface for the asset loader to avoid circular dependencies.
/// </summary>
public interface IAssetLoader
{
    Result<T> LoadAsset<T>(string filePath, bool loadDependencies = true, Action<string>? onProgress = null) where T : AssetData;
    T? GetLoadedAsset<T>(string filePath) where T : AssetData;
    bool IsAssetLoaded(string filePath);
    
    /// <summary>
    /// Gets the game provider associated with this asset loader.
    /// </summary>
    IGameProvider GameProvider { get; }
}
