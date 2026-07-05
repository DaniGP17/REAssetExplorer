using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace REAssetExplorer.Desktop.ViewModels.Docking;

public sealed class AssetEditorDockFactory : Factory
{
    public AssetEditorDockFactory(AssetEditorViewModel editor)
    {
        Hierarchy = new HierarchyTool(editor.Hierarchy);
        Viewport = editor.Viewport != null ? new ViewportTool(editor.Viewport) : null;
        Inspector = new InspectorTool(editor.Inspector);
        Messages = editor.MessageTable != null ? new MessageTableTool(editor.MessageTable) { CanClose = false } : null;
        GuiPreview = editor.GuiPreview != null ? new GuiPreviewTool(editor.GuiPreview) { CanClose = false } : null;
        Graph = editor.FsmGraph != null ? new FsmGraphTool(editor.FsmGraph) { CanClose = false } : null;
        // The editor has no Window menu to bring a closed panel back.
        Hierarchy.CanClose = false;
        Inspector.CanClose = false;
        if (Viewport != null) Viewport.CanClose = false;
    }

    public HierarchyTool Hierarchy { get; }
    public ViewportTool? Viewport { get; }
    public InspectorTool Inspector { get; }
    public MessageTableTool? Messages { get; }
    public GuiPreviewTool? GuiPreview { get; }
    public FsmGraphTool? Graph { get; }

    private PanelTool? Center => (PanelTool?)GuiPreview ?? Graph;

    public override IRootDock CreateLayout()
    {
        var hierarchyDock = new ToolDock
        {
            Id = "HierarchyDock",
            Proportion = Viewport != null || Center != null ? 0.22 : 0.5,
            Alignment = Alignment.Left,
            ActiveDockable = Hierarchy,
            VisibleDockables = CreateList<IDockable>(Hierarchy)
        };
        var inspectorDock = new ToolDock
        {
            Id = "InspectorDock",
            Proportion = Viewport != null || Center != null ? 0.24 : 0.5,
            Alignment = Alignment.Right,
            ActiveDockable = Inspector,
            VisibleDockables = CreateList<IDockable>(Inspector)
        };
        if (Messages != null) inspectorDock.Proportion = 0.34;
        var mainLayout = new ProportionalDock
        {
            Id = "AssetEditorLayout",
            Orientation = Orientation.Horizontal,
            VisibleDockables = Messages != null
                ? CreateList<IDockable>(new ToolDock
                {
                    Id = "MessagesDock",
                    Proportion = 0.66,
                    ActiveDockable = Messages,
                    VisibleDockables = CreateList<IDockable>(Messages)
                }, new ProportionalDockSplitter(), inspectorDock)
                : Center != null
                ? CreateList<IDockable>(hierarchyDock, new ProportionalDockSplitter(), new ToolDock
                {
                    Id = "CenterDock",
                    Proportion = 0.54,
                    ActiveDockable = Center,
                    VisibleDockables = CreateList<IDockable>(Center)
                }, new ProportionalDockSplitter(), inspectorDock)
                : Viewport != null
                ? CreateList<IDockable>(hierarchyDock, new ProportionalDockSplitter(), new ToolDock
                {
                    Id = "ViewportDock",
                    Proportion = 0.54,
                    ActiveDockable = Viewport,
                    VisibleDockables = CreateList<IDockable>(Viewport)
                }, new ProportionalDockSplitter(), inspectorDock)
                : CreateList<IDockable>(hierarchyDock, new ProportionalDockSplitter(), inspectorDock)
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
