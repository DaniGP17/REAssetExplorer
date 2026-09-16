using Avalonia;
using Avalonia.Controls;
using REAssetExplorer.Desktop.ViewModels;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.Views.Panels;

public partial class LogView : UserControl
{
    private LogViewModel? subscribed;

    public LogView()
    {
        InitializeComponent();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Subscribe((DataContext as LogTool)?.Main.Log);
        ScrollToEnd();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Subscribe(null);
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (TopLevel.GetTopLevel(this) != null) Subscribe((DataContext as LogTool)?.Main.Log);
    }

    private void Subscribe(LogViewModel? log)
    {
        if (subscribed != null) subscribed.EntriesAdded -= ScrollToEnd;
        subscribed = log;
        if (subscribed != null) subscribed.EntriesAdded += ScrollToEnd;
    }

    private void ScrollToEnd()
    {
        if (subscribed is { FilteredEntries.Count: > 0 } log) LogList.ScrollIntoView(log.FilteredEntries.Count - 1);
    }
}
