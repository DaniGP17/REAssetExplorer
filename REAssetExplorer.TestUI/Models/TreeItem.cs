using System.Collections.Generic;
using System.Linq;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.TestUI.Models;

public class TreeItem
{
    public string Name { get; set; } = string.Empty;
    public List<TreeItem> Children { get; set; } = new();
    public bool IsFolder { get; set; }
    public FileMetadata? Metadata { get; set; }

    public IEnumerable<TreeItem> FolderChildren => Children.Where(c => c.IsFolder);
}

public class FileMetadata
{
    public long CompressedSize { get; set; }
    public long UncompressedSize { get; set; }
    public long Checksum { get; set; }
    public bool IsCompressed { get; set; }
    public CompressionType Compression { get; set; }
    public PakEntry? PakEntry { get; set; }
}
