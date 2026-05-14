using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;
using REAssetExplorer.TestUI.Models;

namespace REAssetExplorer.TestUI.Controls;

// ── Breadcrumb segment ────────────────────────────────────────────────────────

public class BreadcrumbSegment
{
    public string     Name               { get; init; } = string.Empty;
    public TreeItem?  TreeItemRef        { get; init; }
    public Visibility SeparatorVisibility { get; init; } = Visibility.Visible;
}

// ── File row (DataGrid view model) ───────────────────────────────────────────

public class FileRow
{
    public string   Name               { get; init; } = string.Empty;
    public string   ExtTag             { get; init; } = string.Empty;
    public Brush    TagColor           { get; init; } = Brushes.Gray;
    public string   Type               { get; init; } = string.Empty;
    public string   Size               { get; init; } = string.Empty;
    public long     SizeBytes          { get; init; }
    public string   CompressedSize     { get; init; } = string.Empty;
    public long     CompressedSizeBytes { get; init; }
    public string   Compression        { get; init; } = string.Empty;
    public string   Checksum           { get; init; } = string.Empty;
    public bool     IsFolder           { get; init; }
    public TreeItem? TreeItemRef       { get; init; }
}

// ── Panel ─────────────────────────────────────────────────────────────────────

public partial class AssetBrowserPanel : UserControl
{
    private readonly Stack<TreeItem> _backHistory    = new();
    private readonly Stack<TreeItem> _forwardHistory = new();
    private TreeItem? _currentNode;
    private bool      _isSearchMode;

    public ObservableCollection<FileRow> Rows { get; } = new();

    public AssetBrowserPanel()
    {
        InitializeComponent();
        FileGrid.ItemsSource = Rows;
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= OnLoaded;
        var root = TreeManager.Instance.Root;
        if (root.Children.Count > 0)
        {
            FolderTree.ItemsSource = root.FolderChildren;
            NavigateTo(root, addToHistory: false);
        }
    }

    // ── Navigation ────────────────────────────────────────────────────────────

    private void NavigateTo(TreeItem node, bool addToHistory = true)
    {
        if (addToHistory && _currentNode != null && _currentNode != node)
        {
            _backHistory.Push(_currentNode);
            _forwardHistory.Clear();
        }
        _currentNode  = node;
        _isSearchMode = false;
        SearchBox.Text = string.Empty;
        UpdateBreadcrumb(node);
        LoadContents(node);
    }

    private void LoadContents(TreeItem node)
    {
        Rows.Clear();
        foreach (var child in node.Children)
            Rows.Add(MakeRow(child));
    }

    private void UpdateBreadcrumb(TreeItem node)
    {
        // Build path from root to the target node
        var path = new List<TreeItem>();
        BuildPath(TreeManager.Instance.Root, node, path);
        path.Insert(0, TreeManager.Instance.Root);

        var segments = path
            .Select((item, i) => new BreadcrumbSegment
            {
                Name               = item.Name,
                TreeItemRef        = item,
                SeparatorVisibility = i < path.Count - 1 ? Visibility.Visible : Visibility.Collapsed,
            })
            .ToList();

        BreadcrumbBar.ItemsSource = segments;
    }

    private static bool BuildPath(TreeItem current, TreeItem target, List<TreeItem> path)
    {
        if (current == target) return true;

        foreach (var child in current.Children)
        {
            if (BuildPath(child, target, path))
            {
                path.Insert(0, child);
                return true;
            }
        }
        return false;
    }

    // ── Event handlers ────────────────────────────────────────────────────────

    private void OnBack(object sender, RoutedEventArgs e)
    {
        if (_backHistory.Count == 0) return;
        _forwardHistory.Push(_currentNode!);
        var prev = _backHistory.Pop();
        _currentNode = null;
        NavigateTo(prev, addToHistory: false);
    }

    private void OnForward(object sender, RoutedEventArgs e)
    {
        if (_forwardHistory.Count == 0) return;
        _backHistory.Push(_currentNode!);
        var next = _forwardHistory.Pop();
        _currentNode = null;
        NavigateTo(next, addToHistory: false);
    }

    private void OnUp(object sender, RoutedEventArgs e)
    {
        if (_currentNode == null || _currentNode == TreeManager.Instance.Root) return;
        var parent = FindParent(TreeManager.Instance.Root, _currentNode);
        if (parent != null) NavigateTo(parent);
    }

    private void OnBreadcrumbClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: TreeItem node })
            NavigateTo(node);
    }

    private void OnTreeFolderSelected(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is TreeItem node && node != _currentNode)
            NavigateTo(node);
    }

    private async void OnRowDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (sender is not DataGridRow { Item: FileRow row }) return;

        if (row.IsFolder && row.TreeItemRef != null)
        {
            NavigateTo(row.TreeItemRef);
            return;
        }

        var ext = GetRealExt(row.Name);
        if (ext.Equals("scn", StringComparison.OrdinalIgnoreCase))
            await OpenSceneAsync(row);
        else if (ext.Equals("mesh", StringComparison.OrdinalIgnoreCase))
            await OpenMeshAsync(row);
        else if (ext.Equals("tex", StringComparison.OrdinalIgnoreCase))
            OpenTexture(row);
        else if (HasBankReader(row.Name))
            // Wwise banks have versioned extensions like .bnk.2.stm which GetRealExt
            // can't reliably collapse to "bnk", so fall back to the reader registry.
            OpenBank(row);
    }

    private static bool HasBankReader(string fileName)
    {
        var provider = App.CurrentProvider;
        return provider?.AssetReaders.GetReader<REAssetExplorer.Core.Assets.Models.BankData>(fileName) != null;
    }

    private static void OpenTexture(FileRow row)
    {
        var entry = row.TreeItemRef?.Metadata?.PakEntry;
        if (entry == null)
        {
            new StatusWindow(StatusType.Error, "Cannot open texture: no PAK entry.").Show();
            return;
        }

        var window = new TextureViewerWindow(row.Name, entry.Value);
        window.Show();
    }

    private static void OpenBank(FileRow row)
    {
        var entry = row.TreeItemRef?.Metadata?.PakEntry;
        if (entry == null)
        {
            new StatusWindow(StatusType.Error, "Cannot open bank: no PAK entry.").Show();
            return;
        }

        var window = new BankViewerWindow(row.Name, entry.Value);
        window.Show();
    }

    private async Task OpenSceneAsync(FileRow row)
    {
        var session = AssetSession.Current;
        if (session == null) return;

        var filePath = GetRelativeFilePath(row);
        if (filePath == null) return;

        var status = new StatusWindow(StatusType.Loading, $"Loading {row.Name}...");
        status.Show();

        var result = await session.LoadSceneAsync(filePath,
            msg => Dispatcher.Invoke(() => status.UpdateMessage(msg)));

        status.Close();

        if (result.IsFailure)
            new StatusWindow(StatusType.Error, $"Failed to load scene:\n{result.Error}").Show();
    }

    private async Task OpenMeshAsync(FileRow row)
    {
        var session = AssetSession.Current;
        if (session == null) return;

        var filePath = GetRelativeFilePath(row);
        if (filePath == null) return;

        var status = new StatusWindow(StatusType.Loading, $"Loading {row.Name}...");
        status.Show();

        var result = await session.LoadMeshAsync(filePath,
            msg => Dispatcher.Invoke(() => status.UpdateMessage(msg)));

        status.Close();

        if (result.IsFailure)
            new StatusWindow(StatusType.Error, $"Failed to load mesh:\n{result.Error}").Show();
    }

    /// <summary>
    /// Returns the path relative to the game's FilesPath prefix so AssetLoader can resolve it.
    /// e.g. "natives/stm/scene/master.scn.20" → "scene/master.scn.20"
    /// </summary>
    private static string? GetRelativeFilePath(FileRow row)
    {
        var pakEntry = row.TreeItemRef?.Metadata?.PakEntry;
        if (pakEntry == null) return null;

        var provider = App.CurrentProvider;
        if (provider == null) return null;

        var rawPath = pakEntry.Value.FilePath.Replace('\\', '/');
        var prefix  = provider.FilesPath.Replace('\\', '/').TrimEnd('/');

        return rawPath.StartsWith(prefix + "/", StringComparison.OrdinalIgnoreCase)
            ? rawPath[(prefix.Length + 1)..]
            : rawPath;
    }

    // ── Search ────────────────────────────────────────────────────────────────

    private void OnSearchKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            SearchBox.Text = string.Empty;
            if (_currentNode != null) LoadContents(_currentNode);
            _isSearchMode = false;
        }
    }

    private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
    {
        var text = SearchBox.Text.Trim();
        if (string.IsNullOrEmpty(text))
        {
            _isSearchMode = false;
            if (_currentNode != null) LoadContents(_currentNode);
            return;
        }

        _isSearchMode = true;
        var results = new List<FileRow>();
        CollectMatches(TreeManager.Instance.Root, text, results);

        Rows.Clear();
        foreach (var row in results.OrderBy(r => r.Name))
            Rows.Add(row);

        OpenLocationMenuItem.Visibility = Visibility.Visible;
    }

    private static void CollectMatches(TreeItem node, string text, List<FileRow> results)
    {
        foreach (var child in node.Children)
        {
            if (!child.IsFolder && child.Name.Contains(text, StringComparison.OrdinalIgnoreCase))
                results.Add(MakeRow(child));

            if (child.IsFolder)
                CollectMatches(child, text, results);
        }
    }

    // ── Context menu ──────────────────────────────────────────────────────────

    private void OnExportRaw(object sender, RoutedEventArgs e)
    {
        var row = FileGrid.SelectedItem as FileRow;
        if (row == null || row.IsFolder || row.TreeItemRef?.Metadata?.PakEntry == null) return;

        var pakEntry = row.TreeItemRef.Metadata.PakEntry.Value;
        var provider = App.CurrentProvider;
        if (provider == null) { ShowError("No game loaded."); return; }

        var pakFile = FindPakFile(pakEntry);
        if (pakFile == null) { ShowError("PAK file not found for this entry."); return; }

        var dlg = new SaveFileDialog
        {
            FileName   = row.Name,
            Filter     = "All Files (*.*)|*.*",
            DefaultExt = Path.GetExtension(row.Name),
        };

        if (dlg.ShowDialog() != true) return;

        try
        {
            var data = provider.PakReader.ExtractFile(pakFile, pakEntry);
            File.WriteAllBytes(dlg.FileName, data);
            new StatusWindow(StatusType.Success, $"Exported successfully!\nSize: {FormatSize(data.Length)}").Show();
        }
        catch (Exception ex)
        {
            ShowError($"Export failed: {ex.Message}");
        }
    }

    private void OnExtractDecompressed(object sender, RoutedEventArgs e) => OnExportRaw(sender, e);

    private void OnCopyName(object sender, RoutedEventArgs e)
    {
        if (FileGrid.SelectedItem is FileRow row)
            Clipboard.SetText(row.Name);
    }

    private void OnOpenFileLocation(object sender, RoutedEventArgs e)
    {
        if (FileGrid.SelectedItem is not FileRow row || row.TreeItemRef == null) return;
        var parent = FindParent(TreeManager.Instance.Root, row.TreeItemRef);
        if (parent == null) return;

        SearchBox.Text = string.Empty;
        _isSearchMode  = false;
        NavigateTo(parent);

        Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Loaded, () =>
        {
            var target = Rows.FirstOrDefault(r => r.Name == row.Name);
            if (target != null)
            {
                FileGrid.SelectedItem = target;
                FileGrid.ScrollIntoView(target);
            }
        });
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private static FileRow MakeRow(TreeItem item)
    {
        if (item.IsFolder)
        {
            return new FileRow
            {
                Name        = item.Name,
                ExtTag      = "DIR",
                TagColor    = new SolidColorBrush(Color.FromRgb(0x4a, 0x7f, 0xc8)),
                Type        = "Folder",
                IsFolder    = true,
                TreeItemRef = item,
            };
        }

        var ext   = GetRealExt(item.Name);
        var color = ExtColor(ext);
        var m     = item.Metadata;

        return new FileRow
        {
            Name                = item.Name,
            ExtTag              = ext.Length > 0 ? ext.ToUpperInvariant()[..Math.Min(ext.Length, 4)] : "FILE",
            TagColor            = new SolidColorBrush(color),
            Type                = ext.Length > 0 ? ext.ToUpperInvariant() + " File" : "File",
            Size                = m != null ? FormatSize(m.UncompressedSize) : "",
            SizeBytes           = m?.UncompressedSize ?? 0,
            CompressedSize      = m is { IsCompressed: true } ? FormatSize(m.CompressedSize) : (m != null ? "-" : ""),
            CompressedSizeBytes = m is { IsCompressed: true } ? m.CompressedSize : 0,
            Compression         = m?.Compression.ToString() ?? "",
            Checksum            = m != null ? $"0x{m.Checksum:X16}" : "",
            IsFolder            = false,
            TreeItemRef         = item,
        };
    }

    private static string GetRealExt(string name)
    {
        var parts = name.Split('.');
        return parts.Length >= 3 ? parts[^2] : (parts.Length >= 2 ? parts[^1] : string.Empty);
    }

    private static Color ExtColor(string ext) => ext.ToLowerInvariant() switch
    {
        "tex"            => Color.FromRgb(0x4a, 0x87, 0xc8),
        "mdf2"           => Color.FromRgb(0xc8, 0x8a, 0x4a),
        "pfb"            => Color.FromRgb(0xe5, 0x22, 0x22),
        "mesh"           => Color.FromRgb(0x4a, 0xc8, 0x8a),
        "scn"            => Color.FromRgb(0xc8, 0x4a, 0x87),
        "motlist" or "mot" => Color.FromRgb(0x87, 0x4a, 0xc8),
        "bnk" or "pck" or "wcc" or "wcbk" => Color.FromRgb(0xa0, 0x4a, 0xc8),
        _                => Color.FromRgb(0x60, 0x60, 0x60),
    };

    private static string FormatSize(long bytes)
    {
        string[] units = { "B", "KB", "MB", "GB" };
        double size = bytes;
        int i = 0;
        while (size >= 1024 && i < units.Length - 1) { size /= 1024; i++; }
        return $"{size:0.##} {units[i]}";
    }

    private static REAssetExplorer.Core.Pak.PakFile? FindPakFile(REAssetExplorer.Core.Pak.PakEntry entry)
    {
        foreach (var pak in GameLoader.LoadedPakFiles.Values)
        {
            if (pak.Entries.Any(e => e.FilePath == entry.FilePath))
                return pak;
        }
        return null;
    }

    private static TreeItem? FindParent(TreeItem root, TreeItem target)
    {
        foreach (var child in root.Children)
        {
            if (child == target) return root;
            var found = FindParent(child, target);
            if (found != null) return found;
        }
        return null;
    }

    private static void ShowError(string msg)
        => new StatusWindow(StatusType.Error, msg).Show();
}
