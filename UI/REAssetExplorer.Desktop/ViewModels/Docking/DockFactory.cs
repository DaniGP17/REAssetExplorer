using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace REAssetExplorer.Desktop.ViewModels.Docking;

public sealed class DockFactory : Factory
{
    public DockFactory(MainViewModel main)
    {
        Hierarchy = new HierarchyTool(main.Hierarchy);
        Viewport = new ViewportTool(main.Viewport);
        Inspector = new InspectorTool(main.Inspector);
        AssetBrowser = new AssetBrowserTool(main);
        Log = new LogTool(main);
        Tools = [Hierarchy, Viewport, Inspector, AssetBrowser, Log];
        // The Windows menu brings closed panels back from the root's hidden list.
        HideToolsOnClose = true;
    }

    public HierarchyTool Hierarchy { get; }
    public ViewportTool Viewport { get; }
    public InspectorTool Inspector { get; }
    public AssetBrowserTool AssetBrowser { get; }
    public LogTool Log { get; }
    public IReadOnlyList<PanelTool> Tools { get; }

    public override IRootDock CreateLayout()
    {
        var hierarchyDock = new ToolDock
        {
            Id = "HierarchyDock",
            Proportion = 0.22,
            Alignment = Alignment.Left,
            ActiveDockable = Hierarchy,
            VisibleDockables = CreateList<IDockable>(Hierarchy)
        };
        var viewportDock = new ToolDock
        {
            Id = "ViewportDock",
            Proportion = 0.78,
            ActiveDockable = Viewport,
            VisibleDockables = CreateList<IDockable>(Viewport)
        };
        var bottomDock = new ToolDock
        {
            Id = "BottomDock",
            Proportion = 0.34,
            Alignment = Alignment.Bottom,
            ActiveDockable = AssetBrowser,
            VisibleDockables = CreateList<IDockable>(AssetBrowser, Log)
        };
        var inspectorDock = new ToolDock
        {
            Id = "InspectorDock",
            Proportion = 0.22,
            Alignment = Alignment.Right,
            ActiveDockable = Inspector,
            VisibleDockables = CreateList<IDockable>(Inspector)
        };

        var top = new ProportionalDock
        {
            Proportion = 0.66,
            Orientation = Orientation.Horizontal,
            VisibleDockables = CreateList<IDockable>(hierarchyDock, new ProportionalDockSplitter(), viewportDock)
        };
        var center = new ProportionalDock
        {
            Proportion = 0.78,
            Orientation = Orientation.Vertical,
            VisibleDockables = CreateList<IDockable>(top, new ProportionalDockSplitter(), bottomDock)
        };
        var mainLayout = new ProportionalDock
        {
            Id = "MainLayout",
            Orientation = Orientation.Horizontal,
            VisibleDockables = CreateList<IDockable>(center, new ProportionalDockSplitter(), inspectorDock)
        };

        IRootDock root = CreateRootDock();
        root.Id = "Root";
        root.IsCollapsable = false;
        root.ActiveDockable = mainLayout;
        root.DefaultDockable = mainLayout;
        root.VisibleDockables = CreateList<IDockable>(mainLayout);
        root.LeftPinnedDockables = CreateList<IDockable>();
        root.RightPinnedDockables = CreateList<IDockable>();
        root.TopPinnedDockables = CreateList<IDockable>();
        root.BottomPinnedDockables = CreateList<IDockable>();
        return root;
    }

    public override void InitLayout(IDockable layout)
    {
        HostWindowLocator = new Dictionary<string, Func<IHostWindow?>>
        {
            [nameof(IDockWindow)] = () => new HostWindow()
        };
        base.InitLayout(layout);
    }
}
