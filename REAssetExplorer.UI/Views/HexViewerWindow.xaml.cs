using System.IO;
using System.Text;
using System.Windows;
using REAssetExplorer.Core.Pak;
using Wpf.Ui.Controls;
using System.Collections.ObjectModel;
using System.ComponentModel;
using REAssetExplorer.App.Views;
using REAssetExplorer.UI.Enums;

namespace REAssetExplorer.UI.Views;

/// <summary>
/// Represents a single line in the hex viewer.
/// </summary>
public class HexViewLine
{
    public string Offset { get; set; } = string.Empty;
    public string HexBytes { get; set; } = string.Empty;
    public string AsciiText { get; set; } = string.Empty;
}

/// <summary>
/// Hex viewer window for displaying raw file data.
/// </summary>
public partial class HexViewerWindow : FluentWindow
{
    private readonly string _fileName;
    private readonly PakEntry _pakEntry;
    private byte[] _data = Array.Empty<byte>();
    private const int BytesPerLine = 16;
    private const int MaxInitialLines = 5000;
    private int _currentLoadedLines = 0;
    private ObservableCollection<HexViewLine> _hexLines = new();

    public HexViewerWindow(string fileName, PakEntry pakEntry)
    {
        InitializeComponent();
        
        _fileName = fileName;
        _pakEntry = pakEntry;
        
        HexItemsControl.ItemsSource = _hexLines;
        
        Loaded += OnWindowLoaded;
    }
    
    private async void OnWindowLoaded(object sender, RoutedEventArgs e)
    {
        await LoadHexDataAsync();
    }
    
    private async Task LoadHexDataAsync()
    {
        try
        {
            StatusText.Text = "Loading...";
            
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
            
            byte[] data = await Task.Run(() => gameProvider.PakReader.ExtractFile(pakFile, _pakEntry));
            
            _data = data;
            
            FileNameText.Text = _fileName;
            FileSizeText.Text = $"Size: {FormatBytes(data.Length)} ({data.Length:N0} bytes)";
            
            // Generate hex lines in background
            await Task.Run(() => GenerateHexLines(data));
            
            StatusText.Text = $"Ready - {_hexLines.Count:N0} lines";
        }
        catch (Exception ex)
        {
            ShowError($"Error loading file: {ex.Message}");
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
    
    private void GenerateHexLines(byte[] data)
    {
        int totalLines = (data.Length + BytesPerLine - 1) / BytesPerLine;
        
        // Clear existing lines on UI thread
        Application.Current.Dispatcher.Invoke(() => _hexLines.Clear());
        
        int linesToGenerate = Math.Min(totalLines, MaxInitialLines);
        _currentLoadedLines = linesToGenerate;
        
        // Process in smaller batches to keep UI responsive
        const int batchSize = 500;
        var batch = new List<HexViewLine>(batchSize);
        
        for (int line = 0; line < linesToGenerate; line++)
        {
            int offset = line * BytesPerLine;
            int bytesInLine = Math.Min(BytesPerLine, data.Length - offset);
            
            var hexLine = new HexViewLine
            {
                Offset = $"{offset:X8}",
                HexBytes = FormatHexBytes(data, offset, bytesInLine),
                AsciiText = FormatAsciiText(data, offset, bytesInLine)
            };
            
            batch.Add(hexLine);
            
            // Add batch to collection on UI thread
            if (batch.Count >= batchSize || line == linesToGenerate - 1)
            {
                var currentBatch = batch.ToList();
                Application.Current.Dispatcher.Invoke(() =>
                {
                    foreach (var item in currentBatch)
                    {
                        _hexLines.Add(item);
                    }
                });
                batch.Clear();
            }
        }
        
        Application.Current.Dispatcher.Invoke(() =>
        {
            if (totalLines > MaxInitialLines)
            {
                StatusText.Text = $"Showing first {linesToGenerate:N0} of {totalLines:N0} lines ({FormatBytes(linesToGenerate * BytesPerLine)} of {FormatBytes(data.Length)}) - Use search/goto for navigation";
            }
            else
            {
                StatusText.Text = $"Ready - {_hexLines.Count:N0} lines";
            }
        });
    }
    
    private string FormatHexBytes(byte[] data, int offset, int count)
    {
        var sb = new StringBuilder(BytesPerLine * 3);
        
        for (int i = 0; i < BytesPerLine; i++)
        {
            if (i < count)
            {
                sb.Append($"{data[offset + i]:X2} ");
            }
            else
            {
                sb.Append("   ");
            }
        }
        
        return sb.ToString().TrimEnd();
    }
    
    private string FormatAsciiText(byte[] data, int offset, int count)
    {
        var sb = new StringBuilder(count);
        
        for (int i = 0; i < count; i++)
        {
            byte b = data[offset + i];
            char c = (b >= 32 && b <= 126) ? (char)b : '.';
            sb.Append(c);
        }
        
        return sb.ToString();
    }
    
    private void ShowError(string message)
    {
        StatusText.Text = $"Error: {message}";
        var errorWindow = new StatusWindow(StatusType.Error, message);
        errorWindow.ShowDialog();
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
    
    private void SearchBox_KeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == System.Windows.Input.Key.Enter)
        {
            FindButton_Click(sender, e);
        }
    }
    
    private void FindButton_Click(object sender, RoutedEventArgs e)
    {
        string searchText = SearchBox.Text?.Trim() ?? string.Empty;
        if (string.IsNullOrEmpty(searchText))
        {
            StatusText.Text = "Enter search text";
            return;
        }
        
        byte[]? searchBytes = TryParseHexString(searchText);
        
        if (searchBytes == null)
        {
            // Search as ASCII text
            searchBytes = Encoding.ASCII.GetBytes(searchText);
        }
        
        int foundIndex = SearchBytes(_data, searchBytes, 0);
        
        if (foundIndex >= 0)
        {
            ScrollToOffset(foundIndex);
            StatusText.Text = $"Found at offset 0x{foundIndex:X8}";
        }
        else
        {
            StatusText.Text = "Not found";
        }
    }
    
    private byte[]? TryParseHexString(string hex)
    {
        hex = hex.Replace(" ", "").Replace("0x", "");
        
        if (hex.Length % 2 != 0 || !hex.All(c => "0123456789ABCDEFabcdef".Contains(c)))
        {
            return null;
        }
        
        var bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; i++)
        {
            bytes[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
        }
        
        return bytes;
    }
    
    private int SearchBytes(byte[] data, byte[] pattern, int startIndex)
    {
        if (pattern.Length == 0 || pattern.Length > data.Length - startIndex)
            return -1;
        
        for (int i = startIndex; i <= data.Length - pattern.Length; i++)
        {
            bool found = true;
            for (int j = 0; j < pattern.Length; j++)
            {
                if (data[i + j] != pattern[j])
                {
                    found = false;
                    break;
                }
            }
            
            if (found)
                return i;
        }
        
        return -1;
    }
    
    private void GotoOffsetBox_KeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == System.Windows.Input.Key.Enter)
        {
            GotoButton_Click(sender, e);
        }
    }
    
    private void GotoButton_Click(object sender, RoutedEventArgs e)
    {
        string offsetText = GotoOffsetBox.Text?.Trim() ?? string.Empty;
        if (string.IsNullOrEmpty(offsetText))
        {
            StatusText.Text = "Enter offset";
            return;
        }
        
        offsetText = offsetText.Replace("0x", "").Replace("h", "");
        
        if (int.TryParse(offsetText, System.Globalization.NumberStyles.HexNumber, null, out int offset))
        {
            if (offset >= 0 && offset < _data.Length)
            {
                ScrollToOffset(offset);
                StatusText.Text = $"Jumped to offset 0x{offset:X8}";
            }
            else
            {
                StatusText.Text = $"Offset out of range (0x0 - 0x{_data.Length - 1:X8})";
            }
        }
        else
        {
            StatusText.Text = "Invalid hex offset";
        }
    }
    
    private void ScrollToOffset(int offset)
    {
        int lineIndex = offset / BytesPerLine;
        
        // Check if we need to load more lines
        if (lineIndex >= _currentLoadedLines && lineIndex < (_data.Length + BytesPerLine - 1) / BytesPerLine)
        {
            LoadMoreLinesUpTo(lineIndex + 100);
        }
        
        if (lineIndex >= 0 && lineIndex < _hexLines.Count)
        {
            HexScrollViewer.ScrollToVerticalOffset(lineIndex * 20); // Approximate line height
        }
    }
    
    private void LoadMoreLinesUpTo(int targetLine)
    {
        int totalLines = (_data.Length + BytesPerLine - 1) / BytesPerLine;
        int linesToLoad = Math.Min(targetLine, totalLines) - _currentLoadedLines;
        
        if (linesToLoad <= 0) return;
        
        StatusText.Text = $"Loading more lines...";
        
        Task.Run(() =>
        {
            var newLines = new List<HexViewLine>();
            
            for (int i = 0; i < linesToLoad; i++)
            {
                int line = _currentLoadedLines + i;
                int offset = line * BytesPerLine;
                int bytesInLine = Math.Min(BytesPerLine, _data.Length - offset);
                
                var hexLine = new HexViewLine
                {
                    Offset = $"{offset:X8}",
                    HexBytes = FormatHexBytes(_data, offset, bytesInLine),
                    AsciiText = FormatAsciiText(_data, offset, bytesInLine)
                };
                
                newLines.Add(hexLine);
            }
            
            Application.Current.Dispatcher.Invoke(() =>
            {
                foreach (var line in newLines)
                {
                    _hexLines.Add(line);
                }
                
                _currentLoadedLines += linesToLoad;
                StatusText.Text = $"Loaded {_currentLoadedLines:N0} of {totalLines:N0} lines";
            });
        });
    }
    
    private void ExportButton_Click(object sender, RoutedEventArgs e)
    {
        var saveDialog = new Microsoft.Win32.SaveFileDialog
        {
            FileName = Path.GetFileName(_fileName),
            Filter = "Binary Files (*.bin)|*.bin|All Files (*.*)|*.*",
            DefaultExt = "bin"
        };
        
        if (saveDialog.ShowDialog() == true)
        {
            try
            {
                File.WriteAllBytes(saveDialog.FileName, _data);
                StatusText.Text = $"Exported {FormatBytes(_data.Length)} to {Path.GetFileName(saveDialog.FileName)}";
            }
            catch (Exception ex)
            {
                ShowError($"Failed to export: {ex.Message}");
            }
        }
    }
}
