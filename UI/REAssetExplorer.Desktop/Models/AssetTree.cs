using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.Models;

public sealed class AssetTree
{
    private const string UnresolvedFolder = "unknown";

    // Platform/language tags after the extension, as in ".bnk.2.x64"; numeric segments are versions.
    private static readonly HashSet<string> SuffixTags = new(StringComparer.OrdinalIgnoreCase)
    {
        "x64", "stm", "nsw", "ja", "en", "fr", "it", "de", "es", "ru", "pl", "nl", "pt", "ptbr", "ko",
        "zhcn", "zhtw", "ar", "tr", "latam", "hu", "cs"
    };

    private AssetTree(AssetNode root, List<AssetNode> files)
    {
        Root = root;
        Files = files;
    }

    private Dictionary<string, AssetNode>? bySource;

    public AssetNode Root { get; }
    public List<AssetNode> Files { get; }

    // Scenes and materials store "Props/x/y.mesh"; pak paths add natives/stm/,
    // streaming/ and version suffixes. Both forms map to the same key.
    public static string SourceKey(string path)
    {
        string key = path.Replace('\\', '/').TrimStart('/').ToLowerInvariant();
        if (key.StartsWith("natives/stm/", StringComparison.Ordinal)) key = key["natives/stm/".Length..];
        if (key.StartsWith("streaming/", StringComparison.Ordinal)) key = key["streaming/".Length..];
        int slash = key.LastIndexOf('/');
        return key[..(slash + 1)] + SplitFileName(key[(slash + 1)..]).Name;
    }

    public static bool IsAssetPath(string value)
    {
        if (value.Length < 3 || value.IndexOfAny(['/', '\\']) < 0 || value.Contains(' ')) return false;
        int slash = value.LastIndexOfAny(['/', '\\']);
        AssetKind kind = AssetKinds.FromExtension(SplitFileName(value[(slash + 1)..]).Extension);
        return kind is not (AssetKind.Other or AssetKind.Folder);
    }

    public static bool IsStreamingCopy(AssetNode file) =>
        file.FullPath.Contains("/streaming/", StringComparison.OrdinalIgnoreCase);

    public AssetNode? Resolve(string path)
    {
        if (bySource == null)
        {
            var index = new Dictionary<string, AssetNode>(Files.Count, StringComparer.Ordinal);
            foreach (AssetNode file in Files)
            {
                // The base file wins over its streaming/ copy.
                string key = SourceKey(file.FullPath);
                if (!index.TryGetValue(key, out AssetNode? existing) || IsStreamingCopy(existing)) index[key] = file;
            }
            bySource = index;
        }
        return bySource.GetValueOrDefault(SourceKey(path));
    }

    public static AssetTree Build(IReadOnlyList<PakFileEntry> entries)
    {
        var root = new AssetNode("", "", AssetKind.Folder, null);
        var folders = new Dictionary<string, AssetNode>(StringComparer.Ordinal) { [""] = root };
        var files = new List<AssetNode>(entries.Count);

        foreach (PakFileEntry entry in entries)
        {
            string path = entry.Path.StartsWith("Unknown_", StringComparison.Ordinal)
                ? UnresolvedFolder + "/" + entry.Path
                : entry.Path;
            int slash = path.LastIndexOf('/');
            AssetNode folder = GetFolder(folders, slash < 0 ? "" : path[..slash]);
            string fileName = path[(slash + 1)..];
            (string name, string extension, string? version) = SplitFileName(fileName);
            var node = new AssetNode(name, entry.Path, AssetKinds.FromExtension(extension), folder)
            {
                Version = version,
                Size = entry.Size
            };
            folder.Children!.Add(node);
            files.Add(node);
        }

        SortAndCount(root);
        return new AssetTree(root, files);
    }

    private static AssetNode GetFolder(Dictionary<string, AssetNode> folders, string path)
    {
        if (folders.TryGetValue(path, out AssetNode? folder)) return folder;
        int slash = path.LastIndexOf('/');
        AssetNode parent = GetFolder(folders, slash < 0 ? "" : path[..slash]);
        folder = new AssetNode(path[(slash + 1)..], path, AssetKind.Folder, parent);
        parent.Children!.Add(folder);
        folders[path] = folder;
        return folder;
    }

    private static (string Name, string Extension, string? Version) SplitFileName(string fileName)
    {
        string[] parts = fileName.Split('.');
        int ext = parts.Length - 1;
        while (ext > 1 && (IsNumber(parts[ext]) || SuffixTags.Contains(parts[ext]))) ext--;
        if (ext < 1) return (fileName, "", null);
        string name = string.Join('.', parts, 0, ext + 1);
        string? version = ext < parts.Length - 1 ? string.Join('.', parts, ext + 1, parts.Length - ext - 1) : null;
        return (name, parts[ext], version);
    }

    private static bool IsNumber(string text) => text.Length > 0 && text.All(char.IsAsciiDigit);

    private static int SortAndCount(AssetNode folder)
    {
        folder.Children!.Sort(static (a, b) =>
        {
            if (a.IsFolder != b.IsFolder) return a.IsFolder ? -1 : 1;
            return string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase);
        });
        int count = 0;
        foreach (AssetNode child in folder.Children) count += child.IsFolder ? SortAndCount(child) : 1;
        folder.FileCount = count;
        return count;
    }
}
