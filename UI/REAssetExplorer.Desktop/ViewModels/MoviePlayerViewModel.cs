using System.Globalization;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed partial class MoviePlayerViewModel : ObservableObject
{
    private readonly ViewportViewModel viewport;
    // Faster than the window timer, so the playhead moves smoothly.
    private readonly DispatcherTimer timer = new(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(33) };

    public MoviePlayerViewModel(ViewportViewModel viewport)
    {
        this.viewport = viewport;
        timer.Tick += (_, _) => Tick();
        timer.Start();
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPlaying))]
    private bool isPaused;

    [ObservableProperty]
    private bool isLoaded;

    [ObservableProperty]
    private double position;

    [ObservableProperty]
    private double duration;

    [ObservableProperty]
    private string timeText = string.Empty;

    public bool IsPlaying => !IsPaused;

    [RelayCommand]
    private void TogglePlay()
    {
        if (viewport.Handle == IntPtr.Zero) return;
        NativeMethods.rae_viewport_movie_set_paused(viewport.Handle, IsPaused ? 0 : 1);
        Tick();
    }

    [RelayCommand]
    private void Restart()
    {
        if (viewport.Handle == IntPtr.Zero) return;
        NativeMethods.rae_viewport_movie_seek(viewport.Handle, 0);
        NativeMethods.rae_viewport_movie_set_paused(viewport.Handle, 0);
        Tick();
    }

    [RelayCommand]
    private void Seek(double seconds)
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_movie_seek(viewport.Handle, (float)seconds);
    }

    public void Tick()
    {
        if (viewport.Handle == IntPtr.Zero ||
            NativeMethods.rae_viewport_movie_status(viewport.Handle, out float pos, out float length, out int paused) == 0)
        {
            IsLoaded = false;
            TimeText = string.Empty;
            return;
        }
        IsLoaded = true;
        IsPaused = paused != 0;
        Position = pos;
        Duration = length;
        TimeText = $"{Format(pos)} / {Format(length)}";
    }

    public void Release() => timer.Stop();

    private static string Format(double seconds) =>
        TimeSpan.FromSeconds(seconds).ToString(seconds >= 3600 ? @"h\:mm\:ss\.f" : @"m\:ss\.f", CultureInfo.InvariantCulture);
}
