using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Extension methods for working with asset dependencies.
/// </summary>
public static class AssetDependencyExtensions
{
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
