using Avalonia;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;

namespace REAssetExplorer.Desktop.Models;

public sealed partial class AssetNode : ObservableObject, ITreeNode
{
    private List<AssetNode>? subfolders;

    public AssetNode(string name, string fullPath, AssetKind kind, AssetNode? parent)
    {
        Name = name;
        FullPath = fullPath;
        Kind = kind;
        Parent = parent;
        Depth = parent == null ? -1 : parent.Depth + 1;
        if (kind == AssetKind.Folder) Children = [];
    }

    public string Name { get; }
    // Pak path for files, folder path (no trailing slash) for folders.
    public string FullPath { get; }
    public AssetKind Kind { get; }
    public AssetNode? Parent { get; }
    public int Depth { get; }
    public List<AssetNode>? Children { get; }
    public string? Version { get; init; }
    public long Size { get; init; }
    public int FileCount { get; set; }

    [ObservableProperty]
    private bool isExpanded;

    public bool IsFolder => Children != null;
    public bool IsTopLevel => Depth == 0;
    public IReadOnlyList<AssetNode> Subfolders => subfolders ??= Children?.Where(c => c.IsFolder).ToList() ?? [];
    public bool HasSubfolders => Subfolders.Count > 0;
    public Thickness Indent => new(Depth * 14, 0, 0, 0);
    public string KindLabel => AssetKinds.Label(Kind);
    public Geometry? Icon => Icons.Get(AssetKinds.IconKey(Kind));
    public IBrush KindBrush => AssetKinds.Brush(Kind);
    public bool CanOpen => AssetKinds.CanPreview(Kind) || AssetKinds.CanOutline(Kind);
    public string SizeText => IsFolder ? string.Empty : FormatSize(Size);
    public string FolderPath => Parent?.FullPath ?? string.Empty;

    public static string FormatSize(long bytes) => bytes switch
    {
        < 1024 => $"{bytes} B",
        < 1024 * 1024 => $"{bytes / 1024.0:0.#} KB",
        < 1024L * 1024 * 1024 => $"{bytes / (1024.0 * 1024):0.#} MB",
        _ => $"{bytes / (1024.0 * 1024 * 1024):0.##} GB"
    };
}
