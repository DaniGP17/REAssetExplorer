using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;
using NAudio.Vorbis;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.TestUI.Audio;

namespace REAssetExplorer.TestUI;

/// <summary>
/// Lists every Wwise media chunk inside a .bnk and lets the user play, scrub, and
/// export them. WEM streams are decoded to OGG on demand via <see cref="WemConverter"/>.
/// </summary>
public partial class BankViewerWindow : Window
{
    // ── Source identity ──────────────────────────────────────────────────────
    private readonly string   _fileName;
    private readonly PakEntry _pakEntry;

    // ── Parsed data ──────────────────────────────────────────────────────────
    private BankData? _bankData;
    private readonly ObservableCollection<AudioFileRow> _audioFiles = new();

    // ── Playback ─────────────────────────────────────────────────────────────
    private OggAudioPlayer? _player;
    private readonly DispatcherTimer _timer;
    private bool _userScrubbing;
    private int  _currentIndex = -1;

    public BankViewerWindow(string fileName, PakEntry pakEntry)
    {
        InitializeComponent();

        _fileName = fileName;
        _pakEntry = pakEntry;
        TitleText.Text = $"Audio Bank Viewer — {fileName}";

        AudioFilesGrid.ItemsSource = _audioFiles;

        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        _timer.Tick += OnPlaybackTimerTick;

        StateChanged += (_, _) => UpdateMaxRestoreGlyph();
        Loaded       += async (_, _) => await LoadBankAsync();
        Closing      += (_, _) => DisposePlayback();
    }

    // ── Loading ──────────────────────────────────────────────────────────────

    private async Task LoadBankAsync()
    {
        if (string.IsNullOrEmpty(_pakEntry.FilePath))
        {
            ShowError("No bank data available.");
            return;
        }

        ShowLoading("Loading audio bank…");

        try
        {
            var provider = App.CurrentProvider;
            if (provider == null) { ShowError("No game loaded."); return; }

            var pakFile = FindPakFile(_pakEntry);
            if (pakFile == null) { ShowError("PAK file not found for this entry."); return; }

            var raw = await Task.Run(() => provider.PakReader.ExtractFile(pakFile, _pakEntry));
            var reader = provider.AssetReaders.GetReader<BankData>(_pakEntry.FilePath);
            if (reader == null) { ShowError($"No bank reader for {provider.Name}."); return; }

            var result = reader.Read(raw, _pakEntry.FilePath);
            if (result.IsFailure || result.Value == null)
            {
                ShowError($"Failed to read bank:\n{result.Error}");
                return;
            }

            _bankData = result.Value;

            PopulateAudioList();
            UpdateBankInfo();
            HideOverlays();

            // Calculate durations in background — only works if codebooks are available.
            _ = Task.Run(CalculateDurationsAsync);
            StatusText.Text = $"Loaded {_audioFiles.Count} audio files";
        }
        catch (Exception ex)
        {
            ShowError($"Error loading bank:\n{ex.Message}");
        }
    }

    private static PakFile? FindPakFile(PakEntry entry)
    {
        foreach (var pak in GameLoader.LoadedPakFiles.Values)
        {
            if (pak.Entries.Any(e => e.FilePath == entry.FilePath))
                return pak;
        }
        return null;
    }

    private void PopulateAudioList()
    {
        _audioFiles.Clear();
        if (_bankData?.MediaHeaders == null) return;

        for (int i = 0; i < _bankData.MediaHeaders.Length; i++)
        {
            var media = _bankData.MediaHeaders[i];
            string name = _bankData.StringTable.TryGetValue(media.Id, out var n)
                ? n
                : $"Audio_{media.Id:X8}";

            _audioFiles.Add(new AudioFileRow
            {
                Index = i + 1,
                Id    = media.Id.ToString("X8"),
                Name  = name,
                Size  = media.Size,
                Data  = media.Data,
            });
        }
    }

    private void UpdateBankInfo()
    {
        if (_bankData == null) return;

        FileNameText.Text   = _fileName;
        FilePathText.Text   = _pakEntry.FilePath ?? "";
        BankIdText.Text     = $"0x{_bankData.Header.BankID:X8}";
        VersionText.Text    = _bankData.Header.Version.ToString();
        LanguageText.Text   = LanguageName(_bankData.Header.LanguageID);
        ProjectIdText.Text  = $"0x{_bankData.Header.ProjectID:X8}";
        AudioCountText.Text = (_bankData.MediaHeaders?.Length ?? 0).ToString();
        TotalSizeText.Text  = FormatBytes(_bankData.MediaHeaders?.Sum(m => (long)m.Size) ?? 0);
        HircText.Text       = (_bankData.HircObjects?.Count ?? 0).ToString();
        StringTableText.Text = $"{_bankData.StringTable.Count} entries";

        CodebooksText.Text = WemSettings.IsCodebooksAvailable
            ? $"Found: {Path.GetFileName(WemSettings.CodebooksPath)}"
            : "Not found (playback disabled)";
    }

    private async Task CalculateDurationsAsync()
    {
        if (!WemSettings.IsCodebooksAvailable) WemSettings.TryAutoLocateCodebooks();
        if (!WemSettings.IsCodebooksAvailable) return;

        // Snapshot to avoid touching the ObservableCollection from this thread.
        var snapshot = await Dispatcher.InvokeAsync(() => _audioFiles.ToArray());

        foreach (var row in snapshot)
        {
            if (row.Data == null || row.Data.Length == 0) continue;
            try
            {
                var ogg = WemConverter.ConvertWemToOgg(row.Data, WemSettings.CodebooksPath);
                var temp = Path.GetTempFileName();
                try
                {
                    File.WriteAllBytes(temp, ogg);
                    using var v = new VorbisWaveReader(temp);
                    var dur = v.TotalTime;
                    await Dispatcher.InvokeAsync(() => row.Duration = dur);
                }
                finally { try { File.Delete(temp); } catch { } }
            }
            catch
            {
                // Don't spam errors — leave as "--:--".
            }
        }
    }

    // ── Selection / playback ─────────────────────────────────────────────────

    private void OnAudioSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is AudioFileRow row)
            _currentIndex = _audioFiles.IndexOf(row);
    }

    private void OnAudioGridDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is AudioFileRow row)
            PlayRow(row);
    }

    private void OnPlaySelected(object sender, RoutedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is AudioFileRow row) PlayRow(row);
    }

    private void OnPlayPause(object sender, RoutedEventArgs e)
    {
        if (_player == null)
        {
            // Nothing loaded — play current selection or first row.
            if (AudioFilesGrid.SelectedItem is AudioFileRow row) PlayRow(row);
            else if (_audioFiles.Count > 0)
            {
                AudioFilesGrid.SelectedIndex = 0;
                PlayRow(_audioFiles[0]);
            }
            return;
        }

        if (_player.IsPlaying) { _player.Pause(); _timer.Stop(); StatusText.Text = "Paused"; }
        else                   { _player.Play();  _timer.Start(); StatusText.Text = "Playing"; }
    }

    private void OnStop(object sender, RoutedEventArgs e) => StopPlayback();

    private void OnPrev(object sender, RoutedEventArgs e)
    {
        if (_currentIndex <= 0) return;
        AudioFilesGrid.SelectedIndex = _currentIndex - 1;
        if (AudioFilesGrid.SelectedItem is AudioFileRow row) PlayRow(row);
    }

    private void OnNext(object sender, RoutedEventArgs e)
    {
        if (_currentIndex < 0 || _currentIndex >= _audioFiles.Count - 1) return;
        AudioFilesGrid.SelectedIndex = _currentIndex + 1;
        if (AudioFilesGrid.SelectedItem is AudioFileRow row) PlayRow(row);
    }

    private void PlayRow(AudioFileRow row)
    {
        try
        {
            StopPlayback();

            if (row.Data == null || row.Data.Length == 0)
            {
                StatusText.Text = "No audio data for this entry.";
                return;
            }

            if (!WemSettings.IsCodebooksAvailable) WemSettings.TryAutoLocateCodebooks();
            if (!WemSettings.IsCodebooksAvailable)
            {
                new StatusWindow(StatusType.Warning,
                    "Wwise codebooks (packed_codebooks_aoTuV_603.bin) are required for playback.\n" +
                    "Drop the file in the Dependencies/ folder next to the binaries.").Show();
                StatusText.Text = "Codebooks required for playback";
                return;
            }

            StatusText.Text = "Decoding WEM…";
            byte[] ogg = WemConverter.ConvertWemToOgg(row.Data, WemSettings.CodebooksPath);

            _player = new OggAudioPlayer();
            _player.PlaybackStopped += OnPlaybackStopped;
            _player.LoadOgg(ogg);
            _player.Volume = VolumeSlider.Value / 100.0;
            _player.Play();

            NowPlayingText.Text       = row.Name;
            ProgressSlider.Maximum    = _player.Duration.TotalSeconds;
            TotalTimeText.Text        = FormatTime(_player.Duration);
            PlayPauseButton.Content   = ""; // pause glyph

            // Backfill duration on the row if it wasn't computed yet.
            if (row.Duration <= TimeSpan.Zero) row.Duration = _player.Duration;

            _timer.Start();
            StatusText.Text = $"Playing: {row.Name}";
        }
        catch (Exception ex)
        {
            new StatusWindow(StatusType.Error, $"Failed to play audio:\n{ex.Message}").Show();
            StatusText.Text = "Playback error";
        }
    }

    private void StopPlayback()
    {
        _timer.Stop();
        _player?.Stop();
        _player?.Dispose();
        _player = null;

        PlayPauseButton.Content = ""; // play glyph
        NowPlayingText.Text     = "No audio selected";
        CurrentTimeText.Text    = "00:00";
        ProgressSlider.Value    = 0;
        StatusText.Text         = "Ready";
    }

    private void DisposePlayback()
    {
        _timer.Stop();
        _player?.Dispose();
        _player = null;
    }

    private void OnPlaybackStopped(object? sender, EventArgs e) =>
        Dispatcher.Invoke(StopPlayback);

    private void OnPlaybackTimerTick(object? sender, EventArgs e)
    {
        if (_player == null || _userScrubbing) return;
        ProgressSlider.Value  = _player.Position.TotalSeconds;
        CurrentTimeText.Text  = FormatTime(_player.Position);
    }

    // ── Slider scrub ─────────────────────────────────────────────────────────

    private void OnProgressMouseDown(object sender, MouseButtonEventArgs e) => _userScrubbing = true;

    private void OnProgressMouseUp(object sender, MouseButtonEventArgs e)
    {
        _userScrubbing = false;
        if (_player != null)
            _player.Position = TimeSpan.FromSeconds(ProgressSlider.Value);
    }

    private void OnProgressValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_userScrubbing) CurrentTimeText.Text = FormatTime(TimeSpan.FromSeconds(e.NewValue));
    }

    private void OnVolumeChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_player != null) _player.Volume = e.NewValue / 100.0;
        if (VolumeText != null) VolumeText.Text = $"{(int)e.NewValue}%";
    }

    // ── Export ───────────────────────────────────────────────────────────────

    private void OnExportDropdown(object sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.ContextMenu != null)
        {
            btn.ContextMenu.PlacementTarget = btn;
            btn.ContextMenu.IsOpen          = true;
        }
    }

    private void OnExportWem(object sender, RoutedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is not AudioFileRow row) return;
        if (row.Data == null || row.Data.Length == 0)
        {
            new StatusWindow(StatusType.Warning, "No audio data for this entry.").Show();
            return;
        }

        var dlg = new SaveFileDialog
        {
            FileName   = row.Name,
            Filter     = "Wwise Audio (*.wem)|*.wem",
            DefaultExt = "wem",
        };
        if (dlg.ShowDialog() != true) return;

        try
        {
            File.WriteAllBytes(dlg.FileName, row.Data);
            new StatusWindow(StatusType.Success, $"Exported WEM to:\n{dlg.FileName}").Show();
        }
        catch (Exception ex)
        {
            new StatusWindow(StatusType.Error, $"Export failed:\n{ex.Message}").Show();
        }
    }

    private void OnExportOgg(object sender, RoutedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is not AudioFileRow row) return;
        if (row.Data == null || row.Data.Length == 0)
        {
            new StatusWindow(StatusType.Warning, "No audio data for this entry.").Show();
            return;
        }
        if (!WemSettings.IsCodebooksAvailable && !WemSettings.TryAutoLocateCodebooks())
        {
            new StatusWindow(StatusType.Warning,
                "Wwise codebooks required to convert to OGG.").Show();
            return;
        }

        var dlg = new SaveFileDialog
        {
            FileName   = Path.ChangeExtension(row.Name, null),
            Filter     = "Ogg Vorbis (*.ogg)|*.ogg",
            DefaultExt = "ogg",
        };
        if (dlg.ShowDialog() != true) return;

        try
        {
            var ogg = WemConverter.ConvertWemToOgg(row.Data, WemSettings.CodebooksPath);
            File.WriteAllBytes(dlg.FileName, ogg);
            new StatusWindow(StatusType.Success, $"Exported OGG to:\n{dlg.FileName}").Show();
        }
        catch (Exception ex)
        {
            new StatusWindow(StatusType.Error, $"Export failed:\n{ex.Message}").Show();
        }
    }

    private void OnExportAllWem(object sender, RoutedEventArgs e)
    {
        if (_audioFiles.Count == 0) return;

        var dlg = new OpenFolderDialog { Title = "Select folder to export all WEM files" };
        if (dlg.ShowDialog() != true) return;

        int ok = 0, fail = 0;
        foreach (var row in _audioFiles)
        {
            if (row.Data == null || row.Data.Length == 0) continue;
            try
            {
                var dst = Path.Combine(dlg.FolderName, $"{row.Name}.wem");
                File.WriteAllBytes(dst, row.Data);
                ok++;
            }
            catch { fail++; }
        }

        var kind = fail == 0 ? StatusType.Success : StatusType.Warning;
        new StatusWindow(kind, $"Exported {ok} files ({fail} failed) to:\n{dlg.FolderName}").Show();
    }

    // ── Misc ─────────────────────────────────────────────────────────────────

    private void ShowLoading(string msg)
    {
        LoadingText.Text          = msg;
        LoadingOverlay.Visibility = Visibility.Visible;
        ErrorOverlay.Visibility   = Visibility.Collapsed;
        StatusText.Text           = msg;
    }

    private void ShowError(string msg)
    {
        ErrorText.Text            = msg;
        ErrorOverlay.Visibility   = Visibility.Visible;
        LoadingOverlay.Visibility = Visibility.Collapsed;
        StatusText.Text           = "Error";
    }

    private void HideOverlays()
    {
        LoadingOverlay.Visibility = Visibility.Collapsed;
        ErrorOverlay.Visibility   = Visibility.Collapsed;
    }

    private static string LanguageName(uint id) => id switch
    {
        0xE07A43D  => "Spanish",
        0x28CCF006 => "English",
        0x49D84887 => "Italian",
        0xFFB9E71B => "German",
        0x77BA6750 => "Japanese",
        0x134795B3 => "French",
        0x17705D3E => "None",
        _          => $"Unknown (0x{id:X8})",
    };

    private static string FormatBytes(long bytes)
    {
        string[] u = { "B", "KB", "MB", "GB" };
        double s = bytes; int i = 0;
        while (s >= 1024 && i < u.Length - 1) { s /= 1024; i++; }
        return $"{s:0.##} {u[i]}";
    }

    internal static string FormatTime(TimeSpan t) =>
        t.TotalHours >= 1 ? t.ToString(@"hh\:mm\:ss") : t.ToString(@"mm\:ss");

    // ── Window chrome ────────────────────────────────────────────────────────

    private void UpdateMaxRestoreGlyph() =>
        MaxRestoreButton.Content = WindowState == WindowState.Maximized ? "" : "";

    private void OnMinimize  (object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void OnMaxRestore(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void OnClose     (object sender, RoutedEventArgs e) => Close();
}

/// <summary>
/// View-model row for the audio files grid. Duration is computed asynchronously
/// after the bank loads, so it raises PropertyChanged when set.
/// </summary>
public sealed class AudioFileRow : INotifyPropertyChanged
{
    private TimeSpan _duration;

    public int    Index { get; init; }
    public string Id    { get; init; } = "";
    public string Name  { get; init; } = "";
    public uint   Size  { get; init; }
    public byte[]? Data { get; init; }

    public string SizeFormatted
    {
        get
        {
            string[] u = { "B", "KB", "MB", "GB" };
            double s = Size; int i = 0;
            while (s >= 1024 && i < u.Length - 1) { s /= 1024; i++; }
            return $"{s:0.##} {u[i]}";
        }
    }

    public TimeSpan Duration
    {
        get => _duration;
        set
        {
            _duration = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(DurationFormatted));
        }
    }

    public string DurationFormatted => _duration > TimeSpan.Zero
        ? BankViewerWindow.FormatTime(_duration)
        : "--:--";

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
