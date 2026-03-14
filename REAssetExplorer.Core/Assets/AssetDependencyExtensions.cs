using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Extension methods for working with asset dependencies.
/// </summary>
public static class AssetDependencyExtensions
{
    /// <summary>
    /// Resolves material file paths using the materials cache.
    /// </summary>
    /// <param name="asset">The asset with dependencies to resolve.</param>
    /// <param name="materialsCache">The materials cache to use for resolution.</param>
    /// <returns>The number of dependencies that were successfully resolved.</returns>
    public static int ResolveMaterialPaths(this AssetData asset, MaterialsCache materialsCache)
    {
        ArgumentNullException.ThrowIfNull(asset);
        ArgumentNullException.ThrowIfNull(materialsCache);
        
        int resolvedCount = 0;
        
        foreach (var dependency in asset.Dependencies)
        {
            // Skip if already has a file path
            if (!string.IsNullOrEmpty(dependency.FilePath))
            {
                continue;
            }
            
            // Skip if doesn't need path resolution
            if (!dependency.Metadata.TryGetValue("NeedsPathResolution", out var needsResolution) 
                || !(bool)needsResolution)
            {
                continue;
            }
            
            // Try to resolve the material path
            if (dependency.AssetType == AssetType.Material 
                && dependency.Metadata.TryGetValue("MaterialName", out var materialNameObj)
                && materialNameObj is string materialName)
            {
                if (materialsCache.TryGetMaterial(materialName, out var materialPath) 
                    && !string.IsNullOrEmpty(materialPath))
                {
                    dependency.FilePath = materialPath;
                    dependency.Metadata["NeedsPathResolution"] = false;
                    resolvedCount++;
                }
            }
        }
        
        return resolvedCount;
    }
    
    /// <summary>
    /// Gets all dependencies of a specific type.
    /// </summary>
    /// <param name="asset">The asset to get dependencies from.</param>
    /// <param name="assetType">The type of dependencies to get.</param>
    /// <returns>Collection of dependencies of the specified type.</returns>
    public static IEnumerable<AssetDependency> GetDependenciesByType(this AssetData asset, AssetType assetType)
    {
        return asset.Dependencies.Where(d => d.AssetType == assetType);
    }
    
    /// <summary>
    /// Gets all required dependencies.
    /// </summary>
    /// <param name="asset">The asset to get dependencies from.</param>
    /// <returns>Collection of required dependencies.</returns>
    public static IEnumerable<AssetDependency> GetRequiredDependencies(this AssetData asset)
    {
        return asset.Dependencies.Where(d => d.IsRequired);
    }
    
    /// <summary>
    /// Gets all optional dependencies.
    /// </summary>
    /// <param name="asset">The asset to get dependencies from.</param>
    /// <returns>Collection of optional dependencies.</returns>
    public static IEnumerable<AssetDependency> GetOptionalDependencies(this AssetData asset)
    {
        return asset.Dependencies.Where(d => !d.IsRequired);
    }
    
    /// <summary>
    /// Checks if an asset has any unresolved dependencies.
    /// </summary>
    /// <param name="asset">The asset to check.</param>
    /// <returns>True if there are unresolved dependencies, false otherwise.</returns>
    public static bool HasUnresolvedDependencies(this AssetData asset)
    {
        return asset.Dependencies.Any(d => string.IsNullOrEmpty(d.FilePath));
    }
    
    /// <summary>
    /// Gets all unresolved dependencies.
    /// </summary>
    /// <param name="asset">The asset to get dependencies from.</param>
    /// <returns>Collection of unresolved dependencies.</returns>
    public static IEnumerable<AssetDependency> GetUnresolvedDependencies(this AssetData asset)
    {
        return asset.Dependencies.Where(d => string.IsNullOrEmpty(d.FilePath));
    }
}
