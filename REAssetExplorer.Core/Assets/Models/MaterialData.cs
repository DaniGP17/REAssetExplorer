namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Represents material definition data.
/// </summary>
/// <param name="Name">The name of the material.</param>
/// <param name="ShaderName">The shader used by this material.</param>
/// <param name="TextureReferences">Texture file paths referenced by this material.</param>
/// <param name="Properties">Material properties (key-value pairs).</param>
public record MaterialData(
    string Name,
    string ShaderName,
    string[] TextureReferences,
    Dictionary<string, object> Properties
);
