using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using Microsoft.Win32;
using REAssetExplorer.App.Views;
using REAssetExplorer.Core.Assets.Audio;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI.Audio;
using REAssetExplorer.UI.Enums;
using REAssetExplorer.UI.Services;
using Wpf.Ui.Controls;

namespace REAssetExplorer.UI.Views;

/// <summary>
/// Audio Bank viewer window for displaying and playing audio bank files (.bnk).
/// </summary>
public partial class BankViewerWindow : FluentWindow
{
    private readonly string _fileName;
    private readonly PakEntry _pakEntry;
    private BankData? _bankData;
    private ObservableCollection<AudioFileInfo> _audioFiles = new();
    
    private OggAudioPlayer? _audioPlayer;
    private DispatcherTimer? _updateTimer;
    private bool _isUserDraggingSlider = false;
    private int _currentAudioIndex = -1;

    public BankViewerWindow(string fileName, PakEntry? pakEntry = null)
    {
        InitializeComponent();
        
        _fileName = fileName;
        _pakEntry = pakEntry ?? default;
        
        AudioFilesGrid.ItemsSource = _audioFiles;
        
        Loaded += OnWindowLoaded;
        Closing += OnWindowClosing;
        
        InitializeAudioPlayer();
    }

    private void InitializeAudioPlayer()
    {
        _updateTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(100)
        };
        _updateTimer.Tick += UpdateTimer_Tick;
    }

    private async void OnWindowLoaded(object sender, RoutedEventArgs e)
    {
        await LoadBankAsync();
    }

    private void OnWindowClosing(object? sender, CancelEventArgs e)
    {
        StopAudio();
        _updateTimer?.Stop();
        _audioPlayer?.Dispose();
    }

    private async Task LoadBankAsync()
    {
        if (string.IsNullOrEmpty(_pakEntry.FilePath))
        {
            ShowError("No bank data available.");
            return;
        }

        LoadingIndicator.Visibility = Visibility.Visible;
        StatusText.Text = "Loading audio bank...";

        try
        {
            var gameProvider = GameManager.CurrentGameProvider;
            if (gameProvider == null)
            {
                ShowError("No game loaded.");
                return;
            }

            var pakFile = FindPakFile();
            if (pakFile == null)
            {
                ShowError("PAK file not found.");
                return;
            }

            var bankData = await ExtractBankDataAsync(pakFile);
            var bankReader = gameProvider.AssetReaders.GetReader<BankData>(_pakEntry.FilePath);

            if (bankReader == null)
            {
                ShowError($"No bank reader available for {gameProvider.Name}.");
                return;
            }

            var result = bankReader.Read(bankData, _pakEntry.FilePath);
            if (!result.IsSuccess)
            {
                ShowError($"Failed to read bank: {result.Error}");
                return;
            }

            _bankData = result.Value!;
            
            UpdateBankInfo();
            LoadAudioFiles();

            StatusText.Text = "Ready";
        }
        catch (Exception ex)
        {
            ShowError($"Error loading bank: {ex.Message}");
        }
        finally
        {
            LoadingIndicator.Visibility = Visibility.Collapsed;
        }
    }

    private PakFile? FindPakFile()
    {
        foreach (var pak in GameManager.LoadedPakFiles.Values)
        {
            if (pak.Entries.Any(e => e.FilePath == _pakEntry.FilePath))
            {
                return pak;
            }
        }
        return null;
    }

    private async Task<byte[]> ExtractBankDataAsync(PakFile pakFile)
    {
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
        {
            throw new InvalidOperationException("No game provider loaded.");
        }

        return await Task.Run(() => gameProvider.PakReader.ExtractFile(pakFile, _pakEntry));
    }

    private void UpdateBankInfo()
    {
        if (_bankData == null) return;

        FileNameText.Text = _fileName;
        BankIdText.Text = _bankData.Header.BankID.ToString("X8");
        VersionText.Text = _bankData.Header.Version.ToString();
        LanguageText.Text = GetLanguageName(_bankData.Header.LanguageID);
        ProjectIdText.Text = _bankData.Header.ProjectID.ToString("X8");
        
        int audioCount = _bankData.MediaHeaders?.Length ?? 0;
        AudioCountText.Text = audioCount.ToString();
        
        long totalSize = _bankData.MediaHeaders?.Sum(m => (long)m.Size) ?? 0;
        TotalSizeText.Text = FormatBytes(totalSize);
        
        HircObjectsText.Text = _bankData.HircObjects?.Count.ToString() ?? "0";
        StringTableText.Text = $"{_bankData.StringTable.Count} entries";
    }

    private void LoadAudioFiles()
    {
        _audioFiles.Clear();

        if (_bankData?.MediaHeaders == null || _bankData.MediaHeaders.Length == 0)
        {
            StatusText.Text = "No audio files found in bank";
            return;
        }

        foreach (var media in _bankData.MediaHeaders)
        {
            string name = "Unknown";
            
            // Try to get name from string table
            if (_bankData.StringTable.TryGetValue(media.Id, out var mediaName))
            {
                name = mediaName;
            }
            else
            {
                name = $"Audio_{media.Id:X8}";
            }

            var audioInfo = new AudioFileInfo
            {
                Id = media.Id.ToString("X8"),
                Name = name,
                Size = media.Size,
                Data = media.Data,
                MediaHeader = media
            };

            _audioFiles.Add(audioInfo);
        }

        StatusText.Text = $"Loaded {_audioFiles.Count} audio files";
        
        // Calculate durations in background
        Task.Run(() => CalculateAudioDurations());
    }
    
    private async Task CalculateAudioDurations()
    {
        if (!WemSettings.IsCodebooksAvailable)
        {
            WemSettings.TryAutoLocateCodebooks();
        }
        
        if (!WemSettings.IsCodebooksAvailable)
        {
            return; // Can't calculate durations without codebooks
        }

        foreach (var audioInfo in _audioFiles)
        {
            if (audioInfo.Data == null || audioInfo.Data.Length == 0)
                continue;
                
            try
            {
                // Convert to OGG and read duration with NVorbis
                var oggData = WemConverter.ConvertWemToOgg(audioInfo.Data, WemSettings.CodebooksPath);
                
                // Create temp file to read with VorbisWaveReader
                var tempFile = Path.GetTempFileName();
                try
                {
                    File.WriteAllBytes(tempFile, oggData);
                    using (var vorbisReader = new NAudio.Vorbis.VorbisWaveReader(tempFile))
                    {
                        await Dispatcher.InvokeAsync(() =>
                        {
                            audioInfo.Duration = vorbisReader.TotalTime;
                        });
                    }
                }
                finally
                {
                    if (File.Exists(tempFile))
                        File.Delete(tempFile);
                }
            }
            catch
            {
                // Ignore errors, duration will remain as --:--
            }
        }
    }

    private string GetLanguageName(uint languageId)
    {
        return languageId switch
        {
            0xE07A43D => "Spanish",
            0x28CCF006 => "English", // Don't know why ZHCN is sharing the same language hash as english
            0x49D84887 => "Italian",
            0xFFB9E71B => "German",
            0x77BA6750 => "Japanese",
            0x134795B3 => "French",
            0x17705D3E => "None",
            _ => $"Unknown (0x{languageId:X})"
        };
    }

    private void ShowError(string message)
    {
        ErrorMessage.Text = message;
        ErrorMessage.Visibility = Visibility.Visible;
        LoadingIndicator.Visibility = Visibility.Collapsed;
        StatusText.Text = "Error";
    }

    private string FormatBytes(long bytes)
    {
        string[] sizes = { "B", "KB", "MB", "GB" };
        double len = bytes;
        int order = 0;
        while (len >= 1024 && order < sizes.Length - 1)
        {
            order++;
            len /= 1024;
        }
        return $"{len:0.##} {sizes[order]}";
    }

    private void AudioFilesGrid_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is AudioFileInfo audioInfo)
        {
            _currentAudioIndex = _audioFiles.IndexOf(audioInfo);
        }
    }

    private void AudioFilesGrid_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount != 2)
            return;

        var dep = (DependencyObject)e.OriginalSource;
        while (dep != null && dep is not DataGridRow)
        {
            dep = System.Windows.Media.VisualTreeHelper.GetParent(dep);
        }

        if (dep is DataGridRow row && row.Item is AudioFileInfo audioInfo)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (AudioFilesGrid.SelectedItem == audioInfo)
                {
                    PlayAudio(audioInfo);
                }
            }), System.Windows.Threading.DispatcherPriority.Background);
        }
    }

    private void PlayPauseButton_Click(object sender, RoutedEventArgs e)
    {
        if (_audioPlayer == null)
        {
            // No audio loaded, try to play selected
            if (AudioFilesGrid.SelectedItem is AudioFileInfo audioInfo)
            {
                PlayAudio(audioInfo);
            }
            else if (_audioFiles.Count > 0)
            {
                AudioFilesGrid.SelectedIndex = 0;
                PlayAudio(_audioFiles[0]);
            }
        }
        else
        {
            if (_audioPlayer.IsPlaying)
            {
                PauseAudio();
            }
            else
            {
                ResumeAudio();
            }
        }
    }

    private void StopButton_Click(object sender, RoutedEventArgs e)
    {
        StopAudio();
    }

    private void PreviousButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentAudioIndex > 0)
        {
            AudioFilesGrid.SelectedIndex = _currentAudioIndex - 1;
            if (AudioFilesGrid.SelectedItem is AudioFileInfo audioInfo)
            {
                PlayAudio(audioInfo);
            }
        }
    }

    private void NextButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentAudioIndex < _audioFiles.Count - 1)
        {
            AudioFilesGrid.SelectedIndex = _currentAudioIndex + 1;
            if (AudioFilesGrid.SelectedItem is AudioFileInfo audioInfo)
            {
                PlayAudio(audioInfo);
            }
        }
    }

    private void PlayAudio(AudioFileInfo audioInfo)
    {
        try
        {
            StopAudio();

            if (audioInfo.Data == null || audioInfo.Data.Length == 0)
            {
                StatusText.Text = "No audio data available";
                return;
            }

            // Auto-locate codebooks if not done
            if (!WemSettings.IsCodebooksAvailable)
            {
                WemSettings.TryAutoLocateCodebooks();
            }

            // Check if codebooks are available
            if (!WemSettings.IsCodebooksAvailable)
            {
                var infoWindow = new StatusWindow(StatusType.Warning, "Codebooks are required for WEM playback. Please set the codebooks path in settings.");
                infoWindow.ShowDialog();
                StatusText.Text = "Codebooks required for WEM playback";
                return;
            }

            try
            {
                StatusText.Text = "Converting WEM to OGG...";
                byte[] oggData = WemConverter.ConvertWemToOgg(audioInfo.Data, WemSettings.CodebooksPath);
                
                StatusText.Text = "Loading audio...";
                
                _audioPlayer = new OggAudioPlayer();
                _audioPlayer.PlaybackStopped += AudioPlayer_PlaybackStopped;
                _audioPlayer.LoadOgg(oggData);
                _audioPlayer.Volume = VolumeSlider.Value / 100.0;
            }
            catch (Exception ex)
            {
                var errorMessage = $"Error converting/loading audio:\n{ex.Message}";
                var errorWindow = new StatusWindow(StatusType.Error, errorMessage);
                errorWindow.ShowDialog();
                StatusText.Text = "Error loading audio";
                return;
            }

            _audioPlayer.Play();
            
            NowPlayingText.Text = audioInfo.Name;
            PlayPauseIcon.Symbol = Wpf.Ui.Controls.SymbolRegular.Pause24;
            
            ProgressSlider.Maximum = _audioPlayer.Duration.TotalSeconds;
            TotalTimeText.Text = FormatTime(_audioPlayer.Duration);
            
            _updateTimer?.Start();
            
            StatusText.Text = $"Playing: {audioInfo.Name}";
            
            // Update duration if not set
            if (string.IsNullOrEmpty(audioInfo.DurationFormatted))
            {
                audioInfo.Duration = _audioPlayer.Duration;
            }
        }
        catch (Exception ex)
        {
            StatusText.Text = $"Error playing audio: {ex.Message}";
        }
    }

    private void PauseAudio()
    {
        _audioPlayer?.Pause();
        PlayPauseIcon.Symbol = Wpf.Ui.Controls.SymbolRegular.Play24;
        _updateTimer?.Stop();
        StatusText.Text = "Paused";
    }

    private void ResumeAudio()
    {
        _audioPlayer?.Play();
        PlayPauseIcon.Symbol = Wpf.Ui.Controls.SymbolRegular.Pause24;
        _updateTimer?.Start();
        StatusText.Text = "Playing";
    }

    private void StopAudio()
    {
        _audioPlayer?.Stop();
        _audioPlayer?.Dispose();
        _audioPlayer = null;
        
        _updateTimer?.Stop();
        
        PlayPauseIcon.Symbol = Wpf.Ui.Controls.SymbolRegular.Play24;
        NowPlayingText.Text = "No audio selected";
        CurrentTimeText.Text = "00:00";
        ProgressSlider.Value = 0;
        
        StatusText.Text = "Ready";
    }

    private void AudioPlayer_PlaybackStopped(object? sender, EventArgs e)
    {
        Dispatcher.Invoke(() =>
        {
            StopAudio();
        });
    }

    private void UpdateTimer_Tick(object? sender, EventArgs e)
    {
        if (_audioPlayer != null && !_isUserDraggingSlider)
        {
            ProgressSlider.Value = _audioPlayer.Position.TotalSeconds;
            CurrentTimeText.Text = FormatTime(_audioPlayer.Position);
        }
    }

    private void ProgressSlider_PreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        _isUserDraggingSlider = true;
    }

    private void ProgressSlider_PreviewMouseUp(object sender, MouseButtonEventArgs e)
    {
        _isUserDraggingSlider = false;
        
        if (_audioPlayer != null)
        {
            _audioPlayer.Position = TimeSpan.FromSeconds(ProgressSlider.Value);
        }
    }

    private void ProgressSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_isUserDraggingSlider && _audioPlayer != null)
        {
            CurrentTimeText.Text = FormatTime(TimeSpan.FromSeconds(e.NewValue));
        }
    }

    private void VolumeSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_audioPlayer != null)
        {
            _audioPlayer.Volume = e.NewValue / 100.0;
        }
        
        if (VolumeText != null)
        {
            VolumeText.Text = $"{(int)e.NewValue}%";
        }
    }

    private string FormatTime(TimeSpan time)
    {
        return time.TotalHours >= 1
            ? time.ToString(@"hh\:mm\:ss")
            : time.ToString(@"mm\:ss");
    }

    private void ExportButton_Click(object sender, RoutedEventArgs e)
    {
        var button = sender as System.Windows.Controls.Button;
        if (button?.ContextMenu != null)
        {
            button.ContextMenu.PlacementTarget = button;
            button.ContextMenu.IsOpen = true;
        }
    }

    private void ExportAsWem_Click(object sender, RoutedEventArgs e)
    {
        if (AudioFilesGrid.SelectedItem is AudioFileInfo audioInfo)
        {
            ExportSingleAudio(audioInfo);
        }
    }

    private void ExportAllAsWem_Click(object sender, RoutedEventArgs e)
    {
        ExportAllAudio();
    }

    private void ExportSingleAudio(AudioFileInfo audioInfo)
    {
        if (audioInfo.Data == null || audioInfo.Data.Length == 0)
        {
            var errorWindow = new StatusWindow(StatusType.Warning, "No audio data available to export.");
            errorWindow.ShowDialog();
            return;
        }

        var saveDialog = new WpfSaveFileDialog
        {
            FileName = audioInfo.Name,
            Filter = "Wwise Audio Files (*.wem)|*.wem",
            DefaultExt = "wem"
        };

        if (saveDialog.ShowDialog() == true)
        {
            try
            {
                File.WriteAllBytes(saveDialog.FileName, audioInfo.Data);

                StatusText.Text = $"Exported: {Path.GetFileName(saveDialog.FileName)}";
                
                var successWindow = new StatusWindow(StatusType.Success, 
                    $"Audio exported successfully!\n\nFile: {Path.GetFileName(saveDialog.FileName)}");
                successWindow.ShowDialog();
            }
            catch (Exception ex)
            {
                var errorWindow = new StatusWindow(StatusType.Error, 
                    $"Error exporting audio:\n{ex.Message}");
                errorWindow.ShowDialog();
            }
        }
    }

    private void ExportAllAudio()
    {
        var folderDialog = new OpenFolderDialog
        {
            Title = "Select folder to export all audio files as WEM"
        };

        if (folderDialog.ShowDialog() == true)
        {
            int successCount = 0;
            int errorCount = 0;

            foreach (var audioInfo in _audioFiles)
            {
                if (audioInfo.Data == null || audioInfo.Data.Length == 0)
                    continue;

                try
                {
                    string fileName = $"{audioInfo.Name}.wem";
                    string filePath = Path.Combine(folderDialog.FolderName, fileName);

                    File.WriteAllBytes(filePath, audioInfo.Data);

                    successCount++;
                }
                catch
                {
                    errorCount++;
                }
            }

            StatusText.Text = $"Exported {successCount} files ({errorCount} errors)";
            
            var resultWindow = new StatusWindow(
                errorCount == 0 ? StatusType.Success : StatusType.Warning,
                $"Export completed!\n\nSuccessful: {successCount}\nFailed: {errorCount}");
            resultWindow.ShowDialog();
        }
    }
}

/// <summary>
/// Information about an audio file in the bank.
/// </summary>
public class AudioFileInfo : INotifyPropertyChanged
{
    private TimeSpan _duration;
    
    public string Id { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public uint Size { get; set; }
    public byte[]? Data { get; set; }
    public MediaHeader MediaHeader { get; set; }

    public string SizeFormatted => FormatBytes(Size);

    public TimeSpan Duration
    {
        get => _duration;
        set
        {
            _duration = value;
            OnPropertyChanged(nameof(Duration));
            OnPropertyChanged(nameof(DurationFormatted));
        }
    }

    public string DurationFormatted => Duration.TotalSeconds > 0 
        ? (Duration.TotalHours >= 1 ? Duration.ToString(@"hh\:mm\:ss") : Duration.ToString(@"mm\:ss"))
        : "--:--";

    private static string FormatBytes(uint bytes)
    {
        string[] sizes = { "B", "KB", "MB", "GB" };
        double len = bytes;
        int order = 0;
        while (len >= 1024 && order < sizes.Length - 1)
        {
            order++;
            len /= 1024;
        }
        return $"{len:0.##} {sizes[order]}";
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected virtual void OnPropertyChanged(string propertyName)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
