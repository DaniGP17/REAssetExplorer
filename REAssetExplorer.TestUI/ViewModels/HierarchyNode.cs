using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.TestUI;

public class HierarchyNode : INotifyPropertyChanged
{
    private bool _isExpanded;
    private bool _isSelected;

    public string Name { get; init; } = "";
    public HierarchyItemType ItemType { get; init; }

    /// <summary>
    /// The originating SceneNode from the parsed scene data.
    /// Cast to SceneGameObjectNode / SceneFolderNode to access components, RSZ data, etc.
    /// </summary>
    public SceneNode? SourceNode { get; init; }

    /// <summary>
    /// Populated only for HierarchyItemType.Mesh — the parsed mesh asset backing this node.
    /// </summary>
    public MeshData? SourceMesh { get; init; }

    public ObservableCollection<HierarchyNode> Children { get; } = [];
    public bool HasChildren => Children.Count > 0;

    public bool IsExpanded
    {
        get => _isExpanded;
        set { if (_isExpanded == value) return; _isExpanded = value; OnPropertyChanged(); }
    }

    public bool IsSelected
    {
        get => _isSelected;
        set { if (_isSelected == value) return; _isSelected = value; OnPropertyChanged(); }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? n = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));

    // ── Scene → ViewModel adapter ─────────────────────────────────────────────

    public static ObservableCollection<HierarchyNode> BuildFromScene(SceneData scene)
    {
        var result = new ObservableCollection<HierarchyNode>();
        if (scene.Root == null) return result;
        foreach (var child in scene.Root.Children)
            result.Add(FromNode(child));
        return result;
    }

    public static ObservableCollection<HierarchyNode> BuildFromMesh(MeshData mesh, string displayName)
    {
        return new ObservableCollection<HierarchyNode>
        {
            new()
            {
                Name       = displayName,
                ItemType   = HierarchyItemType.Mesh,
                SourceMesh = mesh,
            }
        };
    }

    private static HierarchyNode FromNode(SceneNode node)
    {
        var hn = new HierarchyNode
        {
            Name       = GetName(node),
            ItemType   = GetItemType(node),
            SourceNode = node,
        };
        foreach (var child in node.Children)
            hn.Children.Add(FromNode(child));
        return hn;
    }

    private static string GetName(SceneNode node) => node switch
    {
        SceneGameObjectNode go when go.GameObject?.Name is { Length: > 0 } n => n,
        SceneGameObjectNode                                                    => "GameObject",
        SceneFolderNode f   when f.Folder?.ScenePath is { Length: > 0 } p    =>
            Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(p)),
        SceneFolderNode f   when f.Folder?.Name is { Length: > 0 } n         => n,
        SceneFolderNode                                                        => "Folder",
        _                                                                      => "Node",
    };

    private static HierarchyItemType GetItemType(SceneNode node) => node switch
    {
        SceneGameObjectNode                                      => HierarchyItemType.GameObject,
        SceneFolderNode { Folder.ScenePath: { Length: > 0 } }   => HierarchyItemType.Scene,
        _                                                        => HierarchyItemType.Folder,
    };
}
