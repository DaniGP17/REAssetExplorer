using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using REAssetExplorer.Desktop.Controls;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.Views.Panels;

public partial class HierarchyView : UserControl
{
    private HierarchyViewModel? subscribed;
    // Row under the last press; a Ctrl or Shift click makes it the active node.
    private OutlineNode? pressed;
    private bool syncing;
    private bool hideKeyDown;

    public HierarchyView()
    {
        InitializeComponent();
        TreeKeys.Attach<OutlineNode>(HierarchyList, n => n.CanExpand, n => n.Children.FirstOrDefault(), n => n.Parent,
                                     n => Hierarchy?.Expand(n), n => Hierarchy?.Collapse(n));
        // Tunnel: seen before the row turns the press into a selection.
        HierarchyList.AddHandler(PointerPressedEvent, (_, e) => pressed = (e.Source as StyledElement)?.DataContext as OutlineNode,
                                 RoutingStrategies.Tunnel);
        HierarchyList.AddHandler(KeyDownEvent, OnHierarchyKeyDown, RoutingStrategies.Tunnel);
        HierarchyList.AddHandler(KeyUpEvent, (_, e) =>
        {
            if (e.Key == Key.H) hideKeyDown = false;
        }, RoutingStrategies.Tunnel);
        HierarchyList.SelectionChanged += OnSelectionChanged;
    }

    private HierarchyViewModel? Hierarchy => (DataContext as HierarchyTool)?.Hierarchy;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Subscribe();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        if (subscribed != null) subscribed.SelectionRequested -= OnSelectionRequested;
        subscribed = null;
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (VisualRoot != null) Subscribe();
    }

    // Docking can recreate the view, so it restores the selection the model kept.
    private void Subscribe()
    {
        if (subscribed == Hierarchy) return;
        if (subscribed != null) subscribed.SelectionRequested -= OnSelectionRequested;
        subscribed = Hierarchy;
        if (subscribed == null) return;
        subscribed.SelectionRequested += OnSelectionRequested;
        syncing = true;
        try
        {
            HierarchyList.SelectedItems?.Clear();
            foreach (OutlineNode node in subscribed.SelectedNodes) HierarchyList.SelectedItems?.Add(node);
        }
        finally
        {
            syncing = false;
        }
    }

    private void OnSelectionRequested(OutlineNode? node)
    {
        syncing = true;
        try
        {
            HierarchyList.SelectedItem = node;
        }
        finally
        {
            syncing = false;
        }
    }

    private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (syncing || Hierarchy is not { } hierarchy) return;
        List<OutlineNode> selected = HierarchyList.SelectedItems?.OfType<OutlineNode>().ToList() ?? [];
        OutlineNode? active = pressed != null && selected.Contains(pressed)
            ? pressed
            : e.AddedItems.OfType<OutlineNode>().LastOrDefault() ??
              (hierarchy.SelectedNode is { } current && selected.Contains(current) ? current : selected.FirstOrDefault());
        pressed = null;
        hierarchy.SetSelection(active, selected);
    }

    private void OnOutlineChevronTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: OutlineNode node }) Hierarchy?.Toggle(node);
        e.Handled = true;
    }

    private void OnEyeClick(object? sender, RoutedEventArgs e)
    {
        if (sender is Control { DataContext: OutlineNode node }) Hierarchy?.ToggleVisibility(node);
    }

    private void OnHierarchyDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (Hierarchy?.SelectedNode is not { } node) return;
        Hierarchy.Activate(node);
        Hierarchy.Toggle(node);
    }

    private void OnHierarchyKeyDown(object? sender, KeyEventArgs e)
    {
        if (Hierarchy is not { } hierarchy || e.KeyModifiers != KeyModifiers.None) return;
        if (e.Key == Key.H)
        {
            // Held down, the key repeats: one toggle per press.
            if (!hideKeyDown) hierarchy.ToggleSelectedVisibility();
            hideKeyDown = true;
            e.Handled = true;
        }
        else if (e.Key == Key.Space && hierarchy.SelectedNode is { } node)
        {
            hierarchy.Activate(node);
            e.Handled = true;
        }
    }
}
