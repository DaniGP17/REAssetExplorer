using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using REAssetExplorer.Desktop.Controls;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.Views.Panels;

public partial class AssetBrowserView : UserControl
{
    private AssetBrowserViewModel? subscribed;

    public AssetBrowserView()
    {
        InitializeComponent();
        TreeKeys.Attach<AssetNode>(FolderList, n => n.HasSubfolders, n => n.Subfolders.FirstOrDefault(),
                                   n => n.Parent is { Depth: >= 0 } p ? p : null,
                                   n => Browser?.ExpandFolder(n), n => Browser?.CollapseFolder(n));
    }

    private MainViewModel? Main => (DataContext as AssetBrowserTool)?.Main;
    private AssetBrowserViewModel? Browser => Main?.Browser;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Subscribe(Browser);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Subscribe(null);
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (TopLevel.GetTopLevel(this) != null) Subscribe(Browser);
        AssetList.ContextMenu = Main is { } main ? CreateAssetMenu(main) : null;
    }

    private void Subscribe(AssetBrowserViewModel? browser)
    {
        if (subscribed != null) subscribed.PropertyChanged -= OnBrowserPropertyChanged;
        subscribed = browser;
        if (subscribed != null) subscribed.PropertyChanged += OnBrowserPropertyChanged;
    }

    private void OnBrowserPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (Browser is not { } browser) return;
        if (e.PropertyName == nameof(AssetBrowserViewModel.CurrentFolder) && browser.CurrentFolder is { } folder)
            FolderList.ScrollIntoView(folder);
        if (e.PropertyName == nameof(AssetBrowserViewModel.SelectedAsset) && browser.SelectedAsset is { } asset &&
            AssetList.SelectedItem != asset)
        {
            AssetList.SelectedItem = asset;
            AssetList.ScrollIntoView(asset);
        }
    }

    private static ContextMenu CreateAssetMenu(MainViewModel main) => new()
    {
        Items =
        {
            new MenuItem { Header = "Open", Command = main.OpenSelectedCommand },
            new MenuItem { Header = "Extract...", Command = main.ExtractSelectedCommand },
            new Separator(),
            new MenuItem { Header = "Copy Pak Path", Command = main.CopyPathCommand }
        }
    };

    private void OnAssetListSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (AssetList.SelectedItem is AssetNode node) Browser?.Select(node);
    }

    private void OnAssetListDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (AssetList.SelectedItem is AssetNode node) Browser?.Activate(node);
    }

    private void OnBreadcrumbClick(object? sender, RoutedEventArgs e)
    {
        if (sender is Control { DataContext: AssetNode node }) Browser?.Navigate(node);
    }

    private void OnFolderChevronTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: AssetNode node }) Browser?.ToggleFolder(node);
        e.Handled = true;
    }

    private void OnFolderDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (FolderList.SelectedItem is AssetNode node) Browser?.ToggleFolder(node);
    }
}
