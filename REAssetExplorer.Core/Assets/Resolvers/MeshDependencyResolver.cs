using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Assets.Resolvers;

/// <summary>
/// Dependency resolver for mesh assets.
/// </summary>
public class MeshDependencyResolver : IDependencyResolver
{
    public AssetType AssetType => AssetType.Mesh;
    
    public bool ResolveDependencies(AssetData asset, DependencyResolutionContext context)
    {
        if (asset is not MeshData meshData)
            return false;
        
        try
        {
            meshData.Dependencies.Clear();
            
            // Add material dependencies
            foreach (var materialName in meshData.Materials.Values.Distinct())
            {
                if (string.IsNullOrWhiteSpace(materialName) || materialName == "\0")
                    continue;
                
                var dependency = new AssetDependency(
                    AssetType.Material,
                    filePath: "",
                    name: materialName,
                    isRequired: true,
                    purpose: "Material"
                );

                meshData.Dependencies.Add(dependency);
            }
            
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error resolving mesh dependencies: {ex.Message}");
            return false;
        }
    }
    
    public AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null)
    {
        if (dependency.AssetType != AssetType.Material)
            return null;
        
        if (string.IsNullOrEmpty(dependency.FilePath))
        {
            onProgress?.Invoke($"Cannot load material '{dependency.Name}': path not resolved");
            return null;
        }
        
        var result = context.AssetLoader.LoadAsset<MaterialData>(
            dependency.FilePath, 
            loadDependencies: true, // Materials can have texture dependencies
            onProgress
        );
        
        return result.IsSuccess ? result.Value : null;
    }
}
