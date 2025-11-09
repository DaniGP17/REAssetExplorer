using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Games.RE7.Assets;

/// <summary>
/// Reader for RE7 material files (.mdf2).
/// </summary>
public class RE7MaterialReader : IAssetReader<MaterialData>
{
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mdf2.10",
        ".mdf2"
    };

    /// <inheritdoc/>
    public IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Material;

    /// <inheritdoc/>
    public bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        // TODO: Implement actual RE7 material magic number check
        return true;
    }

    /// <inheritdoc/>
    public Result<MaterialData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            // TODO: Implement actual RE7 material parsing logic
            // Read shader name, texture references, properties, etc.
            
            var name = Path.GetFileNameWithoutExtension(fileName);
            
            // Placeholder
            var material = new MaterialData(
                Name: name,
                ShaderName: "Unknown",
                TextureReferences: Array.Empty<string>(),
                Properties: new Dictionary<string, object>()
            );

            return Result<MaterialData>.Success(material);
        }
        catch (Exception ex)
        {
            return Result<MaterialData>.Failure($"Failed to read RE7 material '{fileName}': {ex.Message}");
        }
    }
}
