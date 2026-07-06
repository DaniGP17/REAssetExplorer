using Avalonia.Controls;
using REAssetExplorer.Desktop.Controls;
using REAssetExplorer.Desktop.ViewModels;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.Views.Panels;

public partial class ViewportView : UserControl
{
    private ViewportViewModel? hosted;

    public ViewportView()
    {
        InitializeComponent();
    }

    // Created in code, not bound in XAML: the host needs its viewport before it is attached.
    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        ViewportViewModel? viewport = (DataContext as ViewportTool)?.Viewport;
        if (viewport == hosted) return;
        hosted = viewport;
        HostSlot.Children.Clear();
        if (viewport == null) return;
        HostSlot.Children.Add(new ViewportHost(viewport));
    }

    private void OnCameraFlyoutOpened(object? sender, EventArgs e) => hosted?.RefreshCamera();
}
