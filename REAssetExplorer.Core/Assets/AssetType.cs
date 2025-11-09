namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Represents the type of game asset.
/// </summary>
public enum AssetType
{
    /// <summary>
    /// Unknown or unrecognized asset type.
    /// </summary>
    Unknown,
    
    /// <summary>
    /// 3D mesh geometry.
    /// </summary>
    Mesh,
    
    /// <summary>
    /// Texture image data.
    /// </summary>
    Texture,
    
    /// <summary>
    /// Material definition.
    /// </summary>
    Material,
    
    /// <summary>
    /// Animation data.
    /// </summary>
    Animation,
    
    /// <summary>
    /// Audio/sound file.
    /// </summary>
    Sound,
}
