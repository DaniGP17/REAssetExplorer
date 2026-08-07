using System.Globalization;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

// The native player is app-wide: only its owner window shows and controls the sound.
public sealed partial class AudioPlayerViewModel : ObservableObject
{
    private const int WaveformColumns = 4096;
    private const int MaxWaveformChannels = 8;

    private static AudioPlayerViewModel? owner;

    private readonly IAssetServices assets;
    private readonly string container;
    // Polls faster than the window while playing, so the playhead moves smoothly.
    private readonly DispatcherTimer timer = new(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(30) };
    private string? loadedKey;

    public AudioPlayerViewModel(IAssetServices assets, string container)
    {
        this.assets = assets;
        this.container = container;
        timer.Tick += (_, _) => Tick();
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand))]
    [NotifyCanExecuteChangedFor(nameof(ExportCommand))]
    private OutlineNode? target;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand))]
    [NotifyPropertyChangedFor(nameof(ShowsWaveform))]
    private bool isDecoding;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PauseCommand))]
    private bool isPlaying;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(PauseCommand))]
    private bool isPaused;

    [ObservableProperty]
    private double position;

    [ObservableProperty]
    private double duration;

    [ObservableProperty]
    private string nowPlaying = string.Empty;

    [ObservableProperty]
    private string timeText = string.Empty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsWaveform))]
    private float[]? waveform;

    [ObservableProperty]
    private int waveformChannels;

    public bool ShowsWaveform => Waveform != null || IsDecoding;

    // Sounds, music tracks and .wem entries carry "wem:<media id>".
    public static uint? MediaId(OutlineNode? node) =>
        node != null && node.Key.StartsWith("wem:", StringComparison.Ordinal) &&
        uint.TryParse(node.Key.AsSpan(4), NumberStyles.None, CultureInfo.InvariantCulture, out uint id)
            ? id
            : null;

    public void Select(OutlineNode? node) => Target = MediaId(node) != null ? node : null;

    public void Toggle(OutlineNode node)
    {
        if (MediaId(node) == null) return;
        Target = node;
        if (IsPlaying && owner == this && loadedKey == node.Key) Pause();
        else _ = Play();
    }

    [RelayCommand(CanExecute = nameof(CanPlay))]
    private async Task Play()
    {
        if (Target is not { } node || MediaId(node) is not { } id) return;
        if (owner == this && loadedKey == node.Key)
        {
            if (IsPaused) NativeMethods.rae_audio_set_paused(0);
            else NativeMethods.rae_audio_seek(0);
            Resume();
            return;
        }
        IsDecoding = true;
        owner = this;
        loadedKey = null;
        Waveform = null;
        NowPlaying = node.Name;
        bool ok = await assets.PlayAudioAsync(container, id);
        if (ok)
        {
            loadedKey = node.Key;
            (float[]? peaks, int channels) = await Task.Run(ReadWaveform);
            WaveformChannels = channels;
            Waveform = peaks;
        }
        else
        {
            NowPlaying = $"{node.Name} (could not be decoded)";
        }
        IsDecoding = false;
        Resume();
    }

    [RelayCommand(CanExecute = nameof(CanPause))]
    private void Pause()
    {
        if (owner != this) return;
        NativeMethods.rae_audio_set_paused(IsPaused ? 0 : 1);
        Resume();
    }

    [RelayCommand]
    private void Stop()
    {
        if (owner == this) NativeMethods.rae_audio_stop();
        Tick();
    }

    // Native seek also restarts an ended or stopped sound; a pause is kept.
    [RelayCommand]
    private void Seek(double seconds)
    {
        if (owner != this || loadedKey == null) return;
        NativeMethods.rae_audio_seek((float)seconds);
        Resume();
    }

    [RelayCommand(CanExecute = nameof(CanExport))]
    private Task Export() =>
        Target is { } node && MediaId(node) is { } id
            ? assets.ExportAudioAsync(container, id, Path.ChangeExtension(node.Name, ".wav"))
            : Task.CompletedTask;

    private bool CanPlay() => Target != null && !IsDecoding;

    private bool CanPause() => IsPlaying || IsPaused;

    private bool CanExport() => Target != null;

    private void Resume()
    {
        timer.Start();
        Tick();
    }

    public void Tick()
    {
        if (owner != this)
        {
            if (loadedKey != null)
            {
                loadedKey = null;
                Waveform = null;
                NowPlaying = string.Empty;
                TimeText = string.Empty;
                Position = 0;
                Duration = 0;
            }
            IsPlaying = false;
            IsPaused = false;
            timer.Stop();
            return;
        }
        // The native player still holds the previous sound.
        if (IsDecoding) return;
        int state = NativeMethods.rae_audio_status(out float pos, out float length);
        IsPlaying = state == 1;
        IsPaused = state == 2;
        Position = pos;
        Duration = length;
        TimeText = length > 0 ? $"{Format(pos)} / {Format(length)}" : string.Empty;
        if (!IsPlaying) timer.Stop();
    }

    public void Release()
    {
        timer.Stop();
        if (owner != this) return;
        NativeMethods.rae_audio_stop();
        owner = null;
    }

    private static (float[]? Peaks, int Channels) ReadWaveform()
    {
        var peaks = new float[WaveformColumns * MaxWaveformChannels * 2];
        int channels = NativeMethods.rae_audio_waveform(peaks, WaveformColumns, MaxWaveformChannels);
        if (channels <= 0) return (null, 0);
        Array.Resize(ref peaks, WaveformColumns * channels * 2);
        return (peaks, channels);
    }

    private static string Format(double seconds) =>
        TimeSpan.FromSeconds(seconds).ToString(seconds >= 3600 ? @"h\:mm\:ss\.f" : @"m\:ss\.f", CultureInfo.InvariantCulture);
}
