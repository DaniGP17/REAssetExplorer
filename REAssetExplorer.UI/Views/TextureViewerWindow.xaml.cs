using System.IO;
using System.Windows;
using System.Windows.Media.Imaging;
using System.Windows.Media;
using Microsoft.Win32;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Core.Assets;
using Wpf.Ui.Controls;
using System.Threading.Tasks;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;
using System.Windows.Input;
using REAssetExplorer.App.Views;
using REAssetExplorer.UI.Enums;

namespace REAssetExplorer.UI.Views;

/// <summary>
/// Texture viewer window for displaying texture files.
/// </summary>
public partial class TextureViewerWindow : FluentWindow
{
    private readonly string _fileName;
    private readonly PakEntry _pakEntry;
    private TextureData? _textureData;
    private byte[]? _originalPixelData;
    private byte[] _fullTextureData = Array.Empty<byte>();
    private int _displayedMipWidth;
    private int _displayedMipHeight;
    private int _displayedMipLevel;
    private long _actualFileSize;
    private double _zoomLevel = 1.0;
    private const double ZoomIncrement = 0.25;
    private const double MinZoom = 0.1;
    private const double MaxZoom = 10.0;

    public TextureViewerWindow(string fileName, PakEntry? pakEntry = null)
    {
        InitializeComponent();
        
        _fileName = fileName;
        _pakEntry = pakEntry ?? default;
        
        Loaded += OnWindowLoaded;
        ImageScrollViewer.PreviewMouseWheel += ImageScrollViewer_PreviewMouseWheel;
    }
    
    private async void OnWindowLoaded(object sender, RoutedEventArgs e)
    {
        await LoadTextureAsync();
    }
    
    private async Task LoadTextureAsync()
    {
        if (string.IsNullOrEmpty(_pakEntry.FilePath))
        {
            ShowError("No texture data available.");
            return;
        }
        
        LoadingIndicator.Visibility = Visibility.Visible;
        StatusText.Text = "Loading texture...";
        
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
            
            var textureData = await ExtractTextureDataAsync(pakFile);
            var textureReader = gameProvider.AssetReaders.GetReader<TextureData>(_pakEntry.FilePath);
            
            if (textureReader == null)
            {
                ShowError($"No texture reader available for {gameProvider.Name}.");
                return;
            }
            
            var result = textureReader.Read(textureData, _pakEntry.FilePath);
            if (!result.IsSuccess)
            {
                ShowError($"Failed to read texture: {result.Error}");
                return;
            }
            
            _textureData = result.Value!;
            _textureData.Compression = _pakEntry.CompressionType;
            _fullTextureData = textureData;
            
            PopulateMipmapSelector();
            UpdateTextureInfo();
            await DisplayTextureAsync(textureData);
            
            StatusText.Text = "Ready";
        }
        catch (Exception ex)
        {
            ShowError($"Error loading texture: {ex.Message}");
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
    
    private async Task<byte[]> ExtractTextureDataAsync(PakFile pakFile)
    {
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
        {
            throw new InvalidOperationException("No game provider loaded.");
        }
        
        var textureData = await Task.Run(() => gameProvider.PakReader.ExtractFile(pakFile, _pakEntry));
        _actualFileSize = textureData.Length;
        return textureData;
    }
    
    private void UpdateTextureInfo()
    {
        if (_textureData == null) return;
        
        FileNameText.Text = _fileName;
        
        DimensionsText.Text = GetDimensionsText();
        FormatText.Text = _textureData.Format.ToString();
        MipmapText.Text = _textureData.MipsPerImage.ToString();
        AlphaText.Text = HasAlpha() ? "Yes" : "No";
        DataSizeText.Text = FormatBytes(_actualFileSize > 0 ? _actualFileSize : _textureData.DataSize);
        CompressionText.Text = _textureData.Compression.ToString();
    }
    
    private string GetDimensionsText()
    {
        if (_displayedMipWidth > 0 && (_displayedMipWidth != _textureData!.Width || _displayedMipHeight != _textureData.Height))
        {
            return $"{_displayedMipWidth} x {_displayedMipHeight} (Full: {_textureData.Width} x {_textureData.Height}, Mip {_displayedMipLevel})";
        }
        return $"{_textureData!.Width} x {_textureData.Height}";
    }
    
    private bool HasAlpha()
    {
        var formatStr = _textureData!.Format.ToString();
        return formatStr.Contains("Bc") && (formatStr.Contains("3") || formatStr.Contains("7"));
    }
    
    private int GetBlockSize()
    {
        return _textureData!.Format switch
        {
            TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb or
            TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm => 8,
            _ => 16
        };
    }
    
    private CompressionFormat GetCompressionFormat()
    {
        return _textureData!.Format switch
        {
            TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb => CompressionFormat.Bc1,
            TextureFormat.Bc2Unorm => CompressionFormat.Bc2,
            TextureFormat.Bc3Unorm => CompressionFormat.Bc3,
            TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm => CompressionFormat.Bc4,
            TextureFormat.Bc5Unorm or TextureFormat.Bc5Snorm => CompressionFormat.Bc5,
            TextureFormat.Bc7Unorm or TextureFormat.Bc7UnormSrgb => CompressionFormat.Bc7,
            _ => CompressionFormat.Bc7
        };
    }
    
    private (int width, int height, int size) CalculateMipDimensions(int mipLevel)
    {
        int width = Math.Max(1, _textureData!.Width >> mipLevel);
        int height = Math.Max(1, _textureData.Height >> mipLevel);
        int blocksWide = Math.Max(1, (width + 3) / 4);
        int blocksHigh = Math.Max(1, (height + 3) / 4);
        int size = blocksWide * blocksHigh * GetBlockSize();
        return (width, height, size);
    }
    
    private async Task DisplayTextureAsync(byte[] textureData)
    {
        if (_textureData == null) return;
        
        try
        {
            if (_textureData.Mips.Length == 0)
            {
                ShowError("No mipmap data available.");
                return;
            }
            
            int headerSize = 0x28 + (_textureData.Mips.Length * 16);
            int mipDataSize = textureData.Length - headerSize;
            
            if (mipDataSize <= 0)
            {
                ShowError($"Invalid mip data size. Available: {textureData.Length - headerSize}");
                return;
            }
            
            var (mipLevel, mipWidth, mipHeight, actualMipSize) = FindBestMipLevel(mipDataSize);
            
            if (mipLevel == -1)
            {
                ShowError($"Cannot determine mip level. Data size: {mipDataSize} bytes doesn't match any mip level for {_textureData.Width}x{_textureData.Height}.");
                return;
            }
            
            bool isStreaming = mipLevel > 0 || (_textureData.StreamingFlags & 0x1) != 0;
            if (isStreaming)
            {
                StatusText.Text = $"Showing mip level {mipLevel} - Full resolution requires external streaming files (.9/.10)";
            }
            
            byte[] mipData = new byte[actualMipSize];
            Array.Copy(textureData, headerSize, mipData, 0, actualMipSize);
            
            var pixelData = await DecodeMipDataAsync(mipData, mipWidth, mipHeight);
            
            UpdateDisplayedMipInfo(mipWidth, mipHeight, mipLevel, pixelData);
            CreateAndDisplayBitmap(pixelData, mipWidth, mipHeight);
            AutoFitZoom(mipWidth, mipHeight);
        }
        catch (Exception ex)
        {
            ShowError($"Failed to display texture: {ex.Message}");
        }
    }
    
    private (int level, int width, int height, int size) FindBestMipLevel(int availableDataSize)
    {
        int bestMipLevel = -1;
        int bestMipSize = 0;
        
        for (int level = 0; level < _textureData!.MipsPerImage; level++)
        {
            var (width, height, size) = CalculateMipDimensions(level);
            
            if (size == availableDataSize)
            {
                return (level, width, height, size);
            }
            
            if (size <= availableDataSize && size > bestMipSize)
            {
                bestMipLevel = level;
                bestMipSize = size;
            }
        }
        
        if (bestMipLevel != -1)
        {
            var (width, height, _) = CalculateMipDimensions(bestMipLevel);
            StatusText.Text = $"Showing largest available mip (level {bestMipLevel}: {width}x{height}). File may contain multiple mips or extra data.";
            return (bestMipLevel, width, height, bestMipSize);
        }
        
        return (-1, 0, 0, 0);
    }
    
    private async Task<byte[]> DecodeMipDataAsync(byte[] mipData, int width, int height)
    {
        return await Task.Run(() =>
        {
            var decoder = new BcDecoder();
            var bcFormat = GetCompressionFormat();
            var decoded = decoder.DecodeRaw(mipData, width, height, bcFormat);
            
            if (decoded.Length != width * height)
            {
                throw new InvalidOperationException(
                    $"Decoded size mismatch. Expected: {width * height} pixels, Got: {decoded.Length}");
            }
            
            return ConvertToBgraPixelData(decoded, width, height);
        });
    }
    
    private byte[] ConvertToBgraPixelData(BCnEncoder.Shared.ColorRgba32[] decoded, int width, int height)
    {
        byte[] pixels = new byte[width * height * 4];
        int index = 0;
        
        for (int i = 0; i < decoded.Length; i++)
        {
            var pixel = decoded[i];
            pixels[index++] = pixel.b;
            pixels[index++] = pixel.g;
            pixels[index++] = pixel.r;
            pixels[index++] = pixel.a;
        }
        
        return pixels;
    }
    
    private void UpdateDisplayedMipInfo(int width, int height, int level, byte[] pixelData)
    {
        _originalPixelData = pixelData;
        _displayedMipWidth = width;
        _displayedMipHeight = height;
        _displayedMipLevel = level >= 0 ? level : 0;
        UpdateTextureInfo();
    }
    
    private void CreateAndDisplayBitmap(byte[] pixelData, int width, int height)
    {
        byte[] displayData = TransparencyToggle.IsChecked == true 
            ? pixelData 
            : RemoveAlpha(pixelData);
        
        var bitmapSource = BitmapSource.Create(
            width, height, 96, 96,
            PixelFormats.Bgra32, null,
            displayData, width * 4
        );
        
        bitmapSource.Freeze();
        TextureImage.Source = bitmapSource;
    }
    
    private void AutoFitZoom(int imageWidth, int imageHeight)
    {
        // Wait for the next layout pass to get accurate viewport size
        Dispatcher.InvokeAsync(() =>
        {
            // Get the ScrollViewer's viewport size
            double viewportWidth = ImageScrollViewer.ViewportWidth;
            double viewportHeight = ImageScrollViewer.ViewportHeight;
            
            if (viewportWidth <= 0 || viewportHeight <= 0)
            {
                // Viewport not ready, use a default zoom
                SetZoom(1.0);
                return;
            }
            
            // Calculate zoom to fit
            double zoomToFitWidth = viewportWidth / imageWidth;
            double zoomToFitHeight = viewportHeight / imageHeight;
            
            // Use the smaller zoom to ensure image fits in both dimensions
            double fitZoom = Math.Min(zoomToFitWidth, zoomToFitHeight);
            
            // Clamp to min/max zoom limits and apply a small margin (95%)
            fitZoom = Math.Clamp(fitZoom * 0.95, MinZoom, MaxZoom);
            
            SetZoom(fitZoom);
        }, System.Windows.Threading.DispatcherPriority.Loaded);
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
    
    // Zoom controls
    private void ZoomIn_Click(object sender, RoutedEventArgs e)
    {
        SetZoom(_zoomLevel + ZoomIncrement);
    }
    
    private void ZoomOut_Click(object sender, RoutedEventArgs e)
    {
        SetZoom(_zoomLevel - ZoomIncrement);
    }
    
    private void ZoomReset_Click(object sender, RoutedEventArgs e)
    {
        SetZoom(1.0);
    }
    
    private void ImageScrollViewer_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (Keyboard.Modifiers == ModifierKeys.Control)
        {
            e.Handled = true;
            double delta = e.Delta > 0 ? ZoomIncrement : -ZoomIncrement;
            SetZoom(_zoomLevel + delta);
        }
    }
    
    private void SetZoom(double zoom)
    {
        zoom = Math.Clamp(zoom, MinZoom, MaxZoom);
        _zoomLevel = zoom;
        
        ImageScaleTransform.ScaleX = zoom;
        ImageScaleTransform.ScaleY = zoom;
    }
    
    private void ToggleTransparency_Changed(object sender, RoutedEventArgs e)
    {
        if (_originalPixelData == null || _textureData == null) return;
        
        byte[] displayData = TransparencyToggle.IsChecked == true 
            ? _originalPixelData 
            : RemoveAlpha(_originalPixelData);
        
        var bitmapSource = BitmapSource.Create(
            _textureData.Width,
            _textureData.Height,
            96,
            96,
            PixelFormats.Bgra32,
            null,
            displayData,
            _textureData.Width * 4
        );
        
        bitmapSource.Freeze();
        TextureImage.Source = bitmapSource;
        
        CheckerboardBackground.Visibility = TransparencyToggle.IsChecked == true 
            ? Visibility.Visible 
            : Visibility.Collapsed;
    }
    
    private byte[] RemoveAlpha(byte[] pixelData)
    {
        byte[] result = new byte[pixelData.Length];
        
        for (int i = 0; i < pixelData.Length; i += 4)
        {
            result[i] = pixelData[i];       // B
            result[i + 1] = pixelData[i + 1]; // G
            result[i + 2] = pixelData[i + 2]; // R
            result[i + 3] = 255;              // A (fully opaque)
        }
        
        return result;
    }
    
    private void ExportAsPng_Click(object sender, RoutedEventArgs e)
    {
        ExportTexture("PNG");
    }
    
    private void ExportAsDds_Click(object sender, RoutedEventArgs e)
    {
        ExportTexture("DDS");
    }
    
    private void ExportAsTga_Click(object sender, RoutedEventArgs e)
    {
        ExportTexture("TGA");
    }
    
    private void ExportTexture(string format)
    {
        if (TextureImage.Source == null)
        {
            var errorWindow = new StatusWindow(StatusType.Warning, "No texture loaded to export.");
            errorWindow.ShowDialog();
            return;
        }
        
        var saveDialog = new WpfSaveFileDialog
        {
            FileName = Path.GetFileNameWithoutExtension(_fileName),
            Filter = format switch
            {
                "PNG" => "PNG Files (*.png)|*.png",
                "DDS" => "DDS Files (*.dds)|*.dds",
                "TGA" => "TGA Files (*.tga)|*.tga",
                _ => "All Files (*.*)|*.*"
            },
            DefaultExt = format.ToLower()
        };
        
        if (saveDialog.ShowDialog() == true)
        {
            StatusWindow? progressWindow = null;
            try
            {
                progressWindow = new StatusWindow(StatusType.Loading, $"Exporting texture as {format}...");
                progressWindow.Show();
                
                switch (format)
                {
                    case "PNG":
                        ExportAsPngFile(saveDialog.FileName);
                        break;
                    case "DDS":
                        ExportAsDdsFile(saveDialog.FileName);
                        break;
                    case "TGA":
                        ExportAsTgaFile(saveDialog.FileName);
                        break;
                }
                
                progressWindow.Close();
                
                var successWindow = new StatusWindow(StatusType.Success, $"Texture exported successfully to {format}!");
                successWindow.ShowDialog();
            }
            catch (Exception ex)
            {
                progressWindow?.Close();
                
                var errorWindow = new StatusWindow(StatusType.Error, $"Failed to export texture: {ex.Message}");
                errorWindow.ShowDialog();
            }
        }
    }
    
    private void ExportAsPngFile(string filePath)
    {
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create((BitmapSource)TextureImage.Source));
        
        using var stream = new FileStream(filePath, FileMode.Create);
        encoder.Save(stream);
    }
    
    private void ExportAsDdsFile(string filePath)
    {
        if (_textureData == null || _fullTextureData == null || _fullTextureData.Length == 0)
        {
            throw new InvalidOperationException("No texture data available to export.");
        }

        using var stream = new FileStream(filePath, FileMode.Create);
        using var writer = new BinaryWriter(stream);

        // Calculate how many mipmaps we can export from the available data
        int headerSize = 0x28 + (_textureData.Mips.Length * 16);
        int availableDataSize = _fullTextureData.Length - headerSize;
        
        int exportMipCount = 0;
        int requiredSize = 0;
        for (int i = 0; i < _textureData.MipsPerImage; i++)
        {
            var (_, _, mipSize) = CalculateMipDimensions(i);
            if (requiredSize + mipSize <= availableDataSize)
            {
                exportMipCount++;
                requiredSize += mipSize;
            }
            else
            {
                break;
            }
        }

        WriteDdsHeader(writer, _textureData.Width, _textureData.Height, _textureData.Format, exportMipCount);

        for (int mipLevel = 0; mipLevel < exportMipCount; mipLevel++)
        {
            var (mipWidth, mipHeight, mipSize) = CalculateMipDimensions(mipLevel);
            int mipOffset = CalculateMipOffset(headerSize, mipLevel);
            
            byte[] mipData = new byte[mipSize];
            Array.Copy(_fullTextureData, mipOffset, mipData, 0, mipSize);
            
            var decoder = new BcDecoder();
            var bcFormat = GetCompressionFormat();
            var decoded = decoder.DecodeRaw(mipData, mipWidth, mipHeight, bcFormat);
            var pixelData = ConvertToBgraPixelData(decoded, mipWidth, mipHeight);
            
            // Convert BGRA to RGBA for DDS
            byte[] rgbaData = new byte[pixelData.Length];
            for (int i = 0; i < pixelData.Length; i += 4)
            {
                rgbaData[i] = pixelData[i + 2];     // R
                rgbaData[i + 1] = pixelData[i + 1]; // G
                rgbaData[i + 2] = pixelData[i];     // B
                rgbaData[i + 3] = pixelData[i + 3]; // A
            }
            
            writer.Write(rgbaData);
        }
    }

    private void WriteDdsHeader(BinaryWriter writer, int width, int height, TextureFormat format, int mipMapCount = 1)
    {
        // Magic number "DDS "
        writer.Write(0x20534444);

        // DDS_HEADER
        writer.Write(124); // dwSize
        
        uint flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000; // CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
        if (mipMapCount > 1)
            flags |= 0x20000; // MIPMAPCOUNT
        
        writer.Write(flags); // dwFlags
        writer.Write(height); // dwHeight
        writer.Write(width); // dwWidth
        writer.Write(width * height * 4); // dwPitchOrLinearSize
        writer.Write(0); // dwDepth
        writer.Write(mipMapCount); // dwMipMapCount
        
        // dwReserved1[11]
        for (int i = 0; i < 11; i++)
            writer.Write(0);

        // DDS_PIXELFORMAT
        writer.Write(32); // dwSize
        writer.Write(0x41); // dwFlags (RGBA)
        writer.Write(0); // dwFourCC
        writer.Write(32); // dwRGBBitCount
        writer.Write(0x000000FF); // dwRBitMask (R)
        writer.Write(0x0000FF00); // dwGBitMask (G)
        writer.Write(0x00FF0000); // dwBBitMask (B)
        writer.Write(0xFF000000); // dwABitMask (A)

        // dwCaps
        uint caps = 0x1000; // DDSCAPS_TEXTURE
        if (mipMapCount > 1)
            caps |= 0x400000 | 0x8; // DDSCAPS_MIPMAP | DDSCAPS_COMPLEX
        
        writer.Write(caps);

        // dwCaps2, dwCaps3, dwCaps4
        writer.Write(0);
        writer.Write(0);
        writer.Write(0);

        // dwReserved2
        writer.Write(0);
    }
    
    private void ExportAsTgaFile(string filePath)
    {
        if (_originalPixelData == null)
        {
            throw new InvalidOperationException("No texture data available to export.");
        }

        using var stream = new FileStream(filePath, FileMode.Create);
        using var writer = new BinaryWriter(stream);

        // TGA Header
        writer.Write((byte)0);  // ID length
        writer.Write((byte)0);  // Color map type
        writer.Write((byte)2);  // Image type (uncompressed true-color)
        writer.Write((short)0); // Color map first entry index
        writer.Write((short)0); // Color map length
        writer.Write((byte)0);  // Color map entry size
        writer.Write((short)0); // X-origin
        writer.Write((short)0); // Y-origin
        writer.Write((short)_displayedMipWidth);  // Width
        writer.Write((short)_displayedMipHeight); // Height
        writer.Write((byte)32); // Pixel depth (32-bit BGRA)
        writer.Write((byte)8);  // Image descriptor (8-bit alpha)

        // Write pixel data (TGA uses BGRA format, which matches our data)
        writer.Write(_originalPixelData);
    }
    
    private void ExportDropdown_Click(object sender, RoutedEventArgs e)
    {
        var button = sender as System.Windows.Controls.Button;
        if (button?.ContextMenu != null)
        {
            button.ContextMenu.PlacementTarget = button;
            button.ContextMenu.IsOpen = true;
        }
    }
    
    private void PopulateMipmapSelector()
    {
        if (_textureData == null) return;
        
        MipmapSelector.Items.Clear();
        
        for (int i = 0; i < _textureData.MipsPerImage; i++)
        {
            int mipWidth = Math.Max(1, _textureData.Width >> i);
            int mipHeight = Math.Max(1, _textureData.Height >> i);
            MipmapSelector.Items.Add($"Mip {i}: {mipWidth}x{mipHeight}");
        }
        
        if (_displayedMipLevel >= 0 && _displayedMipLevel < MipmapSelector.Items.Count)
        {
            MipmapSelector.SelectedIndex = _displayedMipLevel;
        }
    }
    
    private async void MipmapSelector_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (MipmapSelector.SelectedIndex < 0 || _textureData == null || _fullTextureData.Length == 0)
            return;
        
        int selectedMipLevel = MipmapSelector.SelectedIndex;
        
        if (selectedMipLevel == _displayedMipLevel)
            return;
        
        try
        {
            LoadingIndicator.Visibility = Visibility.Visible;
            StatusText.Text = $"Loading mip level {selectedMipLevel}...";
            
            await DisplaySpecificMipAsync(selectedMipLevel);
            
            StatusText.Text = $"Showing mip level {selectedMipLevel}";
        }
        catch (Exception ex)
        {
            ShowError($"Failed to load mip {selectedMipLevel}: {ex.Message}");
        }
        finally
        {
            LoadingIndicator.Visibility = Visibility.Collapsed;
        }
    }
    
    private async Task DisplaySpecificMipAsync(int mipLevel)
    {
        if (_textureData == null) return;
        
        int headerSize = 0x28 + (_textureData.Mips.Length * 16);
        var (mipWidth, mipHeight, mipDataSize) = CalculateMipDimensions(mipLevel);
        
        int mipOffset = CalculateMipOffset(headerSize, mipLevel);
        
        if (mipOffset + mipDataSize > _fullTextureData.Length)
        {
            ShowError($"Mip {mipLevel} data not available in file. May require external streaming files (.9/.10)");
            return;
        }
        
        byte[] mipData = new byte[mipDataSize];
        Array.Copy(_fullTextureData, mipOffset, mipData, 0, mipDataSize);
        
        var pixelData = await DecodeMipDataAsync(mipData, mipWidth, mipHeight);
        
        UpdateDisplayedMipInfo(mipWidth, mipHeight, mipLevel, pixelData);
        CreateAndDisplayBitmap(pixelData, mipWidth, mipHeight);
        AutoFitZoom(mipWidth, mipHeight);
    }
    
    private int CalculateMipOffset(int headerSize, int targetMipLevel)
    {
        int offset = headerSize;
        for (int i = 0; i < targetMipLevel; i++)
        {
            var (_, _, mipSize) = CalculateMipDimensions(i);
            offset += mipSize;
        }
        return offset;
    }
}
