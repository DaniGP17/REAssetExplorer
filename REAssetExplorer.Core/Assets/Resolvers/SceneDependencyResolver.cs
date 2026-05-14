using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Resolvers;

/// <summary>
/// Dependency resolver for mesh assets.
/// </summary>
public class SceneDependencyResolver : IDependencyResolver
{
    public AssetType AssetType => AssetType.Scene;
    
    public bool ResolveDependencies(AssetData asset, DependencyResolutionContext context)
    {
        if (asset is not SceneData sceneData)
            return false;
        
        try
        {
            sceneData.Dependencies.Clear();

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (var path in sceneData.ExternalScenePaths)
                AddSceneDep(sceneData, path, "Scene", seen);

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error resolving scene dependencies: {ex.Message}");
            return false;
        }
    }
    
    private static void AddSceneDep(SceneData sceneData, string path, string purpose, HashSet<string> seen)
    {
        if (!seen.Add(path)) return;
        sceneData.Dependencies.Add(new AssetDependency(
            AssetType.Scene,
            filePath: path,
            name: path.Split('/').LastOrDefault() ?? path,
            isRequired: false,
            purpose: purpose
        ));
    }

    public AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null)
    {
        if (dependency.AssetType != AssetType.Scene)
            return null;
        
        if (string.IsNullOrEmpty(dependency.FilePath))
        {
            onProgress?.Invoke($"Cannot load scene '{dependency.Name}': path not resolved");
            return null;
        }
        
        var result = context.AssetLoader.LoadAsset<SceneData>(
            dependency.FilePath, 
            loadDependencies: true, // Materials can have texture dependencies
            onProgress
        );
        
        return result.IsSuccess ? result.Value : null;
    }
}
