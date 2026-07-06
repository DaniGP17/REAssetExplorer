using Avalonia.Controls;
using Avalonia.Platform;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Controls;

// Avalonia cannot draw over the native viewport, so overlays go next to it, not on top.
public sealed class ViewportHost(ViewportViewModel viewport) : NativeControlHost
{
    // The viewport may be destroyed while still shown; only the base class's own
    // placeholder may go back to the base class.
    private bool attached;

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        if (viewport.Handle == IntPtr.Zero) return base.CreateNativeControlCore(parent);
        attached = true;
        return new PlatformHandle(viewport.Attach(this, parent.Handle), "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        if (!attached)
        {
            base.DestroyNativeControlCore(control);
            return;
        }
        attached = false;
        viewport.Detach(this);
    }
}
