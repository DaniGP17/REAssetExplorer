using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Assets.Resolvers;

public class SdfDependencyResolver : IDependencyResolver
{
    public AssetType AssetType => AssetType.Sdf;
    
    public bool ResolveDependencies(AssetData asset, DependencyResolutionContext context)
    {
        return true;
    }
    
    public AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null)
    {
        return null;
    }
}