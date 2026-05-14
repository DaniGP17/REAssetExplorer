using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using REAssetExplorer.TestUI.Controls.Docking;

namespace REAssetExplorer.TestUI.Controls;

public class DockingArea : Grid
{
    private LayoutNode _root = new PanelLeaf();
    private readonly Dictionary<PanelLeaf, Grid> _leafGrids = new();

    // Drop preview overlay
    private readonly Canvas _overlay = new() { IsHitTestVisible = false };
    private readonly Border _dropPreview = new()
    {
        Background       = new SolidColorBrush(Color.FromArgb(90,  86, 159, 255)),
        BorderBrush      = new SolidColorBrush(Color.FromArgb(220, 86, 159, 255)),
        BorderThickness  = new Thickness(2),
        IsHitTestVisible = false,
        Visibility       = Visibility.Collapsed,
    };

    private (PanelLeaf Leaf, DropZone Zone)? _pendingDrop;
    private bool _pendingRebuild;

    public DockingArea()
    {
        Panel.SetZIndex(_overlay, 1000);
        _overlay.Children.Add(_dropPreview);
        Children.Add(_overlay);
    }

    // ── Public API ──────────────────────────────────────────────────────────

    public void SetLayout(LayoutNode root)
    {
        AssignParents(root, null);
        _root = root;
        RebuildLayout();
    }

    public bool ContainsPanel(FloatingPanel panel) =>
        _leafGrids.Keys.Any(l => l.Panel == panel);

    public void UpdateDrag(Point posInDockArea, FloatingPanel dragging)
    {
        _pendingDrop = FindDropZone(posInDockArea, dragging);
        if (_pendingDrop.HasValue)
            ShowPreview(_pendingDrop.Value.Leaf, _pendingDrop.Value.Zone);
        else
            HidePreview();
    }

    // Returns true if drop was registered — caller must call CommitDrop after
    // removing the panel from any external container (e.g. FloatingCanvas).
    public bool EndDrag(Point posInDockArea, FloatingPanel dragging)
    {
        HidePreview();
        var drop = _pendingDrop;
        _pendingDrop = null;

        if (!drop.HasValue) return false;

        var source = _leafGrids.Keys.FirstOrDefault(l => l.Panel == dragging);
        var target = drop.Value.Leaf;

        if (source == target) return false;

        if (source != null)
            RemoveLeafFromTree(source);

        var leaf = source ?? new PanelLeaf { Panel = dragging };
        SplitLeaf(leaf, target, drop.Value.Zone);

        _pendingRebuild = true;
        return true;
    }

    public void CommitDrop()
    {
        if (!_pendingRebuild) return;
        _pendingRebuild = false;
        RebuildLayout();
    }

    public void RemovePanel(FloatingPanel panel)
    {
        var leaf = _leafGrids.Keys.FirstOrDefault(l => l.Panel == panel);
        if (leaf == null) return;

        RemoveLeafFromTree(leaf);
        RebuildLayout();

        panel.Width    = 380;
        panel.Height   = 600;
        panel.IsDocked = false;
    }

    // ── Layout building ─────────────────────────────────────────────────────

    public void RebuildLayout()
    {
        // Detach panels from their leaf grids before tearing down the visual tree
        foreach (var (leaf, grid) in _leafGrids)
            if (leaf.Panel != null)
                grid.Children.Remove(leaf.Panel);

        for (int i = Children.Count - 1; i >= 0; i--)
            if (Children[i] != _overlay)
                Children.RemoveAt(i);

        _leafGrids.Clear();

        var ui = BuildUI(_root);
        Children.Insert(0, ui);
    }

    private static bool IsLeafCompact(LayoutNode n) =>
        n is PanelLeaf pl && pl.Panel?.IsCompact == true;

    private FrameworkElement BuildUI(LayoutNode node)
    {
        if (node is PanelLeaf leaf)
        {
            var g = new Grid();
            _leafGrids[leaf] = g;
            if (leaf.Panel != null)
            {
                leaf.Panel.IsDocked = true;
                if (!leaf.Panel.IsCompact)
                {
                    leaf.Panel.Width               = double.NaN;
                    leaf.Panel.Height              = double.NaN;
                    leaf.Panel.VerticalAlignment   = VerticalAlignment.Stretch;
                    leaf.Panel.HorizontalAlignment = HorizontalAlignment.Stretch;
                }
                g.Children.Add(leaf.Panel);
            }
            return g;
        }

        var split  = (SplitNode)node;
        var grid   = new Grid();

        // Tell direct leaf children which compact orientation/position applies to them
        bool sideCompact = split.Orientation == SplitOrientation.Horizontal;
        if (split.First  is PanelLeaf fl && fl.Panel != null)
        {
            fl.Panel.IsCompactSide        = sideCompact;
            fl.Panel.IsCompactSecondChild = false;
        }
        if (split.Second is PanelLeaf sl && sl.Panel != null)
        {
            sl.Panel.IsCompactSide        = sideCompact;
            sl.Panel.IsCompactSecondChild = true;
        }

        var first  = BuildUI(split.First);
        var second = BuildUI(split.Second);
        var sp     = MakeSplitter(split.Orientation);

        bool fc = IsLeafCompact(split.First);
        bool sc = IsLeafCompact(split.Second);

        if (split.Orientation == SplitOrientation.Horizontal)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = fc ? GridLength.Auto : new GridLength(split.Ratio,     GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(5) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = sc ? GridLength.Auto : new GridLength(1 - split.Ratio, GridUnitType.Star) });
            Grid.SetColumn(first,  0);
            Grid.SetColumn(sp,     1);
            Grid.SetColumn(second, 2);
        }
        else
        {
            grid.RowDefinitions.Add(new RowDefinition { Height = fc ? GridLength.Auto : new GridLength(split.Ratio,     GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(5) });
            grid.RowDefinitions.Add(new RowDefinition { Height = sc ? GridLength.Auto : new GridLength(1 - split.Ratio, GridUnitType.Star) });
            Grid.SetRow(first,  0);
            Grid.SetRow(sp,     1);
            Grid.SetRow(second, 2);
        }

        grid.Children.Add(first);
        grid.Children.Add(sp);
        grid.Children.Add(second);
        return grid;
    }

    private static GridSplitter MakeSplitter(SplitOrientation orientation)
    {
        var s = new GridSplitter
        {
            ResizeBehavior = GridResizeBehavior.PreviousAndNext,
            ShowsPreview   = false,
            Background     = new SolidColorBrush(Color.FromRgb(0x11, 0x11, 0x11)),
        };
        if (orientation == SplitOrientation.Horizontal)
        {
            s.Width               = 5;
            s.ResizeDirection     = GridResizeDirection.Columns;
            s.HorizontalAlignment = HorizontalAlignment.Stretch;
            s.VerticalAlignment   = VerticalAlignment.Stretch;
        }
        else
        {
            s.Height              = 5;
            s.ResizeDirection     = GridResizeDirection.Rows;
            s.HorizontalAlignment = HorizontalAlignment.Stretch;
            s.VerticalAlignment   = VerticalAlignment.Stretch;
        }
        return s;
    }

    // ── Tree operations ─────────────────────────────────────────────────────

    private static void AssignParents(LayoutNode node, SplitNode? parent)
    {
        node.Parent = parent;
        if (node is SplitNode split)
        {
            AssignParents(split.First,  split);
            AssignParents(split.Second, split);
        }
    }

    private void RemoveLeafFromTree(PanelLeaf leaf)
    {
        if (_root == leaf)
        {
            _root = new PanelLeaf();
            return;
        }

        var parent  = (SplitNode)leaf.Parent!;
        var sibling = parent.First == leaf ? parent.Second : parent.First;
        sibling.Parent = parent.Parent;

        if (parent.Parent == null)
            _root = sibling;
        else
        {
            var gp = (SplitNode)parent.Parent;
            if (gp.First == parent) gp.First = sibling;
            else                    gp.Second = sibling;
        }
    }

    private void SplitLeaf(PanelLeaf toInsert, PanelLeaf target, DropZone zone)
    {
        var oldParent   = target.Parent;
        bool horizontal = zone is DropZone.Left or DropZone.Right;
        bool first      = zone is DropZone.Left or DropZone.Top;

        var split = new SplitNode
        {
            Orientation = horizontal ? SplitOrientation.Horizontal : SplitOrientation.Vertical,
            Ratio       = 0.5,
            Parent      = oldParent,
        };
        split.First  = first ? toInsert : (LayoutNode)target;
        split.Second = first ? (LayoutNode)target : toInsert;
        toInsert.Parent = split;
        target.Parent   = split;

        if (oldParent == null)
            _root = split;
        else
        {
            if (oldParent.First == target) oldParent.First = split;
            else                           oldParent.Second = split;
        }
    }

    // ── Drop hit-test and preview ───────────────────────────────────────────

    private (PanelLeaf Leaf, DropZone Zone)? FindDropZone(Point pos, FloatingPanel dragging)
    {
        foreach (var (leaf, grid) in _leafGrids)
        {
            if (leaf.Panel == dragging) continue;
            if (grid.ActualWidth < 1 || grid.ActualHeight < 1) continue;

            Point  origin = grid.TransformToAncestor(this).Transform(new Point(0, 0));
            var    bounds = new Rect(origin, new Size(grid.ActualWidth, grid.ActualHeight));
            if (!bounds.Contains(pos)) continue;

            double rx   = (pos.X - bounds.X) / bounds.Width;
            double ry   = (pos.Y - bounds.Y) / bounds.Height;
            const double edge = 0.3;

            var zone = rx < edge     ? DropZone.Left
                     : rx > 1 - edge ? DropZone.Right
                     : ry < edge     ? DropZone.Top
                                     : DropZone.Bottom;
            return (leaf, zone);
        }
        return null;
    }

    private void ShowPreview(PanelLeaf leaf, DropZone zone)
    {
        if (!_leafGrids.TryGetValue(leaf, out var grid)) return;

        Point  origin = grid.TransformToAncestor(this).Transform(new Point(0, 0));
        double w = grid.ActualWidth, h = grid.ActualHeight;

        Rect r = zone switch
        {
            DropZone.Left   => new Rect(origin.X,           origin.Y,           w * 0.5, h),
            DropZone.Right  => new Rect(origin.X + w * 0.5, origin.Y,           w * 0.5, h),
            DropZone.Top    => new Rect(origin.X,           origin.Y,           w,        h * 0.5),
            _               => new Rect(origin.X,           origin.Y + h * 0.5, w,        h * 0.5),
        };

        Canvas.SetLeft(_dropPreview, r.X);
        Canvas.SetTop(_dropPreview,  r.Y);
        _dropPreview.Width      = r.Width;
        _dropPreview.Height     = r.Height;
        _dropPreview.Visibility = Visibility.Visible;
    }

    private void HidePreview() => _dropPreview.Visibility = Visibility.Collapsed;
}
