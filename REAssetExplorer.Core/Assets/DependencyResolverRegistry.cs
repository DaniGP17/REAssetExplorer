using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Registry for dependency resolvers.
/// Manages resolvers for different asset types.
/// </summary>
public class DependencyResolverRegistry
{
    private readonly Dictionary<AssetType, IDependencyResolver> _resolvers = new();
    
    /// <summary>
    /// Registers a dependency resolver for a specific asset type.
    /// </summary>
    /// <param name="resolver">The resolver to register.</param>
    public void RegisterResolver(IDependencyResolver resolver)
    {
        ArgumentNullException.ThrowIfNull(resolver);
        
        _resolvers[resolver.AssetType] = resolver;
    }
    
    /// <summary>
    /// Registers multiple resolvers at once.
    /// </summary>
    /// <param name="resolvers">The resolvers to register.</param>
    public void RegisterResolvers(params IDependencyResolver[] resolvers)
    {
        foreach (var resolver in resolvers)
        {
            RegisterResolver(resolver);
        }
    }
    
    /// <summary>
    /// Gets a resolver for a specific asset type.
    /// </summary>
    /// <param name="assetType">The asset type to get a resolver for.</param>
    /// <returns>The resolver if found, null otherwise.</returns>
    public IDependencyResolver? GetResolver(AssetType assetType)
    {
        return _resolvers.TryGetValue(assetType, out var resolver) ? resolver : null;
    }
    
    /// <summary>
    /// Checks if a resolver exists for a specific asset type.
    /// </summary>
    /// <param name="assetType">The asset type to check.</param>
    /// <returns>True if a resolver exists, false otherwise.</returns>
    public bool HasResolver(AssetType assetType)
    {
        return _resolvers.ContainsKey(assetType);
    }
    
    /// <summary>
    /// Removes a resolver for a specific asset type.
    /// </summary>
    /// <param name="assetType">The asset type to remove the resolver for.</param>
    /// <returns>True if the resolver was removed, false if it didn't exist.</returns>
    public bool RemoveResolver(AssetType assetType)
    {
        return _resolvers.Remove(assetType);
    }
    
    /// <summary>
    /// Clears all registered resolvers.
    /// </summary>
    public void Clear()
    {
        _resolvers.Clear();
    }
    
    /// <summary>
    /// Gets all registered asset types.
    /// </summary>
    /// <returns>Collection of registered asset types.</returns>
    public IEnumerable<AssetType> GetRegisteredAssetTypes()
    {
        return _resolvers.Keys;
    }
}
