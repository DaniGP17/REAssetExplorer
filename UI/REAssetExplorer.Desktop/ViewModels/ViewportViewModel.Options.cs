using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed class ViewModeItem(ViewportViewModel owner, ViewportShading mode) : ObservableObject
{
    public ViewportShading Mode { get; } = mode;
    public string Name { get; } = ViewportViewModel.DisplayName(mode);

    public bool IsSelected
    {
        get => owner.Shading == Mode;
        set
        {
            if (value) owner.Shading = Mode;
        }
    }

    public void Refresh() => OnPropertyChanged(nameof(IsSelected));
}

public sealed record ViewModeGroup(string Title, IReadOnlyList<ViewModeItem> Items);

public sealed record ViewOption(string Name, int View);

public sealed record LodOption(string Name, int Lod);

// The viewport toolbar's menus: view mode, camera, Show flags, rendering options and the editing tools.
public sealed partial class ViewportViewModel
{
    private static readonly (string Title, ViewportShading[] Modes)[] ViewModeLayout =
    [
        ("Lighting", [ViewportShading.Lit, ViewportShading.Unlit, ViewportShading.Wireframe, ViewportShading.LightingOnly]),
        ("Buffer visualization", [ViewportShading.BaseColor, ViewportShading.Normals, ViewportShading.Roughness,
            ViewportShading.Metallic, ViewportShading.Occlusion, ViewportShading.Emissive, ViewportShading.Depth,
            ViewportShading.Velocity]),
        ("Optimization", [ViewportShading.LodColoration, ViewportShading.LightComplexity, ViewportShading.Overdraw]),
        ("Scene data", [ViewportShading.Collision, ViewportShading.AiMap])
    ];

    public static IReadOnlyList<ViewOption> ViewOptions { get; } =
    [
        new("Perspective", 0), new("Top", 1), new("Bottom", 2), new("Left", 3), new("Right", 4), new("Front", 5), new("Back", 6)
    ];

    public static IReadOnlyList<LodOption> LodOptions { get; } =
    [
        new("Auto", -1), new("LOD 0", 0), new("LOD 1", 1), new("LOD 2", 2), new("LOD 3", 3), new("LOD 4", 4), new("LOD 5", 5)
    ];

    public static IReadOnlyList<int> BookmarkSlots { get; } = [1, 2, 3, 4, 5, 6, 7, 8, 9];

    public static IReadOnlyList<string> ExposureModes { get; } = ["Scene (auto)", "Fixed EV"];

    private IReadOnlyList<ViewModeGroup>? viewModeGroups;
    private uint bookmarkMask;
    private bool syncingView;

    public IReadOnlyList<ViewModeGroup> ViewModeGroups =>
        viewModeGroups ??= ViewModeLayout
            .Select(g => new ViewModeGroup(g.Title, g.Modes.Where(ShadingModes.Contains).Select(m => new ViewModeItem(this, m)).ToList()))
            .Where(g => g.Items.Count > 0)
            .ToList();

    public string ShadingLabel => DisplayName(Shading);

    partial void OnShadingModesChanged(IReadOnlyList<ViewportShading> value)
    {
        viewModeGroups = null;
        OnPropertyChanged(nameof(ViewModeGroups));
    }

    // Camera

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ViewName), nameof(IsPerspective))]
    private ViewOption view = ViewOptions[0];

    public string ViewName => View.Name;
    public bool IsPerspective => View.View == 0;

    partial void OnViewChanged(ViewOption value)
    {
        if (!syncingView && Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_view(Handle, value.View);
    }

    [ObservableProperty]
    private double fieldOfView = 60;

    partial void OnFieldOfViewChanged(double value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_fov(Handle, (float)value);
    }

    // Unreal's camera speed scale: a log slider over 0.01 - 1000 m/s.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraSpeedText), nameof(CameraSpeedSlider))]
    private double cameraSpeed = 1;

    public string CameraSpeedText => CameraSpeed >= 10 ? CameraSpeed.ToString("0") : CameraSpeed >= 1 ? CameraSpeed.ToString("0.#") : CameraSpeed.ToString("0.##");

    public double CameraSpeedSlider
    {
        get => Math.Log10(Math.Max(CameraSpeed, 0.01));
        set => ChangeCameraSpeed(Math.Pow(10, value));
    }

    private void ChangeCameraSpeed(double speed)
    {
        CameraSpeed = Math.Clamp(speed, 0.01, 1000);
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_camera_speed(Handle, (float)CameraSpeed);
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsBookmarkSaved))]
    private int bookmarkSlot = 1;

    public bool IsBookmarkSaved => (bookmarkMask & (1u << BookmarkSlot)) != 0;

    [RelayCommand]
    private void GoToBookmark()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_bookmark(Handle, BookmarkSlot, 0);
    }

    [RelayCommand]
    private void SaveBookmark()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_bookmark(Handle, BookmarkSlot, 1);
    }

    // Show flags, as rae_viewport_set_show_flags.

    [ObservableProperty] private bool showStaticMeshes = true;
    [ObservableProperty] private bool showSkinnedMeshes = true;
    [ObservableProperty] private bool showDecals = true;
    [ObservableProperty] private bool showEffects = true;
    [ObservableProperty] private bool showFog = true;
    [ObservableProperty] private bool showVolumetricFog = true;
    [ObservableProperty] private bool showShadows = true;
    [ObservableProperty] private bool showPostProcess = true;
    [ObservableProperty] private bool showSky = true;
    [ObservableProperty] private bool showLocalCubemaps = true;

    partial void OnShowStaticMeshesChanged(bool value) => ApplyShowFlags();
    partial void OnShowSkinnedMeshesChanged(bool value) => ApplyShowFlags();
    partial void OnShowDecalsChanged(bool value) => ApplyShowFlags();
    partial void OnShowEffectsChanged(bool value) => ApplyShowFlags();
    partial void OnShowFogChanged(bool value) => ApplyShowFlags();
    partial void OnShowVolumetricFogChanged(bool value) => ApplyShowFlags();
    partial void OnShowShadowsChanged(bool value) => ApplyShowFlags();
    partial void OnShowPostProcessChanged(bool value) => ApplyShowFlags();
    partial void OnShowSkyChanged(bool value) => ApplyShowFlags();
    partial void OnShowLocalCubemapsChanged(bool value) => ApplyShowFlags();

    private void ApplyShowFlags()
    {
        if (Handle == IntPtr.Zero) return;
        bool[] flags = [ShowStaticMeshes, ShowSkinnedMeshes, ShowDecals, ShowEffects, ShowFog, ShowVolumetricFog, ShowShadows,
            ShowPostProcess, ShowSky, ShowLocalCubemaps];
        uint mask = 0;
        for (int i = 0; i < flags.Length; i++) mask |= flags[i] ? 1u << i : 0u;
        NativeMethods.rae_viewport_set_show_flags(Handle, mask);
    }

    [ObservableProperty]
    private LodOption forcedLod = LodOptions[0];

    [ObservableProperty]
    private bool streamAllZones;

    partial void OnForcedLodChanged(LodOption value) => ApplyLod();
    partial void OnStreamAllZonesChanged(bool value) => ApplyLod();

    private void ApplyLod()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_lod(Handle, ForcedLod.Lod, StreamAllZones ? 1 : 0);
    }

    // Rendering options

    [ObservableProperty]
    private bool realtime = true;

    partial void OnRealtimeChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_realtime(Handle, value ? 1 : 0);
    }

    [ObservableProperty]
    private bool temporalAA = true;

    partial void OnTemporalAAChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_temporal_aa(Handle, value ? 1 : 0);
    }

    // Percent of the viewport's size the frame is rendered at.
    [ObservableProperty]
    private double renderScale = 100;

    partial void OnRenderScaleChanged(double value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_render_scale(Handle, (float)(value / 100));
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsFixedExposure))]
    private int exposureMode;

    public bool IsFixedExposure => ExposureMode == 1;

    [ObservableProperty]
    private double exposureEv = 0;

    partial void OnExposureModeChanged(int value) => ApplyExposure();
    partial void OnExposureEvChanged(double value) => ApplyExposure();

    private void ApplyExposure()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_exposure(Handle, ExposureMode, (float)ExposureEv);
    }

    // Saved PNGs go to Pictures/REAssetExplorer, as Unreal's to Saved/Screenshots.
    [RelayCommand]
    private Task Screenshot() => SaveScreenshotAsync(false);

    [RelayCommand]
    private Task HighResScreenshot() => SaveScreenshotAsync(true);

    private async Task SaveScreenshotAsync(bool highRes)
    {
        if (Handle == IntPtr.Zero) return;
        IntPtr viewport = Handle;
        double previousScale = RenderScale;
        if (highRes) RenderScale = 200;
        try
        {
            (byte[]? pixels, int width, int height) = await Task.Run(() =>
            {
                // The first frame after the call may still have the old size.
                if (highRes) NativeMethods.rae_viewport_capture(viewport, 5000, out _, out _);
                IntPtr data = NativeMethods.rae_viewport_capture(viewport, 5000, out int w, out int h);
                if (data == IntPtr.Zero || w <= 0 || h <= 0) return ((byte[]?)null, 0, 0);
                byte[] copy = new byte[w * h * 4];
                Marshal.Copy(data, copy, 0, copy.Length);
                return (copy, w, h);
            });
            if (pixels == null)
            {
                NativeLog.Write(LogLevel.Warning, "Screenshot: the viewport did not render a frame");
                return;
            }
            string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures), "REAssetExplorer");
            Directory.CreateDirectory(folder);
            string file = Path.Combine(folder, $"Viewport_{DateTime.Now:yyyyMMdd_HHmmss}.png");
            using (var bitmap = new WriteableBitmap(new PixelSize(width, height), new Vector(96, 96), PixelFormat.Rgba8888,
                                                    AlphaFormat.Opaque))
            {
                using (ILockedFramebuffer buffer = bitmap.Lock())
                {
                    for (int y = 0; y < height; y++) Marshal.Copy(pixels, y * width * 4, buffer.Address + y * buffer.RowBytes, width * 4);
                }
                bitmap.Save(file);
            }
            NativeLog.Write(LogLevel.Info, $"Screenshot saved to {file} ({width}x{height})");
        }
        finally
        {
            if (highRes) RenderScale = previousScale;
        }
    }

    // Editing tools

    [ObservableProperty]
    private bool canUndo;

    [ObservableProperty]
    private bool canRedo;

    [RelayCommand]
    private void Undo()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_undo(Handle);
    }

    [RelayCommand]
    private void Redo()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_redo(Handle);
    }

    [RelayCommand]
    private void SnapToFloor()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_snap_to_floor(Handle);
    }

    [ObservableProperty]
    private bool isMeasuring;

    partial void OnIsMeasuringChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_measuring(Handle, value ? 1 : 0);
    }

    public event Action? SaveScenesRequested;

    [RelayCommand]
    private void SaveScenes() => SaveScenesRequested?.Invoke();

    // Keys in the viewport change the view, the camera speed, bookmarks and the undo stacks natively.
    private void PollViewOptions()
    {
        int nativeView = NativeMethods.rae_viewport_get_view(Handle);
        if (nativeView != View.View && nativeView >= 0 && nativeView < ViewOptions.Count)
        {
            syncingView = true;
            View = ViewOptions[nativeView];
            syncingView = false;
        }
        float speed = NativeMethods.rae_viewport_get_camera_speed(Handle);
        if (Math.Abs(speed - CameraSpeed) > 1e-4) CameraSpeed = speed;
        uint mask = NativeMethods.rae_viewport_get_bookmarks(Handle);
        if (mask != bookmarkMask)
        {
            bookmarkMask = mask;
            OnPropertyChanged(nameof(IsBookmarkSaved));
        }
        uint undo = NativeMethods.rae_viewport_get_undo_state(Handle);
        CanUndo = (undo & 1) != 0;
        CanRedo = (undo & 2) != 0;
    }
}
