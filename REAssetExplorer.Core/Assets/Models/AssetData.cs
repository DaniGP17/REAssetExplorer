using System.Collections.Concurrent;
using System.Linq;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for all asset data types.
/// Contains common properties shared across all assets (textures, models, materials, etc.)
/// </summary>
public abstract class AssetData
{
    // === IDENTIFICATION ===
    
    /// <summary>
    /// Name of the asset (without extension).
    /// </summary>
    public string Name { get; set; } = string.Empty;
    
    /// <summary>
    /// Full path within the PAK archive.
    /// </summary>
    public string FilePath { get; set; } = string.Empty;
    
    /// <summary>
    /// File extension (e.g., ".tex", ".mdf2", ".mesh").
    /// </summary>
    public string Extension { get; set; } = string.Empty;
    
    // === PAK METADATA (Common to all files in PAK) ===
    
    /// <summary>
    /// Checksum value from PAK entry.
    /// </summary>
    public ulong Checksum { get; set; }
    
    /// <summary>
    /// Compression type used for this asset.
    /// </summary>
    public CompressionType Compression { get; set; }
    
    /// <summary>
    /// Compressed size in bytes.
    /// </summary>
    public long CompressedSize { get; set; }
    
    /// <summary>
    /// Uncompressed size in bytes.
    /// </summary>
    public long UncompressedSize { get; set; }
    
    /// <summary>
    /// Offset within the PAK file.
    /// </summary>
    public long Offset { get; set; }
    
    /// <summary>
    /// Whether the asset is compressed.
    /// </summary>
    public bool IsCompressed => CompressedSize != UncompressedSize;
    
    // === RAW DATA ===
    
    /// <summary>
    /// Raw uncompressed data of the asset.
    /// </summary>
    public byte[] RawData { get; set; } = Array.Empty<byte>();
    
    /// <summary>
    /// Size of the raw data.
    /// </summary>
    public long DataSize => RawData.LongLength;
    
    // === GAME-SPECIFIC METADATA ===
    
    /// <summary>
    /// Extended metadata for game-specific attributes.
    /// Use this for attributes that only exist in specific games or asset types.
    /// </summary>
    public Dictionary<string, object> ExtendedMetadata { get; set; } = new();
    
    // === VALIDATION ===
    
    /// <summary>
    /// Whether this asset has valid data.
    /// </summary>
    public virtual bool IsValid => !string.IsNullOrEmpty(Name) && RawData.Length > 0;

    // === DEPENDENCY SYSTEM ===
    
    /// <summary>
    /// List of dependencies (metadata) that this asset requires.
    /// </summary>
    public List<AssetDependency> Dependencies { get; set; } = new List<AssetDependency>();
    
    /// <summary>
    /// Dictionary of resolved dependencies (actual loaded asset objects).
    /// Key: FilePath of the dependency
    /// Value: The loaded AssetData object
    /// </summary>
    public ConcurrentDictionary<string, AssetData> ResolvedDependencies { get; set; } = new ConcurrentDictionary<string, AssetData>(StringComparer.Ordinal);
    
    /// <summary>
    /// Gets a resolved dependency by its file path.
    /// </summary>
    /// <typeparam name="T">The expected type of the dependency.</typeparam>
    /// <param name="filePath">The file path of the dependency.</param>
    /// <returns>The resolved dependency, or null if not found or wrong type.</returns>
    public T? GetResolvedDependency<T>(string filePath) where T : AssetData
    {
        if (ResolvedDependencies.TryGetValue(filePath, out var dependency))
        {
            return dependency as T;
        }
        return null;
    }
    
    /// <summary>
    /// Gets all resolved dependencies of a specific type.
    /// </summary>
    /// <typeparam name="T">The type of dependencies to retrieve.</typeparam>
    /// <returns>Collection of resolved dependencies of the specified type.</returns>
    public IEnumerable<T> GetResolvedDependencies<T>() where T : AssetData
    {
        return ResolvedDependencies.Values.OfType<T>();
    }
}
