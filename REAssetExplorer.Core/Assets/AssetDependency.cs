namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Represents a dependency between assets.
/// </summary>
public class AssetDependency
{
    /// <summary>
    /// The type of asset this dependency points to.
    /// </summary>
    public AssetType AssetType { get; set; }
    
    /// <summary>
    /// The file path of the dependency within the PAK.
    /// </summary>
    public string FilePath { get; set; } = string.Empty;
    
    /// <summary>
    /// The name or identifier of the dependency.
    /// </summary>
    public string Name { get; set; } = string.Empty;
    
    /// <summary>
    /// Whether this dependency is required for the asset to function.
    /// </summary>
    public bool IsRequired { get; set; } = true;
    
    /// <summary>
    /// The purpose of this dependency (e.g., "Albedo", "Normal", "Material").
    /// </summary>
    public string Purpose { get; set; } = string.Empty;
    
    /// <summary>
    /// Additional metadata about the dependency.
    /// </summary>
    public Dictionary<string, object> Metadata { get; set; } = new();
    
    public AssetDependency() { }
    
    public AssetDependency(AssetType assetType, string filePath, string name, bool isRequired = true, string purpose = "")
    {
        AssetType = assetType;
        FilePath = filePath;
        Name = name;
        IsRequired = isRequired;
        Purpose = purpose;
    }
}