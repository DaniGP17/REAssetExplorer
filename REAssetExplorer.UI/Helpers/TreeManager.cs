using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI.Models;
using REAssetExplorer.UI.Services;

namespace REAssetExplorer.UI.Helpers;

/// <summary>
/// Manages the tree structure of files from PAK archives.
/// </summary>
public class TreeManager
{
    private const string PathSeparator = "/";
    
    public static TreeManager Instance { get; } = new();
    
    private readonly TreeItem _root;
    
    private TreeManager()
    {
        _root = CreateRootNode();
    }
    
    public TreeItem Root => _root;

    /// <summary>
    /// Builds the tree structure from a PAK file's entries.
    /// </summary>
    /// <param name="pakFile">The PAK file to process.</param>
    public void BuildTree(PakFile pakFile)
    {
        ArgumentNullException.ThrowIfNull(pakFile);

        foreach (var entry in pakFile.Entries)
        {
            AddEntry(pakFile.Name, entry);
        }
    }

    /// <summary>
    /// Recursively sorts the tree structure (folders first, then alphabetically).
    /// </summary>
    /// <param name="node">The node to sort.</param>
    public void SortTree(TreeItem node)
    {
        ArgumentNullException.ThrowIfNull(node);

        node.Children = node.Children
            .OrderByDescending(c => c.IsFolder)
            .ThenBy(c => c.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();
        
        foreach (var child in node.Children.Where(c => c.IsFolder))
        {
            SortTree(child);
        }
    }

    /// <summary>
    /// Clears the tree and resets it to the root node.
    /// </summary>
    public void Clear()
    {
        _root.Children.Clear();
    }

    private static TreeItem CreateRootNode()
    {
        return new TreeItem
        {
            Name = "Root",
            Icon = "x",
            IsFolder = true,
            Children = new List<TreeItem>()
        };
    }

    private void AddEntry(string pakName, PakEntry entry)
    {
        var filePath = entry.FilePath;
        
        var fileName = System.IO.Path.GetFileName(filePath);
        if (fileName.StartsWith("Unknown_", StringComparison.OrdinalIgnoreCase))
        {
            // Build path: pakName/[directoryPath]/Unknown/filename
            filePath = $"Unknown{PathSeparator}{fileName}";
        }
        
        var fullPath = $"{pakName}{PathSeparator}{filePath}";
        var pathParts = fullPath.Split(PathSeparator);
        var extension = IconMapper.GetRealExtension(fullPath);
        
        BuildPathInTree(pathParts, extension, entry);
    }

    private void BuildPathInTree(string[] pathParts, string extension, PakEntry? entry = null)
    {
        var currentNode = _root;
        
        for (int i = 0; i < pathParts.Length; i++)
        {
            var part = pathParts[i];
            var isFolder = i < pathParts.Length - 1;
            var isLastItem = i == pathParts.Length - 1;
            
            currentNode = GetOrCreateChild(currentNode, part, isFolder, extension, isLastItem ? entry : null);
        }
    }

    private static TreeItem GetOrCreateChild(TreeItem parent, string name, bool isFolder, string extension, PakEntry? entry = null)
    {
        var existingChild = parent.Children.FirstOrDefault(c => c.Name == name);
        
        if (existingChild != null)
        {
            return existingChild;
        }

        var newChild = new TreeItem
        {
            Name = name,
            IsFolder = isFolder,
            Icon = isFolder 
                ? IconMapper.GetFolderIcon(name) 
                : IconMapper.GetFileIcon(extension),
            Children = new List<TreeItem>()
        };
        
        // Add metadata for files
        if (!isFolder && entry.HasValue)
        {
            var pakEntry = entry.Value;
            var flags = new byte[8];
            unsafe
            {
                for (int i = 0; i < 8; i++)
                {
                    flags[i] = pakEntry.Flags[i];
                }
            }
            
            newChild.Metadata = new FileMetadata
            {
                CompressedSize = pakEntry.CompressedSize,
                UncompressedSize = pakEntry.UncompressedSize,
                Checksum = pakEntry.Checksum,
                Flags = flags,
                IsCompressed = pakEntry.IsCompressed,
                Compression = pakEntry.CompressionType,
                PakEntry = pakEntry
            };
        }
        
        parent.Children.Add(newChild);
        return newChild;
    }
}