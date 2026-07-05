using System.Globalization;
using Avalonia.Collections;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record KindFilter(string Name, AssetKind? Kind);

public enum AssetSortColumn
{
    Name,
    Type,
    Size,
    Version
}

public sealed partial class AssetBrowserViewModel : ObservableObject
{
    private const int MaxSearchResults = 5000;

    private readonly FlatTree<AssetNode> folderTree = new(n => n.Subfolders, n => n.Parent is { Depth: >= 0 } p ? p : null);
    private readonly Stack<AssetNode> backHistory = new();
    private readonly Stack<AssetNode> forwardHistory = new();
    private AssetTree? tree;
    private CancellationTokenSource? searchCancel;
    private List<AssetNode> content = [];
    private bool syncingTree;

    public AssetBrowserViewModel()
    {
        Filters = [new KindFilter("All types", null), .. AssetKinds.FileKinds.Select(k => new KindFilter(AssetKinds.Label(k), k))];
        selectedFilter = Filters[0];
    }

    public AvaloniaList<AssetNode> FolderRows => folderTree.Rows;
    public AvaloniaList<AssetNode> ListItems { get; } = [];
    public AvaloniaList<AssetNode> Breadcrumb { get; } = [];
    public IReadOnlyList<KindFilter> Filters { get; }

    [ObservableProperty]
    private KindFilter selectedFilter;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(GoUpCommand))]
    private AssetNode? currentFolder;

    [ObservableProperty]
    private AssetNode? selectedFolderRow;

    [ObservableProperty]
    private AssetNode? selectedAsset;

    [ObservableProperty]
    private string searchText = string.Empty;

    [ObservableProperty]
    private string totalText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsSortedByName), nameof(IsSortedByType), nameof(IsSortedBySize), nameof(IsSortedByVersion))]
    private AssetSortColumn sortColumn = AssetSortColumn.Name;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SortArrow))]
    private bool sortDescending;

    public bool IsSortedByName => SortColumn == AssetSortColumn.Name;
    public bool IsSortedByType => SortColumn == AssetSortColumn.Type;
    public bool IsSortedBySize => SortColumn == AssetSortColumn.Size;
    public bool IsSortedByVersion => SortColumn == AssetSortColumn.Version;
    public Geometry? SortArrow => Icons.Get(SortDescending ? "IconTriangleDown" : "IconTriangleUp");

    [RelayCommand]
    private void SortBy(AssetSortColumn column)
    {
        if (column == SortColumn) SortDescending = !SortDescending;
        else
        {
            SortColumn = column;
            SortDescending = false;
        }
        ShowContent();
    }

    public event Action<AssetNode>? Activated;
    public event Action<AssetNode>? Selected;

    public void Clear()
    {
        tree = null;
        searchCancel?.Cancel();
        backHistory.Clear();
        forwardHistory.Clear();
        folderTree.Reset([]);
        content = [];
        CurrentFolder = null;
        SelectedAsset = null;
        Breadcrumb.Clear();
        ShowContent();
        GoBackCommand.NotifyCanExecuteChanged();
        GoForwardCommand.NotifyCanExecuteChanged();
    }

    public void SetTree(AssetTree value)
    {
        tree = value;
        folderTree.Reset(value.Root.Subfolders);
        AssetNode start = value.Root;
        AssetNode? natives = value.Root.Subfolders.FirstOrDefault(f => f.Name == "natives");
        AssetNode? stm = natives?.Subfolders.FirstOrDefault(f => f.Name == "stm");
        if (stm != null)
        {
            folderTree.Reveal(stm);
            folderTree.Expand(stm);
            start = stm;
        }
        Navigate(start, remember: false);
    }

    public void Navigate(AssetNode folder, bool remember = true)
    {
        if (!folder.IsFolder) return;
        if (remember && CurrentFolder != null && CurrentFolder != folder)
        {
            backHistory.Push(CurrentFolder);
            forwardHistory.Clear();
        }
        CurrentFolder = folder;
        if (!string.IsNullOrEmpty(SearchText)) SearchText = string.Empty;

        Breadcrumb.Clear();
        var chain = new List<AssetNode>();
        for (AssetNode? n = folder; n is { Depth: >= 0 }; n = n.Parent) chain.Add(n);
        chain.Reverse();
        Breadcrumb.AddRange(chain);

        if (folder.Depth >= 0)
        {
            syncingTree = true;
            folderTree.Reveal(folder);
            SelectedFolderRow = folder;
            syncingTree = false;
        }
        ShowFolderContent();
        GoBackCommand.NotifyCanExecuteChanged();
        GoForwardCommand.NotifyCanExecuteChanged();
    }

    public void ToggleFolder(AssetNode folder) => folderTree.Toggle(folder);

    public void ExpandFolder(AssetNode folder) => folderTree.Expand(folder);

    public void CollapseFolder(AssetNode folder) => folderTree.Collapse(folder);

    public void Select(AssetNode? node)
    {
        SelectedAsset = node;
        if (node != null) Selected?.Invoke(node);
    }

    public void Activate(AssetNode node)
    {
        if (node.IsFolder) Navigate(node);
        else Activated?.Invoke(node);
    }

    public AssetTree? Tree => tree;

    public AssetNode? FindFile(string path) =>
        tree?.Files.FirstOrDefault(f => f.FullPath.Equals(path, StringComparison.OrdinalIgnoreCase))
        ?? tree?.Files.FirstOrDefault(f => f.FullPath.Contains(path, StringComparison.OrdinalIgnoreCase));

    public void Reveal(AssetNode file)
    {
        if (file.Parent != null) Navigate(file.Parent);
        Select(file);
    }

    private bool CanGoBack() => backHistory.Count > 0;

    [RelayCommand(CanExecute = nameof(CanGoBack))]
    private void GoBack()
    {
        if (CurrentFolder != null) forwardHistory.Push(CurrentFolder);
        Navigate(backHistory.Pop(), remember: false);
    }

    private bool CanGoForward() => forwardHistory.Count > 0;

    [RelayCommand(CanExecute = nameof(CanGoForward))]
    private void GoForward()
    {
        if (CurrentFolder != null) backHistory.Push(CurrentFolder);
        Navigate(forwardHistory.Pop(), remember: false);
    }

    private bool CanGoUp() => CurrentFolder?.Parent != null;

    [RelayCommand(CanExecute = nameof(CanGoUp))]
    private void GoUp()
    {
        if (CurrentFolder?.Parent is { } parent) Navigate(parent);
    }

    [RelayCommand]
    private void CollapseFolders() => folderTree.SetExpandedAll(false);

    partial void OnSelectedFolderRowChanged(AssetNode? value)
    {
        if (!syncingTree && value != null && value != CurrentFolder) Navigate(value);
    }

    partial void OnSelectedFilterChanged(KindFilter value)
    {
        if (string.IsNullOrWhiteSpace(SearchText)) ShowFolderContent();
        else _ = RunSearchAsync(SearchText);
    }

    partial void OnSearchTextChanged(string value) => _ = RunSearchAsync(value);

    private bool PassesFilter(AssetNode node) => SelectedFilter.Kind == null || node.Kind == SelectedFilter.Kind;

    private void ShowFolderContent()
    {
        content = CurrentFolder?.Children?.Where(n => n.IsFolder || PassesFilter(n)).ToList() ?? [];
        TotalText = $"Total: {content.Count(n => !n.IsFolder):N0} assets";
        ShowContent();
    }

    private void ShowContent()
    {
        ListItems.Clear();
        ListItems.AddRange(Sorted(content));
    }

    private IEnumerable<AssetNode> Sorted(IEnumerable<AssetNode> nodes)
    {
        StringComparer names = StringComparer.OrdinalIgnoreCase;
        IOrderedEnumerable<AssetNode> ordered = nodes.OrderBy(n => n.IsFolder ? 0 : 1);
        ordered = SortColumn switch
        {
            AssetSortColumn.Type => By(ordered, n => n.KindLabel, names),
            AssetSortColumn.Size => By(ordered, n => n.Size, Comparer<long>.Default),
            AssetSortColumn.Version => By(ordered, VersionKey, Comparer<(long, string)>.Default),
            _ => By(ordered, n => n.Name, names)
        };
        return SortColumn == AssetSortColumn.Name ? ordered : ordered.ThenBy(n => n.Name, names);
    }

    private IOrderedEnumerable<AssetNode> By<TKey>(IOrderedEnumerable<AssetNode> source, Func<AssetNode, TKey> key,
                                                   IComparer<TKey> comparer) =>
        SortDescending ? source.ThenByDescending(key, comparer) : source.ThenBy(key, comparer);

    // Versions look like "2109108288" or "2.x64.ja".
    private static (long, string) VersionKey(AssetNode node)
    {
        string version = node.Version ?? string.Empty;
        int digits = 0;
        while (digits < version.Length && char.IsAsciiDigit(version[digits])) digits++;
        long number = digits > 0 && long.TryParse(version.AsSpan(0, digits), NumberStyles.None, CultureInfo.InvariantCulture,
                                                  out long parsed) ? parsed : -1;
        return (number, version[digits..]);
    }

    private async Task RunSearchAsync(string text)
    {
        searchCancel?.Cancel();
        var cancel = new CancellationTokenSource();
        searchCancel = cancel;

        string[] terms = text.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (terms.Length == 0 || tree == null)
        {
            ShowFolderContent();
            return;
        }

        try
        {
            await Task.Delay(200, cancel.Token);
            List<AssetNode> files = tree.Files;
            AssetKind? kind = SelectedFilter.Kind;
            (List<AssetNode> hits, int total) = await Task.Run(() =>
            {
                var found = new List<AssetNode>();
                int matches = 0;
                foreach (AssetNode file in files)
                {
                    cancel.Token.ThrowIfCancellationRequested();
                    if (kind != null && file.Kind != kind) continue;
                    if (!terms.All(t => file.FullPath.Contains(t, StringComparison.OrdinalIgnoreCase))) continue;
                    if (matches++ < MaxSearchResults) found.Add(file);
                }
                return (found, matches);
            }, cancel.Token);

            if (cancel.IsCancellationRequested) return;
            content = hits;
            TotalText = total > MaxSearchResults
                ? $"Total: {total:N0} assets (first {MaxSearchResults:N0} shown)"
                : $"Total: {total:N0} assets";
            ShowContent();
        }
        catch (OperationCanceledException)
        {
        }
    }
}
