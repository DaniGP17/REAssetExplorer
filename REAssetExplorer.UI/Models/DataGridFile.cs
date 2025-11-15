namespace REAssetExplorer.UI.Models;

/// <summary>
/// Represents a file item displayed in the DataGrid.
/// </summary>
public class DataGridFile
{
    /// <summary>
    /// The name of the file or folder.
    /// </summary>
    public string Name { get; set; } = string.Empty;
    
    /// <summary>
    /// The icon identifier for the item.
    /// </summary>
    public string Icon { get; set; } = string.Empty;
    
    /// <summary>
    /// The type of the item (File or Folder).
    /// </summary>
    public string Type { get; set; } = string.Empty;
    
    /// <summary>
    /// The size of the file (empty for folders).
    /// </summary>
    public string Size { get; set; } = string.Empty;
    
    /// <summary>
    /// The numeric size in bytes for sorting.
    /// </summary>
    public long SizeBytes { get; set; }
    
    /// <summary>
    /// The compressed size of the file.
    /// </summary>
    public string CompressedSize { get; set; } = string.Empty;
    
    /// <summary>
    /// The numeric compressed size in bytes for sorting.
    /// </summary>
    public long CompressedSizeBytes { get; set; }
    
    /// <summary>
    /// The checksum of the file in hexadecimal.
    /// </summary>
    public string Checksum { get; set; } = string.Empty;
    
    /// <summary>
    /// The compression type used for the file.
    /// </summary>
    public string Compression { get; set; } = string.Empty;
    
    /// <summary>
    /// Reference to the original TreeItem.
    /// </summary>
    public TreeItem? TreeItemReference { get; set; }
    
    /// <summary>
    /// Indicates whether this item is a folder.
    /// </summary>
    public bool IsFolder { get; set; }
}