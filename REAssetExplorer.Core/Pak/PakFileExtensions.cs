using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;

namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Extension methods for working with PAK files and assets.
/// </summary>
public static class PakFileExtensions
{
    /// <summary>
    /// Finds an entry in the PAK file by its path.
    /// </summary>
    /// <param name="pakFile">The PAK file to search.</param>
    /// <param name="path">The path to search for.</param>
    /// <returns>The entry if found, null otherwise.</returns>
    public static PakEntry? FindEntry(this PakFile pakFile, string path)
    {
        ArgumentNullException.ThrowIfNull(pakFile);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var normalizedPath = path.Replace('\\', '/').ToLowerInvariant();
        
        return pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Replace('\\', '/').ToLowerInvariant() == normalizedPath);
    }

    /// <summary>
    /// Reads a material asset from the PAK file.
    /// </summary>
    /// <param name="pakFile">The PAK file to read from.</param>
    /// <param name="path">The path to the material file within the PAK.</param>
    /// <param name="provider">The game provider with asset readers.</param>
    /// <returns>A result containing the material data or an error.</returns>
    /// <remarks>
    /// This is a placeholder method. You need to implement actual PAK entry data extraction.
    /// </remarks>
    public static Result<MaterialData> ReadMaterial(
        this PakFile pakFile,
        string path,
        IGameProvider provider)
    {
        ArgumentNullException.ThrowIfNull(pakFile);
        ArgumentNullException.ThrowIfNull(provider);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var entry = pakFile.FindEntry(path);
        if (entry == null)
            return Result<MaterialData>.Failure($"File not found in PAK: {path}");

        // TODO: Implement actual reading
        return Result<MaterialData>.Failure("Not implemented - add entry data extraction logic");
    }

    /// <summary>
    /// Tries to read any supported asset type from the PAK file.
    /// </summary>
    /// <typeparam name="T">The type of asset to read.</typeparam>
    /// <param name="pakFile">The PAK file to read from.</param>
    /// <param name="path">The path to the file within the PAK.</param>
    /// <param name="provider">The game provider with asset readers.</param>
    /// <returns>A result containing the asset data or an error.</returns>
    /// <remarks>
    /// This is a placeholder method. You need to implement actual PAK entry data extraction.
    /// </remarks>
    public static Result<T> ReadAsset<T>(
        this PakFile pakFile,
        string path,
        IGameProvider provider) where T : class
    {
        ArgumentNullException.ThrowIfNull(pakFile);
        ArgumentNullException.ThrowIfNull(provider);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        var entry = pakFile.FindEntry(path);
        if (entry == null)
            return Result<T>.Failure($"File not found in PAK: {path}");

        // TODO: Implement actual reading
        // var data = ExtractEntryData(pakFile, entry);
        // return provider.AssetReaders.Read<T>(path, data);

        return Result<T>.Failure("Not implemented - add entry data extraction logic");
    }
}
