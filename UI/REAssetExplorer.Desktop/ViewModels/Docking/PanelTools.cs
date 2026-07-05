using Dock.Model.Mvvm.Controls;

namespace REAssetExplorer.Desktop.ViewModels.Docking;

public abstract class PanelTool : Tool
{
    protected PanelTool(string id, string title)
    {
        Id = id;
        Title = title;
        CanClose = true;
        CanFloat = true;
        CanPin = true;
    }
}

public sealed class HierarchyTool(HierarchyViewModel hierarchy) : PanelTool("Hierarchy", "Hierarchy")
{
    public HierarchyViewModel Hierarchy { get; } = hierarchy;
}

public sealed class ViewportTool : PanelTool
{
    public ViewportTool(ViewportViewModel viewport) : base("Viewport", "Viewport")
    {
        Viewport = viewport;
        // Auto-hide slides panels over their neighbours, which a native window cannot do.
        CanPin = false;
    }

    public ViewportViewModel Viewport { get; }
}

public sealed class InspectorTool(InspectorViewModel inspector) : PanelTool("Inspector", "Inspector")
{
    public InspectorViewModel Inspector { get; } = inspector;
}

public sealed class MessageTableTool(MessageTableViewModel messages) : PanelTool("Messages", "Messages")
{
    public MessageTableViewModel Messages { get; } = messages;
}

public sealed class GuiPreviewTool(GuiPreviewViewModel preview) : PanelTool("GuiPreview", "Preview")
{
    public GuiPreviewViewModel Preview { get; } = preview;
}

public sealed class FsmGraphTool(FsmGraphViewModel graph) : PanelTool("FsmGraph", "Graph")
{
    public FsmGraphViewModel Graph { get; } = graph;
}

public sealed class AssetBrowserTool(MainViewModel main) : PanelTool("AssetBrowser", "Asset Browser")
{
    public MainViewModel Main { get; } = main;
}

public sealed class LogTool(MainViewModel main) : PanelTool("Log", "Log")
{
    public MainViewModel Main { get; } = main;
}
