using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed partial class HierarchyViewModel : ObservableObject
{
    private const int MaxSearchResults = 2000;

    private readonly FlatTree<OutlineNode> tree = new(n => n.Children, n => n.Parent);
    private List<OutlineNode> all = [];
    private List<OutlineNode> baseRoots = [];

    private OutlineNode? selectedNode;
    private List<OutlineNode> selectedNodes = [];

    [ObservableProperty]
    private IEnumerable<OutlineNode> rows = [];

    [ObservableProperty]
    private string searchText = string.Empty;

    public OutlineNode? SelectedNode => selectedNode;

    public IReadOnlyList<OutlineNode> SelectedNodes => selectedNodes;

    public event Action<OutlineNode?>? Selected;
    public event Action<OutlineNode?>? SelectionRequested;
    public event Action<OutlineNode>? Activated;

    public void Activate(OutlineNode node) => Activated?.Invoke(node);

    public event Action<IReadOnlyList<string>>? VisibilityChanged;

    public void Clear()
    {
        all = [];
        tree.Reset([]);
        Rows = tree.Rows;
        Select(null);
    }

    public async Task LoadAsync(NativeGame game, AssetNode node)
    {
        Clear();
        try
        {
            string text = await Task.Run(() => game.Outline(node.FullPath));
            List<OutlineNode> roots = await Task.Run(() => OutlineNode.Parse(text));
            SetOutline(roots);
        }
        catch (Exception e)
        {
            NativeLog.Write(LogLevel.Warning, $"{node.Name}: {e.Message}");
        }
    }

    public void SetOutline(List<OutlineNode> roots)
    {
        baseRoots = roots;
        all = roots.SelectMany(r => r.DescendantsAndSelf()).ToList();
        foreach (OutlineNode root in roots)
        {
            root.IsExpanded = root.CanExpand;
            foreach (OutlineNode child in root.Children)
            {
                // Groups are small; scene folders can hold hundreds of objects.
                if (child.Kind == "group" && child.Children.Count <= 64) child.IsExpanded = true;
            }
        }
        tree.Reset(roots);
        SearchText = string.Empty;
        Rows = tree.Rows;
        Select(null);
    }

    public void SetExtraRoots(IReadOnlyList<OutlineNode> extra)
    {
        List<OutlineNode> roots = baseRoots.Concat(extra).ToList();
        all = roots.SelectMany(r => r.DescendantsAndSelf()).ToList();
        foreach (OutlineNode node in extra) node.IsExpanded = node.CanExpand;
        tree.Reset(roots);
        Rows = tree.Rows;
        if (SearchText.Length > 0) OnSearchTextChanged(SearchText);
    }

    public IReadOnlyList<OutlineNode> Nodes => all;

    public OutlineNode? Find(string kind, string key) =>
        key.Length == 0 ? null : all.FirstOrDefault(n => n.Kind == kind && n.Key == key);

    // The list can only select a row it shows.
    public void Select(OutlineNode? node)
    {
        if (node != null)
        {
            if (SearchText.Length > 0) SearchText = string.Empty;
            tree.Reveal(node);
        }
        SetActive(node, node == null ? [] : [node]);
        SelectionRequested?.Invoke(node);
    }

    public void SetSelection(OutlineNode? active, IReadOnlyList<OutlineNode> nodes) => SetActive(active, nodes.ToList());

    public void ToggleVisibility(OutlineNode node)
    {
        node.IsHidden = !node.IsHidden;
        node.RefreshVisibility();
        VisibilityChanged?.Invoke(HiddenKeys());
    }

    public void ToggleSelectedVisibility()
    {
        List<OutlineNode> nodes = selectedNodes.Where(n => n.InScene).ToList();
        if (nodes.Count == 0) return;
        bool hide = nodes.Any(n => !n.EffectiveHidden);
        foreach (OutlineNode node in nodes) node.IsHidden = hide;
        foreach (OutlineNode node in nodes) node.RefreshVisibility();
        VisibilityChanged?.Invoke(HiddenKeys());
    }

    public IReadOnlyList<string> HiddenKeys() =>
        all.Where(n => n.Kind is "gameobject" or "emitter" or "rendermesh" or "layer" or "guielement" or "aigroup" or
                           "ailinks" or "ailayer" or "charpart" && n.EffectiveHidden)
            .Select(n => n.Key).ToList();

    public void Toggle(OutlineNode node) => tree.Toggle(node);

    public void Expand(OutlineNode node) => tree.Expand(node);

    public void Collapse(OutlineNode node) => tree.Collapse(node);

    [RelayCommand]
    private void ExpandAll() => tree.SetExpandedAll(true);

    [RelayCommand]
    private void CollapseAll() => tree.SetExpandedAll(false);

    private void SetActive(OutlineNode? active, List<OutlineNode> nodes)
    {
        selectedNode = active;
        selectedNodes = nodes;
        OnPropertyChanged(nameof(SelectedNode));
        Selected?.Invoke(active);
    }

    partial void OnSearchTextChanged(string value)
    {
        string text = value.Trim();
        Rows = text.Length == 0
            ? tree.Rows
            : all.Where(n => n.Name.Contains(text, StringComparison.OrdinalIgnoreCase)).Take(MaxSearchResults).ToList();
    }
}
