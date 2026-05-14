using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.TestUI.Controls;
using REAssetExplorer.TestUI.Controls.Docking;

namespace REAssetExplorer.TestUI;

public partial class MainWindow : Window
{
    // ── Drag state ──────────────────────────────────────────────────────────
    private FloatingPanel? _dragging;
    private Point          _dragOffset;
    private bool           _dragFromDocked;
    private int            _topZIndex = 1;

    // ── Panel refs (resolved at init; x:Name can't pierce FloatingPanel scope) ─
    private TreeView?        _hierarchyTree;
    private TextBlock?       _sceneNameLabel;
    private ObjectInspector? _objectInspector;
    private Controls.SceneViewHost? _sceneView;
    private HierarchyNode?  _selectedHierarchyNode;

    public MainWindow()
    {
        InitializeComponent();
        StateChanged += (_, _) => UpdateMaxRestoreButton();
        BuildInitialLayout();
        ResolveHierarchyRefs();

        if (AssetSession.Current is { } session)
        {
            session.SceneLoaded += OnSceneLoaded;
            session.MeshLoaded  += OnMeshLoaded;
        }

#if DEBUG
        Loaded += async (_, _) =>
        {
            if (AssetSession.Current is { } s)
                await s.LoadSceneAsync("environment/scene/chapter1/c01_2f.scn.20");
        };
#endif
    }

    // ── Initial layout ──────────────────────────────────────────────────────

    private void BuildInitialLayout()
    {
        // Panels are defined in FloatingCanvas for XAML x:Name resolution;
        // remove them and hand ownership to DockArea.
        FloatingCanvas.Children.Remove(HierarchyPanel);
        FloatingCanvas.Children.Remove(ScenePanel);
        FloatingCanvas.Children.Remove(InspectorPanel);
        FloatingCanvas.Children.Remove(AssetBrowserPanel);

        var hierarchyLeaf    = new PanelLeaf { Panel = HierarchyPanel };
        var sceneLeaf        = new PanelLeaf { Panel = ScenePanel };
        var inspectorLeaf    = new PanelLeaf { Panel = InspectorPanel };
        var assetBrowserLeaf = new PanelLeaf { Panel = AssetBrowserPanel };

        var rightSplit = new SplitNode
        {
            Orientation = SplitOrientation.Horizontal,
            Ratio       = 0.78,
            First       = sceneLeaf,
            Second      = inspectorLeaf,
        };
        var topSplit = new SplitNode
        {
            Orientation = SplitOrientation.Horizontal,
            Ratio       = 0.20,
            First       = hierarchyLeaf,
            Second      = rightSplit,
        };
        var root = new SplitNode
        {
            Orientation = SplitOrientation.Vertical,
            Ratio       = 0.72,
            First       = topSplit,
            Second      = assetBrowserLeaf,
        };

        DockArea.SetLayout(root);
    }

    // ── Panel drag ──────────────────────────────────────────────────────────

    private void OnHeaderDragStart(object sender, MouseButtonEventArgs e)
    {
        _dragging = (FloatingPanel)sender;

        if (DockArea.ContainsPanel(_dragging))
        {
            _dragFromDocked      = true;
            _dragging.Opacity    = 0.45;
        }
        else
        {
            _dragFromDocked = false;
            _dragOffset     = e.GetPosition(_dragging);
            Panel.SetZIndex(_dragging, ++_topZIndex);
        }

        e.Handled = true;
    }

    private void OnHeaderDragMove(object sender, MouseEventArgs e)
    {
        if (_dragging == null) return;

        var posInDockArea = e.GetPosition(DockArea);
        DockArea.UpdateDrag(posInDockArea, _dragging);

        if (!_dragFromDocked)
        {
            var pos = e.GetPosition(FloatingCanvas);
            Canvas.SetLeft(_dragging, Math.Max(0, pos.X - _dragOffset.X));
            Canvas.SetTop(_dragging,  Math.Max(0, pos.Y - _dragOffset.Y));
        }
    }

    private void OnHeaderDragEnd(object sender, MouseButtonEventArgs e)
    {
        if (_dragging == null) return;

        // Always restore opacity regardless of drop outcome
        _dragging.Opacity = 1.0;

        var posInDockArea = e.GetPosition(DockArea);
        bool dropped = DockArea.EndDrag(posInDockArea, _dragging);

        if (dropped)
        {
            // Remove from FloatingCanvas if it was floating
            if (!_dragFromDocked && FloatingCanvas.Children.Contains(_dragging))
                FloatingCanvas.Children.Remove(_dragging);

            DockArea.CommitDrop();
        }
        else if (_dragFromDocked)
        {
            // Released outside DockArea → undock and float
            var dockAreaBounds = new Rect(
                DockArea.TransformToVisual(this).Transform(new Point()),
                new Size(DockArea.ActualWidth, DockArea.ActualHeight));

            if (!dockAreaBounds.Contains(e.GetPosition(this)))
            {
                DockArea.RemovePanel(_dragging);
                var posInCanvas = e.GetPosition(FloatingCanvas);
                Canvas.SetLeft(_dragging, Math.Max(0, posInCanvas.X - _dragging.Width / 2));
                Canvas.SetTop(_dragging,  Math.Max(0, posInCanvas.Y - 15));
                FloatingCanvas.Children.Add(_dragging);
                Panel.SetZIndex(_dragging, ++_topZIndex);
            }
        }

        _dragging       = null;
        _dragFromDocked = false;
    }

    private void OnPanelClosed(object sender, RoutedEventArgs e)
    {
        var panel = (FloatingPanel)sender;

        if (DockArea.ContainsPanel(panel))
            DockArea.RemovePanel(panel);
        else
            panel.Visibility = Visibility.Collapsed;
    }

    // ── Hierarchy ────────────────────────────────────────────────────────────

    private void ResolveHierarchyRefs()
    {
        // x:Name can't pierce FloatingPanel's XAML name scope, so we resolve
        // the controls we need at runtime via the logical tree / PanelContent cast.
        if (HierarchyPanel.PanelContent is DockPanel dp)
        {
            _hierarchyTree  = dp.Children.OfType<TreeView>().FirstOrDefault();
            _sceneNameLabel = dp.Children.OfType<TextBlock>().FirstOrDefault();

            if (_hierarchyTree != null)
                _hierarchyTree.SelectedItemChanged += OnHierarchySelectionChanged;
        }

        _objectInspector = InspectorPanel.PanelContent as ObjectInspector;

        if (ScenePanel.PanelContent is DockPanel sceneDp)
        {
            _sceneView = sceneDp.Children.OfType<Controls.SceneViewHost>().FirstOrDefault();
            if (_sceneView != null)
                _sceneView.SceneObjectPicked += OnSceneObjectPicked;
        }
    }

    private void OnHierarchySelectionChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is HierarchyNode node)
            _objectInspector?.ShowNode(node);
        else
            _objectInspector?.Clear();
    }

    private void OnSceneObjectPicked(int nodeId)
    {
        Dispatcher.Invoke(() =>
        {
            if (_hierarchyTree?.ItemsSource is not IEnumerable<HierarchyNode> roots) return;

            var found = FindHierarchyNode(roots, nodeId);
            if (found == null) return;

            // Deselect the previously selected node (two-way binding handles the visual side,
            // but we still need to clear the old VM flag so it doesn't stay "selected" visually
            // in unexpanded subtrees).
            if (_selectedHierarchyNode != null && _selectedHierarchyNode != found)
                _selectedHierarchyNode.IsSelected = false;

            _selectedHierarchyNode = found;

            // Expand ancestors so the node is in the visual tree before selecting it.
            ExpandToNode(roots, found);
            found.IsSelected = true;
        });
    }

    private static HierarchyNode? FindHierarchyNode(IEnumerable<HierarchyNode> nodes, int nodeId)
    {
        foreach (var node in nodes)
        {
            if (node.SourceNode?.Id == nodeId) return node;
            var found = FindHierarchyNode(node.Children, nodeId);
            if (found != null) return found;
        }
        return null;
    }

    private static bool ExpandToNode(IEnumerable<HierarchyNode> nodes, HierarchyNode target)
    {
        foreach (var node in nodes)
        {
            if (node == target) return true;
            if (ExpandToNode(node.Children, target))
            {
                node.IsExpanded = true;
                return true;
            }
        }
        return false;
    }

    private void OnSceneLoaded(object? sender, SceneLoadedEventArgs e)
    {
        Dispatcher.Invoke(() =>
        {
            if (_sceneNameLabel != null)
                _sceneNameLabel.Text = string.IsNullOrEmpty(e.Scene.Name) ? "Scene" : e.Scene.Name;
            if (_hierarchyTree != null)
                _hierarchyTree.ItemsSource = HierarchyNode.BuildFromScene(e.Scene);

            _sceneView?.LoadScene(e.Scene);
        });
    }

    private void OnMeshLoaded(object? sender, MeshLoadedEventArgs e)
    {
        Dispatcher.Invoke(() =>
        {
            var displayName = string.IsNullOrEmpty(e.Mesh.Name)
                ? System.IO.Path.GetFileName(e.MeshPath)
                : e.Mesh.Name;

            if (_sceneNameLabel != null)
                _sceneNameLabel.Text = displayName;

            if (_hierarchyTree != null)
            {
                var nodes = HierarchyNode.BuildFromMesh(e.Mesh, displayName);
                _hierarchyTree.ItemsSource = nodes;
                if (nodes.Count > 0)
                {
                    nodes[0].IsSelected = true;
                    _objectInspector?.ShowNode(nodes[0]);
                }
            }

            _sceneView?.LoadMesh(e.MeshPath, e.MdfPath);
        });
    }

    // ── Window chrome ────────────────────────────────────────────────────────

    private void UpdateMaxRestoreButton()
    {
        RootPanel.Margin = WindowState == WindowState.Maximized
            ? new Thickness(SystemParameters.WindowResizeBorderThickness.Left +
                            SystemParameters.WindowResizeBorderThickness.Right - 1)
            : new Thickness(0);

        MaxRestoreButton.Content = WindowState == WindowState.Maximized ? "" : "";
        MaxRestoreButton.ToolTip = WindowState == WindowState.Maximized ? "Restaurar" : "Maximizar";
    }

    private void OnMinimize(object sender, RoutedEventArgs e)  => WindowState = WindowState.Minimized;
    private void OnMaxRestore(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void OnClose(object sender, RoutedEventArgs e) => Close();

    // ── Menu stubs ────────────────────────────────────────────────────────────

    private void OnOpenProject(object sender, RoutedEventArgs e) { }
    private void OnSave(object sender, RoutedEventArgs e)        { }
    private void OnSaveAll(object sender, RoutedEventArgs e)     { }
    private void OnExit(object sender, RoutedEventArgs e)        => Close();
    private void OnPreferences(object sender, RoutedEventArgs e) { }
    private void OnToggleFullScreen(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void OnBuild(object sender, RoutedEventArgs e)         { }
    private void OnBuildAndRun(object sender, RoutedEventArgs e)   { }
    private void OnBuildSettings(object sender, RoutedEventArgs e) { }
    private void OnImportAsset(object sender, RoutedEventArgs e)   { }
    private void OnAbout(object sender, RoutedEventArgs e) =>
        MessageBox.Show("REAssetExplorer\nRE Engine Asset Viewer", "About",
                        MessageBoxButton.OK, MessageBoxImage.Information);
}
