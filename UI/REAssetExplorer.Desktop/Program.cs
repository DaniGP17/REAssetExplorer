using System.Globalization;
using System.Runtime.InteropServices;
using Avalonia;
using Dock.Settings;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop;

internal static class Program
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint GetModuleHandleW(string? moduleName);

    [STAThread]
    public static void Main(string[] args)
    {
        CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Diagnostics.Install();
        // The native viewport HWND covers anything Avalonia draws over it, so dock
        // targets and auto-hidden panels need their own windows.
        DockSettings.UseFloatingDockAdorner = true;
        DockSettings.UsePinnedDockWindow = true;
        DockSettings.ShowDockablePreviewOnDrag = false;
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
    {
        AppBuilder builder = AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
        // WinUIComposition/DirectComposition create their own D3D11 device for the window surface;
        // RenderDoc's hook on that call crashes coreclr before the window ever appears. RenderDoc
        // (global hook or "Launch Application") injects renderdoc.dll before Main runs, so its
        // presence here means the process is hooked and needs the D3D-free redirection surface.
        bool renderDocAttached = GetModuleHandleW("renderdoc.dll") != 0;
        if (renderDocAttached || Environment.GetEnvironmentVariable("RAE_RENDERDOC") != null) {
            builder = builder.With(new Win32PlatformOptions { CompositionMode = [Win32CompositionMode.RedirectionSurface] });
        }
        return builder;
    }
}
