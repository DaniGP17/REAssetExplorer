using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record FlipbookRate(int PatternsPerSecond)
{
    public override string ToString() => PatternsPerSecond.ToString(CultureInfo.InvariantCulture) + " fps";
}

public sealed partial class ImageToolbarViewModel(ViewportViewModel viewport, bool flipbook) : ObservableObject
{
    public bool IsFlipbook { get; } = flipbook;

    public IReadOnlyList<FlipbookRate> Rates { get; } = [new(5), new(10), new(15), new(24), new(30), new(60)];

    [ObservableProperty]
    private bool showRed = true;

    [ObservableProperty]
    private bool showGreen = true;

    [ObservableProperty]
    private bool showBlue = true;

    // Off by default: packed textures (ALBM, NRMR) keep other data in alpha.
    [ObservableProperty]
    private bool showAlpha;

    [ObservableProperty]
    private bool isPlaying;

    [ObservableProperty]
    private FlipbookRate rate = new(15);

    [ObservableProperty]
    private string statusText = string.Empty;

    partial void OnShowRedChanged(bool value) => PushChannels();
    partial void OnShowGreenChanged(bool value) => PushChannels();
    partial void OnShowBlueChanged(bool value) => PushChannels();
    partial void OnShowAlphaChanged(bool value) => PushChannels();
    partial void OnIsPlayingChanged(bool value) => PushFlipbook();
    partial void OnRateChanged(FlipbookRate value) => PushFlipbook();

    private void PushChannels()
    {
        int mask = (ShowRed ? 1 : 0) | (ShowGreen ? 2 : 0) | (ShowBlue ? 4 : 0) | (ShowAlpha ? 8 : 0);
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_channels(viewport.Handle, mask);
    }

    private void PushFlipbook()
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_flipbook(viewport.Handle, IsPlaying ? 1 : 0, Rate.PatternsPerSecond);
    }

    public void Tick()
    {
        if (viewport.Handle == IntPtr.Zero ||
            NativeMethods.rae_viewport_image_status(viewport.Handle, out float zoom, out int pattern) == 0)
        {
            StatusText = string.Empty;
            return;
        }
        string text = string.Create(CultureInfo.InvariantCulture, $"{zoom * 100:0.#}%");
        if (IsFlipbook && pattern >= 0) text += string.Create(CultureInfo.InvariantCulture, $"   pattern {pattern}");
        StatusText = text;
    }
}
