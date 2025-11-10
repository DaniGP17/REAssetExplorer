using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.UI.Models;

/// <summary>
/// Represents an item in the file tree view.
/// </summary>
public class TreeItem
{
    /// <summary>
    /// The display name of the item.
    /// </summary>
    public string Name { get; set; } = string.Empty;
    
    /// <summary>
    /// The icon identifier for the item.
    /// </summary>
    public string Icon { get; set; } = string.Empty;
    
    /// <summary>
    /// Child items if this is a folder.
    /// </summary>
    public List<TreeItem> Children { get; set; } = new();
    
    /// <summary>
    /// Indicates whether this item is a folder.
    /// </summary>
    public bool IsFolder { get; set; }
    
    /// <summary>
    /// Indicates whether the item's children have been loaded.
    /// </summary>
    public bool IsLoaded { get; set; }
    
    /// <summary>
    /// File metadata (size, checksum, flags, etc.) - only for files, not folders.
    /// </summary>
    public FileMetadata? Metadata { get; set; }
}

/// <summary>
/// Contains metadata about a file entry from the PAK.
/// </summary>
public class FileMetadata
{
    public long CompressedSize { get; set; }
    public long UncompressedSize { get; set; }
    public long Checksum { get; set; }
    public byte[] Flags { get; set; } = Array.Empty<byte>();
    public bool IsCompressed { get; set; }
    public CompressionType Compression { get; set; }
}