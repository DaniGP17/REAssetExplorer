using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Assets.Resolvers;

/// <summary>
/// Dependency resolver for texture assets.
/// Textures typically don't have dependencies.
/// </summary>
public class TextureDependencyResolver : IDependencyResolver
{
    public AssetType AssetType => AssetType.Texture;
    
    public bool ResolveDependencies(AssetData asset, DependencyResolutionContext context)
    {
        // Textures don't have dependencies in most cases
        return true;
    }
    
    public AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null)
    {
        // Textures don't load dependencies
        return null;
    }
}
