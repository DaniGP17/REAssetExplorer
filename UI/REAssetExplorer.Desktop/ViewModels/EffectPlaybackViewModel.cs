using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record PlaybackSpeed(double Value)
{
    public override string ToString() => Value.ToString("0.##", CultureInfo.InvariantCulture) + "x";
}

public sealed partial class EffectPlaybackViewModel(ViewportViewModel viewport) : ObservableObject
{
    public IReadOnlyList<PlaybackSpeed> Speeds { get; } =
        [new(0.1), new(0.25), new(0.5), new(1), new(2)];

    [ObservableProperty]
    private bool isPaused;

    [ObservableProperty]
    private PlaybackSpeed speed = new(1);

    [ObservableProperty]
    private string statusText = string.Empty;

    partial void OnIsPausedChanged(bool value)
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_effect_set_paused(viewport.Handle, value ? 1 : 0);
    }

    partial void OnSpeedChanged(PlaybackSpeed value)
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_effect_set_speed(viewport.Handle, (float)value.Value);
    }

    [RelayCommand]
    private void Restart()
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_effect_restart(viewport.Handle);
    }

    public void Tick()
    {
        if (viewport.Handle == IntPtr.Zero ||
            NativeMethods.rae_viewport_effect_status(viewport.Handle, out float time, out int particles) == 0)
        {
            StatusText = string.Empty;
            return;
        }
        StatusText = string.Create(CultureInfo.InvariantCulture, $"{time:0.00} s   {particles} particles");
    }
}
