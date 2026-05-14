using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.TestUI.Models;

public class TreeManager
{
    public static TreeManager Instance { get; } = new();

    private readonly TreeItem _root = new() { Name = "Root", IsFolder = true };

    private TreeManager() { }

    public TreeItem Root => _root;

    public void Clear() => _root.Children.Clear();

    public void BuildTree(PakFile pakFile)
    {
        foreach (var entry in pakFile.Entries)
            AddEntry(pakFile.Name, entry);
    }

    public void SortTree(TreeItem node)
    {
        node.Children = node.Children
            .OrderByDescending(c => c.IsFolder)
            .ThenBy(c => c.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();

        foreach (var child in node.Children.Where(c => c.IsFolder))
            SortTree(child);
    }

    private void AddEntry(string pakName, PakEntry entry)
    {
        var filePath = entry.FilePath;
        var fileName = Path.GetFileName(filePath);
        if (fileName.StartsWith("Unknown_", StringComparison.OrdinalIgnoreCase))
            filePath = $"Unknown/{fileName}";

        var parts = $"{pakName}/{filePath}".Split('/');
        var current = _root;

        for (int i = 0; i < parts.Length; i++)
        {
            bool isFolder = i < parts.Length - 1;
            current = GetOrCreate(current, parts[i], isFolder, isFolder ? null : entry);
        }
    }

    private static TreeItem GetOrCreate(TreeItem parent, string name, bool isFolder, PakEntry? entry)
    {
        var existing = parent.Children.FirstOrDefault(c => c.Name == name);
        if (existing != null) return existing;

        var node = new TreeItem { Name = name, IsFolder = isFolder };

        if (!isFolder && entry.HasValue)
        {
            var e = entry.Value;
            node.Metadata = new FileMetadata
            {
                CompressedSize   = e.CompressedSize,
                UncompressedSize = e.UncompressedSize,
                Checksum         = e.Checksum,
                IsCompressed     = e.IsCompressed,
                Compression      = e.CompressionType,
                PakEntry         = e,
            };
        }

        parent.Children.Add(node);
        return node;
    }
}
