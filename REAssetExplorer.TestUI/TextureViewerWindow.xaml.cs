using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;
using Microsoft.Win32;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.TestUI;

/// <summary>
/// Standalone window that decodes and displays a .tex asset from the loaded PAK files.
/// Supports mipmap navigation, alpha-channel toggle, zoom, and export to PNG / DDS / TGA.
/// </summary>
public partial class TextureViewerWindow : Window
{
    // ── Constants ────────────────────────────────────────────────────────────
    private const double ZoomStep = 0.25;
    private const double MinZoom  = 0.10;
    private const double MaxZoom  = 16.0;

    // ── Source identity ──────────────────────────────────────────────────────
    private readonly string   _fileName;
    private readonly PakEntry _pakEntry;

    // ── Decoded state ────────────────────────────────────────────────────────
    private TextureData? _textureData;
    private byte[]       _fullTextureData = Array.Empty<byte>();
    private long         _actualFileSize;

    // Per-displayed-mip
    private byte[]? _displayedBgraPixels;
    private int     _displayedMipWidth;
    private int     _displayedMipHeight;
    private int     _displayedMipLevel = -1;

    // ── Interaction state ────────────────────────────────────────────────────
    private double _zoom = 1.0;
    private bool   _showAlpha;
    private bool   _suppressMipChange;   // guard so programmatic SelectedIndex changes don't fire reloads

    // ── Construction ─────────────────────────────────────────────────────────

    public TextureViewerWindow(string fileName, PakEntry pakEntry)
    {
        InitializeComponent();

        _fileName = fileName;
        _pakEntry = pakEntry;
        TitleText.Text = $"Texture Viewer — {fileName}";

        StateChanged += (_, _) => UpdateMaxRestoreGlyph();
        Loaded       += OnWindowLoaded;
        ImageScrollViewer.PreviewMouseWheel += OnPreviewMouseWheel;
    }

    private async void OnWindowLoaded(object sender, RoutedEventArgs e) =>
        await LoadTextureAsync();

    // ── Loading ──────────────────────────────────────────────────────────────

    private async Task LoadTextureAsync()
    {
        if (string.IsNullOrEmpty(_pakEntry.FilePath))
        {
            ShowError("No texture data available.");
            return;
        }

        ShowLoading("Loading texture…");

        try
        {
            var provider = App.CurrentProvider;
            if (provider == null) { ShowError("No game loaded."); return; }

            var pakFile = FindPakFile(_pakEntry);
            if (pakFile == null) { ShowError("PAK file not found for this entry."); return; }

            var rawData = await Task.Run(() => provider.PakReader.ExtractFile(pakFile, _pakEntry));
            _actualFileSize = rawData.LongLength;

            var reader = provider.AssetReaders.GetReader<TextureData>(_pakEntry.FilePath);
            if (reader == null) { ShowError($"No texture reader for {provider.Name}."); return; }

            var result = reader.Read(rawData, _pakEntry.FilePath);
            if (result.IsFailure || result.Value == null)
            {
                ShowError($"Failed to parse texture: {result.Error}");
                return;
            }

            _textureData = result.Value;
            _textureData.Compression = _pakEntry.CompressionType;
            _fullTextureData = rawData;

            PopulateMipmapSelector();
            UpdateInfoPanel();
            await DisplayBestAvailableMipAsync();

            HideOverlays();
            StatusText.Text = "Ready";
        }
        catch (Exception ex)
        {
            ShowError($"Error loading texture:\n{ex.Message}");
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

    // ── Decoding helpers ─────────────────────────────────────────────────────

    /// <summary>
    /// Mip header is fixed-size: 0x28 base + 16 bytes per mip entry.
    /// </summary>
    private int GetHeaderSize() =>
        _textureData == null ? 0 : 0x28 + (_textureData.Mips.Length * 16);

    private int GetBlockSize() => _textureData?.Format switch
    {
        TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb or
        TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm => 8,
        _ => 16
    };

    private CompressionFormat GetCompressionFormat() => _textureData?.Format switch
    {
        TextureFormat.Bc1Unorm or TextureFormat.Bc1UnormSrgb => CompressionFormat.Bc1,
        TextureFormat.Bc2Unorm                                => CompressionFormat.Bc2,
        TextureFormat.Bc3Unorm                                => CompressionFormat.Bc3,
        TextureFormat.Bc4Unorm or TextureFormat.Bc4Snorm     => CompressionFormat.Bc4,
        TextureFormat.Bc5Unorm or TextureFormat.Bc5Snorm     => CompressionFormat.Bc5,
        TextureFormat.Bc7Unorm or TextureFormat.Bc7UnormSrgb => CompressionFormat.Bc7,
        _                                                     => CompressionFormat.Bc7,
    };

    private (int width, int height, int size) CalcMipDimensions(int level)
    {
        if (_textureData == null) return (0, 0, 0);
        int w = Math.Max(1, _textureData.Width  >> level);
        int h = Math.Max(1, _textureData.Height >> level);
        int blocksW = Math.Max(1, (w + 3) / 4);
        int blocksH = Math.Max(1, (h + 3) / 4);
        int size = blocksW * blocksH * GetBlockSize();
        return (w, h, size);
    }

    private int CalcMipOffset(int targetLevel)
    {
        int offset = GetHeaderSize();
        for (int i = 0; i < targetLevel; i++)
            offset += CalcMipDimensions(i).size;
        return offset;
    }

    private bool FormatHasAlpha() => _textureData?.Format switch
    {
        TextureFormat.Bc2Unorm
            or TextureFormat.Bc2UnormSrgb
            or TextureFormat.Bc3Unorm
            or TextureFormat.Bc3UnormSrgb
            or TextureFormat.Bc7Unorm
            or TextureFormat.Bc7UnormSrgb => true,
        _ => false,
    };

    /// <summary>
    /// Walks every mip level and picks the largest one that fits in the available bytes.
    /// .tex files often store only the smaller mips inline; high-res mips live in the
    /// streaming sidecars (.9, .10) and aren't present here.
    /// </summary>
    private (int level, int width, int height, int size, bool exact) FindBestAvailableMip(int availableBytes)
    {
        if (_textureData == null) return (-1, 0, 0, 0, false);

        int bestLevel = -1, bestSize = 0;
        int bestW = 0, bestH = 0;

        for (int level = 0; level < _textureData.MipsPerImage; level++)
        {
            var (w, h, size) = CalcMipDimensions(level);
            if (size == availableBytes) return (level, w, h, size, true);
            if (size <= availableBytes && size > bestSize)
            {
                bestSize  = size;
                bestLevel = level;
                bestW     = w;
                bestH     = h;
            }
        }
        return (bestLevel, bestW, bestH, bestSize, false);
    }

    private async Task<byte[]> DecodeBcAsync(byte[] mipData, int w, int h) =>
        await Task.Run(() =>
        {
            var decoder = new BcDecoder();
            var decoded = decoder.DecodeRaw(mipData, w, h, GetCompressionFormat());
            byte[] bgra = new byte[w * h * 4];
            int o = 0;
            for (int i = 0; i < decoded.Length; i++)
            {
                var p = decoded[i];
                bgra[o++] = p.b;
                bgra[o++] = p.g;
                bgra[o++] = p.r;
                bgra[o++] = p.a;
            }
            return bgra;
        });

    // ── Display ──────────────────────────────────────────────────────────────

    private async Task DisplayBestAvailableMipAsync()
    {
        if (_textureData == null) return;
        if (_textureData.Mips.Length == 0) { ShowError("No mipmap data available."); return; }

        int headerSize    = GetHeaderSize();
        int availableData = _fullTextureData.Length - headerSize;

        if (availableData <= 0) { ShowError("Texture data is too small."); return; }

        var (level, w, h, size, exact) = FindBestAvailableMip(availableData);
        if (level < 0)
        {
            ShowError($"Could not match available {availableData} bytes to any mip level for {_textureData.Width}×{_textureData.Height}.");
            return;
        }

        byte[] mipData = new byte[size];
        Array.Copy(_fullTextureData, headerSize, mipData, 0, size);
        await DisplayMipAsync(level, w, h, mipData);

        if (!exact || level > 0 || (_textureData.StreamingFlags & 0x1) != 0)
            StatusText.Text = $"Showing mip {level} ({w}×{h}). Full-res mips ship in .9/.10 streaming sidecars.";
    }

    private async Task DisplayMipAsync(int level, int w, int h, byte[] mipData)
    {
        try
        {
            ShowLoading($"Decoding mip {level} ({w}×{h})…");
            _displayedBgraPixels = await DecodeBcAsync(mipData, w, h);
            _displayedMipWidth   = w;
            _displayedMipHeight  = h;
            _displayedMipLevel   = level;
            RefreshBitmap();
            UpdateInfoPanel();
            FitToWindow();
            HideOverlays();
        }
        catch (Exception ex)
        {
            ShowError($"Failed to decode mip {level}:\n{ex.Message}");
        }
    }

    private void RefreshBitmap()
    {
        if (_displayedBgraPixels == null) return;

        var src = _showAlpha
            ? _displayedBgraPixels
            : StripAlpha(_displayedBgraPixels);

        var bitmap = BitmapSource.Create(
            _displayedMipWidth, _displayedMipHeight,
            96, 96,
            PixelFormats.Bgra32, null,
            src, _displayedMipWidth * 4);
        bitmap.Freeze();
        TextureImage.Source = bitmap;
    }

    private static byte[] StripAlpha(byte[] bgra)
    {
        // Returns a copy with alpha=255 so the image stays opaque on top of
        // the panel background. We keep _displayedBgraPixels untouched so the
        // toggle is reversible without re-decoding.
        var copy = new byte[bgra.Length];
        Buffer.BlockCopy(bgra, 0, copy, 0, bgra.Length);
        for (int i = 3; i < copy.Length; i += 4) copy[i] = 255;
        return copy;
    }

    // ── Info panel ───────────────────────────────────────────────────────────

    private void UpdateInfoPanel()
    {
        if (_textureData == null) return;

        FileNameText.Text   = _fileName;
        FilePathText.Text   = _pakEntry.FilePath ?? "";

        DimensionsText.Text = _displayedMipLevel > 0
            ? $"{_displayedMipWidth}×{_displayedMipHeight}  (full: {_textureData.Width}×{_textureData.Height}, mip {_displayedMipLevel})"
            : $"{_textureData.Width}×{_textureData.Height}";

        FormatText.Text       = _textureData.Format.ToString();
        MipmapText.Text       = _textureData.MipsPerImage.ToString();
        ImagesText.Text       = $"{_textureData.NumImages} image(s)";
        AlphaText.Text        = FormatHasAlpha() ? "Yes (format carries alpha)" : "No";
        DataSizeText.Text     = FormatBytes(_actualFileSize > 0 ? _actualFileSize : _textureData.DataSize);
        CompressionText.Text  = _textureData.Compression.ToString();
        StreamingText.Text    = $"0x{_textureData.StreamingFlags:X8}";
        VersionText.Text      = _textureData.Version.ToString();
    }

    private void PopulateMipmapSelector()
    {
        if (_textureData == null) return;

        _suppressMipChange = true;
        MipmapSelector.Items.Clear();
        for (int i = 0; i < _textureData.MipsPerImage; i++)
        {
            int w = Math.Max(1, _textureData.Width  >> i);
            int h = Math.Max(1, _textureData.Height >> i);
            MipmapSelector.Items.Add($"Mip {i}: {w}×{h}");
        }
        _suppressMipChange = false;
    }

    private static string FormatBytes(long bytes)
    {
        string[] u = { "B", "KB", "MB", "GB" };
        double s = bytes;
        int i = 0;
        while (s >= 1024 && i < u.Length - 1) { s /= 1024; i++; }
        return $"{s:0.##} {u[i]}";
    }

    // ── Overlays ─────────────────────────────────────────────────────────────

    private void ShowLoading(string msg)
    {
        LoadingText.Text         = msg;
        LoadingOverlay.Visibility = Visibility.Visible;
        ErrorOverlay.Visibility   = Visibility.Collapsed;
        StatusText.Text           = msg;
    }

    private void ShowError(string msg)
    {
        ErrorText.Text          = msg;
        ErrorOverlay.Visibility  = Visibility.Visible;
        LoadingOverlay.Visibility = Visibility.Collapsed;
        StatusText.Text           = "Error";
    }

    private void HideOverlays()
    {
        LoadingOverlay.Visibility = Visibility.Collapsed;
        ErrorOverlay.Visibility   = Visibility.Collapsed;
    }

    // ── Zoom ─────────────────────────────────────────────────────────────────

    private void SetZoom(double zoom)
    {
        _zoom = Math.Clamp(zoom, MinZoom, MaxZoom);
        ImageScaleTransform.ScaleX = _zoom;
        ImageScaleTransform.ScaleY = _zoom;
        ZoomText.Text = $"{(_zoom * 100):0}%";
    }

    private void FitToWindow()
    {
        Dispatcher.InvokeAsync(() =>
        {
            double vw = ImageScrollViewer.ViewportWidth;
            double vh = ImageScrollViewer.ViewportHeight;
            if (vw <= 0 || vh <= 0 || _displayedMipWidth <= 0 || _displayedMipHeight <= 0)
            {
                SetZoom(1.0);
                return;
            }
            double fit = Math.Min(vw / _displayedMipWidth, vh / _displayedMipHeight) * 0.95;
            SetZoom(fit);
        }, System.Windows.Threading.DispatcherPriority.Loaded);
    }

    private void OnZoomIn (object sender, RoutedEventArgs e) => SetZoom(_zoom + ZoomStep);
    private void OnZoomOut(object sender, RoutedEventArgs e) => SetZoom(_zoom - ZoomStep);
    private void OnZoomFit(object sender, RoutedEventArgs e) => FitToWindow();

    private void OnPreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (Keyboard.Modifiers != ModifierKeys.Control) return;
        SetZoom(_zoom + (e.Delta > 0 ? ZoomStep : -ZoomStep));
        e.Handled = true;
    }

    // ── Mipmap selector ──────────────────────────────────────────────────────

    private async void OnMipmapChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressMipChange) return;
        if (_textureData == null) return;
        if (MipmapSelector.SelectedIndex < 0) return;
        if (MipmapSelector.SelectedIndex == _displayedMipLevel) return;

        int level = MipmapSelector.SelectedIndex;
        int headerSize = GetHeaderSize();
        var (w, h, size) = CalcMipDimensions(level);
        int offset = CalcMipOffset(level);

        if (offset + size > _fullTextureData.Length)
        {
            ShowError($"Mip {level} not embedded — ships in .9/.10 streaming sidecars.");
            return;
        }

        byte[] mipData = new byte[size];
        Array.Copy(_fullTextureData, offset, mipData, 0, size);
        await DisplayMipAsync(level, w, h, mipData);
    }

    // ── Alpha toggle ─────────────────────────────────────────────────────────

    private void OnAlphaToggle(object sender, RoutedEventArgs e)
    {
        _showAlpha = !_showAlpha;
        AlphaToggleButton.Content = _showAlpha ? "On" : "Off";
        CheckerboardBackground.Visibility = _showAlpha ? Visibility.Visible : Visibility.Collapsed;
        RefreshBitmap();
    }

    // ── Export ───────────────────────────────────────────────────────────────

    private void OnExportPng(object sender, RoutedEventArgs e) => ExportAs("PNG");
    private void OnExportDds(object sender, RoutedEventArgs e) => ExportAs("DDS");
    private void OnExportTga(object sender, RoutedEventArgs e) => ExportAs("TGA");

    private void OnExportDropdown(object sender, RoutedEventArgs e)
    {
        if (sender is Button btn && btn.ContextMenu != null)
        {
            btn.ContextMenu.PlacementTarget = btn;
            btn.ContextMenu.IsOpen          = true;
        }
    }

    private void ExportAs(string format)
    {
        if (_displayedBgraPixels == null || _textureData == null)
        {
            new StatusWindow(StatusType.Warning, "Nothing to export yet.").Show();
            return;
        }

        var dialog = new SaveFileDialog
        {
            FileName   = Path.GetFileNameWithoutExtension(_fileName),
            Filter     = format switch
            {
                "PNG" => "PNG Files (*.png)|*.png",
                "DDS" => "DDS Files (*.dds)|*.dds",
                "TGA" => "TGA Files (*.tga)|*.tga",
                _     => "All Files (*.*)|*.*",
            },
            DefaultExt = format.ToLowerInvariant(),
        };

        if (dialog.ShowDialog() != true) return;

        try
        {
            switch (format)
            {
                case "PNG": ExportPng(dialog.FileName); break;
                case "DDS": ExportDds(dialog.FileName); break;
                case "TGA": ExportTga(dialog.FileName); break;
            }
            new StatusWindow(StatusType.Success, $"Exported {format} to:\n{dialog.FileName}").Show();
        }
        catch (Exception ex)
        {
            new StatusWindow(StatusType.Error, $"Export failed:\n{ex.Message}").Show();
        }
    }

    private void ExportPng(string path)
    {
        if (TextureImage.Source is not BitmapSource bmp) return;
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bmp));
        using var stream = File.Create(path);
        encoder.Save(stream);
    }

    private void ExportTga(string path)
    {
        if (_displayedBgraPixels == null) return;

        using var stream = File.Create(path);
        using var w = new BinaryWriter(stream);

        // TGA uncompressed BGRA32 header.
        w.Write((byte)0);                          // ID length
        w.Write((byte)0);                          // colour map type
        w.Write((byte)2);                          // image type: true-color
        w.Write((short)0); w.Write((short)0); w.Write((byte)0); // colour map spec
        w.Write((short)0); w.Write((short)0);      // origin
        w.Write((short)_displayedMipWidth);
        w.Write((short)_displayedMipHeight);
        w.Write((byte)32);                         // pixel depth
        w.Write((byte)0x28);                       // image descriptor: alpha=8 bits + top-left origin

        // Our buffer is BGRA which matches TGA's native byte order.
        w.Write(_displayedBgraPixels);
    }

    private void ExportDds(string path)
    {
        if (_textureData == null || _fullTextureData.Length == 0) return;

        using var stream = File.Create(path);
        using var w      = new BinaryWriter(stream);

        // Count how many mips actually fit inside the embedded data.
        int headerSize = GetHeaderSize();
        int available  = _fullTextureData.Length - headerSize;
        int mipCount   = 0;
        int needed     = 0;
        for (int i = 0; i < _textureData.MipsPerImage; i++)
        {
            int s = CalcMipDimensions(i).size;
            if (needed + s > available) break;
            needed += s;
            mipCount++;
        }
        if (mipCount == 0) throw new InvalidOperationException("No mips embedded in file.");

        WriteDdsHeader(w, _textureData.Width, _textureData.Height, mipCount);

        // Decode each embedded mip and write as RGBA32 (so the DDS doesn't need a
        // BC decoder to view). Bigger output but maximally compatible.
        for (int level = 0; level < mipCount; level++)
        {
            var (mw, mh, msize) = CalcMipDimensions(level);
            int off = CalcMipOffset(level);
            byte[] mip = new byte[msize];
            Array.Copy(_fullTextureData, off, mip, 0, msize);

            var decoder = new BcDecoder();
            var decoded = decoder.DecodeRaw(mip, mw, mh, GetCompressionFormat());
            byte[] rgba = new byte[mw * mh * 4];
            int o = 0;
            for (int i = 0; i < decoded.Length; i++)
            {
                var p = decoded[i];
                rgba[o++] = p.r;
                rgba[o++] = p.g;
                rgba[o++] = p.b;
                rgba[o++] = p.a;
            }
            w.Write(rgba);
        }
    }

    private static void WriteDdsHeader(BinaryWriter w, int width, int height, int mipCount)
    {
        w.Write(0x20534444);             // 'DDS '
        w.Write(124);                    // dwSize
        uint flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
        if (mipCount > 1) flags |= 0x20000;
        w.Write(flags);
        w.Write(height);
        w.Write(width);
        w.Write(width * height * 4);     // pitch
        w.Write(0);                      // depth
        w.Write(mipCount);
        for (int i = 0; i < 11; i++) w.Write(0); // reserved

        // DDS_PIXELFORMAT — uncompressed RGBA32
        w.Write(32);
        w.Write(0x41);                   // DDPF_RGB | DDPF_ALPHAPIXELS
        w.Write(0);                      // fourCC
        w.Write(32);                     // bit count
        w.Write(0x000000FFu);            // R
        w.Write(0x0000FF00u);            // G
        w.Write(0x00FF0000u);            // B
        w.Write(0xFF000000u);            // A

        uint caps = 0x1000;
        if (mipCount > 1) caps |= 0x400000 | 0x8;
        w.Write(caps);
        w.Write(0); w.Write(0); w.Write(0); w.Write(0); // caps2-4 + reserved2
    }

    // ── Window chrome ────────────────────────────────────────────────────────

    private void UpdateMaxRestoreGlyph() =>
        MaxRestoreButton.Content = WindowState == WindowState.Maximized ? "" : "";

    private void OnMinimize  (object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void OnMaxRestore(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void OnClose     (object sender, RoutedEventArgs e) => Close();
}
