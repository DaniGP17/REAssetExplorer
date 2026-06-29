using Avalonia.Controls;
using Avalonia.Controls.Templates;
using REAssetExplorer.Desktop.ViewModels.Docking;
using REAssetExplorer.Desktop.Views.Panels;

namespace REAssetExplorer.Desktop;

public sealed class ViewLocator : IDataTemplate
{
    public Control? Build(object? data) => data switch
    {
        HierarchyTool => new HierarchyView(),
        ViewportTool => new ViewportView(),
        InspectorTool => new InspectorView(),
        MessageTableTool => new MessageTableView(),
        GuiPreviewTool => new GuiPreviewView(),
        FsmGraphTool => new FsmGraphView(),
        AssetBrowserTool => new AssetBrowserView(),
        LogTool => new LogView(),
        _ => null
    };

    public bool Match(object? data) => data is PanelTool;
}
