using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Wpf.Ui.Controls;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI.Helpers;
using System.Windows.Threading;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;

namespace REAssetExplorer.UI.Views;

public enum MaterialTextureType
{
    BaseMetalMap,           // RGB = Albedo, A = Metallic
    NormalRoughnessMap,     // RGB = Normal, A = Roughness
    AlphaTranslucentOcclusionSSS,  // R = Alpha, G = Translucency, B = Occlusion, A = SSS
    IntensityMap            // R = Emissive?, G = SSS Intensity?, B = Specular Intensity?, A = Mask?
}

public partial class MaterialViewerWindow : FluentWindow
{
    private readonly MaterialData _materialData;
    private readonly int _materialIndex;
    private WriteableBitmap? _writeableBitmap;
    private List<TextureInfo>? _texturesList;
    
    private bool _isLeftMouseDown = false;
    private bool _isRightMouseDown = false;
    private Point _lastMousePosition;

    public MaterialViewerWindow(MaterialData materialData, int materialIndex = 0)
    {
        InitializeComponent();
        _materialData = materialData;
        _materialIndex = materialIndex;

        Loaded += MaterialViewerWindow_Loaded;
        Closing += MaterialViewerWindow_Closing;
        SizeChanged += MaterialViewerWindow_SizeChanged;
        
        // Pause rendering when window is not active
        /*Activated += (s, e) => _renderTimer?.Start();
        Deactivated += (s, e) => _renderTimer?.Stop();*/
        
        // Add mouse event handlers for 3D control
        RenderBorder.MouseLeftButtonDown += RenderBorder_MouseLeftButtonDown;
        RenderBorder.MouseLeftButtonUp += RenderBorder_MouseLeftButtonUp;
        RenderBorder.MouseRightButtonDown += RenderBorder_MouseRightButtonDown;
        RenderBorder.MouseRightButtonUp += RenderBorder_MouseRightButtonUp;
        RenderBorder.MouseMove += RenderBorder_MouseMove;
    }

    private void MaterialViewerWindow_Loaded(object sender, RoutedEventArgs e)
    {
        LoadMaterialInfo();
        InitializeRenderer();
        ApplyMaterialToRenderer();
    }

    private void LoadMaterialInfo()
    {
        if (_materialIndex >= _materialData.MaterialHeaders.Length)
            return;

        var material = _materialData.MaterialHeaders[_materialIndex];

        // Basic information
        MaterialNameText.Text = material.MaterialName;
        ShaderTypeText.Text = $"{material.ShaderType}";
        MasterMaterialText.Text = material.MasterMaterialFilePath;
        
        // Counts
        TextureCountText.Text = material.TextureCount.ToString();
        PropertyCountText.Text = material.PropertyCount.ToString();

        // Flags
        var flagsList = new List<string>();
        foreach (MaterialFlags flag in Enum.GetValues(typeof(MaterialFlags)))
        {
            if (material.Flags.HasFlag(flag))
            {
                flagsList.Add(flag.ToString());
            }
        }
        FlagsListBox.ItemsSource = flagsList;
        FlagsCountText.Text = $"{flagsList.Count} active";

        // Textures with preview
        if (_materialData.TextureHeaders != null && 
            _materialIndex < _materialData.TextureHeaders.Length &&
            _materialData.TextureHeaders[_materialIndex] != null)
        {
            _texturesList = new List<TextureInfo>();
            foreach (var tex in _materialData.TextureHeaders[_materialIndex])
            {
                var textureInfo = new TextureInfo
                {
                    Type = tex.TextureType,
                    Path = tex.TextureFilePath
                };
                
                // Try to load texture preview asynchronously
                LoadTexturePreview(textureInfo, tex.TextureFilePath);
                
                _texturesList.Add(textureInfo);
            }
            TexturesListBox.ItemsSource = _texturesList;
        }

        // Properties with editable parameters
        if (_materialData.PropertyHeaders != null && 
            _materialIndex < _materialData.PropertyHeaders.Length &&
            _materialData.PropertyHeaders[_materialIndex] != null)
        {
            var propertiesList = new List<PropertyInfo>();
            foreach (var prop in _materialData.PropertyHeaders[_materialIndex])
            {
                var propertyInfo = new PropertyInfo
                {
                    Name = prop.Name
                };
                
                if (prop.Parameters != null && prop.Parameters.Length > 0)
                {
                    for (int i = 0; i < prop.Parameters.Length; i++)
                    {
                        string label = prop.Parameters.Length == 1 ? "Value:" :
                                      prop.Parameters.Length == 2 ? (i == 0 ? "X:" : "Y:") :
                                      prop.Parameters.Length == 3 ? (i == 0 ? "X:" : i == 1 ? "Y:" : "Z:") :
                                      prop.Parameters.Length == 4 ? (i == 0 ? "X:" : i == 1 ? "Y:" : i == 2 ? "Z:" : "W:") :
                                      $"[{i}]:";
                        
                        propertyInfo.Parameters.Add(new ParameterInfo
                        {
                            Label = label,
                            Value = prop.Parameters[i].ToString("F4"),
                            Index = i,
                            PropertyName = prop.Name
                        });
                    }
                }
                
                propertiesList.Add(propertyInfo);
            }
            PropertiesListBox.ItemsSource = propertiesList;
        }
    }

    private void ApplyMaterialToRenderer()
    {
       
    }

    private async void LoadMaterialTextures()
    {
        if (_materialIndex >= _materialData.TextureHeaders.Length)
            return;

        // Check if we have the textures list
        if (_texturesList == null)
            return;

        var textures = _materialData.TextureHeaders[_materialIndex];
        
        // Track which texture types we've found
        var foundTextureTypes = new HashSet<MaterialTextureType>();
        
        foreach (var texture in textures)
        {
            // Map texture type name to enum
            var textureType = GetMaterialTextureType(texture.TextureType);
            /*if (textureType == null)
                continue; // Unknown texture type, skip*/
            
            foundTextureTypes.Add(textureType.Value);
            
            // Find the corresponding TextureInfo to check if it's enabled
            var textureInfo = _texturesList.FirstOrDefault(t => t.Path == texture.TextureFilePath);
            
            if (textureInfo != null && textureInfo.IsEnabled)
            {
                // Load the actual texture
                await LoadTextureToRenderer(texture.TextureFilePath, textureType.Value);
            }
            else
            {
                // Load default texture
                /*_renderer?.SetTexture(textureType.Value, null, 1, 1);
                Console.WriteLine($"[Texture] {textureType.Value} disabled, using default");*/
            }
        }
        
        // Create defaults for any missing texture types
        var allTextureTypes = Enum.GetValues<MaterialTextureType>();
        foreach (var textureType in allTextureTypes)
        {
            if (!foundTextureTypes.Contains(textureType))
            {
                /*_renderer?.SetTexture(textureType, null, 1, 1);
                Console.WriteLine($"[Texture] No {textureType} found, using default");*/
            }
        }
    }

    private MaterialTextureType? GetMaterialTextureType(string textureTypeName)
    {
        // Map common texture type names to our enum
        if (textureTypeName.Contains("BaseMetalMap", StringComparison.OrdinalIgnoreCase) ||
            textureTypeName.Contains("ALBM", StringComparison.OrdinalIgnoreCase))
        {
            return MaterialTextureType.BaseMetalMap;
        }
        else if (textureTypeName.Contains("NormalRoughnessMap", StringComparison.OrdinalIgnoreCase) ||
                 textureTypeName.Contains("NRMR", StringComparison.OrdinalIgnoreCase))
        {
            return MaterialTextureType.NormalRoughnessMap;
        }
        else if (textureTypeName.Contains("AlphaTranslucentOcclusionSSS", StringComparison.OrdinalIgnoreCase) ||
                 textureTypeName.Contains("ATOS", StringComparison.OrdinalIgnoreCase))
        {
            return MaterialTextureType.AlphaTranslucentOcclusionSSS;
        }
        else if (textureTypeName.Contains("IntensityMap", StringComparison.OrdinalIgnoreCase) ||
                 textureTypeName.Contains("INTM", StringComparison.OrdinalIgnoreCase) ||
                 textureTypeName.Contains("Intensity", StringComparison.OrdinalIgnoreCase))
        {
            return MaterialTextureType.IntensityMap;
        }
        
        return null; // Unknown texture type
    }

    private async Task LoadTextureToRenderer(string texturePath, MaterialTextureType textureType)
    {
        try
        {
            var gameProvider = GameManager.CurrentGameProvider;
            if (gameProvider == null)
            {
                Console.WriteLine($"[Texture] No game provider available");
                return;
            }

            string texturePathLower = texturePath.ToLowerInvariant();
            var pakFiles = GameManager.LoadedPakFiles;
            
            PakFile? foundPakFile = null;
            PakEntry? foundEntry = null;

            // Find the texture using partial path
            foreach (var pakFile in pakFiles.Values)
            {
                foundEntry = pakFile.FindEntryByPartialPath(texturePathLower);
                if (foundEntry != null)
                {
                    foundPakFile = pakFile;
                    break;
                }
            }

            if (foundEntry == null || foundPakFile == null)
            {
                Console.WriteLine($"[Texture] Not found: {texturePath}");
                return;
            }

            await Task.Run(() =>
            {
                try
                {
                    // Extract and read texture
                    byte[] textureData = gameProvider.PakReader.ExtractFile(foundPakFile, foundEntry.Value);
                    
                    var textureReader = gameProvider.AssetReaders.GetReader<TextureData>(foundEntry.Value.FilePath ?? "");
                    if (textureReader == null)
                    {
                        Console.WriteLine($"[Texture] No reader available for: {texturePath}");
                        return;
                    }

                    var result = textureReader.Read(textureData, foundEntry.Value.FilePath ?? "");
                    if (!result.IsSuccess || result.Value == null)
                    {
                        Console.WriteLine($"[Texture] Failed to read: {texturePath}");
                        return;
                    }

                    var texture = result.Value;
                    
                    if (texture.Mips == null || texture.Mips.Length == 0)
                    {
                        Console.WriteLine($"[Texture] No mips available: {texturePath}");
                        return;
                    }

                    // Get first mip
                    var mipHeader = texture.Mips[0];
                    
                    // Calculate the correct size for BCn compressed data
                    var bcFormat = ConvertTextureFormat(texture.Format);
                    int blockSize = GetBlockSize(bcFormat);
                    int blocksWide = Math.Max(1, (texture.Width + 3) / 4);
                    int blocksHigh = Math.Max(1, (texture.Height + 3) / 4);
                    int calculatedSize = blocksWide * blocksHigh * blockSize;
                    
                    // Use calculated size and offset from header
                    byte[] mipData = new byte[calculatedSize];
                    Array.Copy(textureData, (int)mipHeader.Offset, mipData, 0, Math.Min(calculatedSize, textureData.Length - (int)mipHeader.Offset));
                    
                    var decoder = new BcDecoder();
                    var decoded2D = decoder.DecodeRaw2D(mipData, texture.Width, texture.Height, bcFormat);
                    
                    // Convert to pixel array
                    byte[] pixels = new byte[texture.Width * texture.Height * 4];
                    int index = 0;
                    
                    for (int y = 0; y < texture.Height; y++)
                    {
                        var rowSpan = decoded2D.Span.GetRowSpan(y);
                        for (int x = 0; x < texture.Width; x++)
                        {
                            var pixel = rowSpan[x];
                            pixels[index++] = pixel.b;
                            pixels[index++] = pixel.g;
                            pixels[index++] = pixel.r;
                            pixels[index++] = pixel.a;
                        }
                    }

                    // Pass to renderer (must be called from UI thread)
                    /*Dispatcher.Invoke(() =>
                    {
                        _renderer?.SetTexture(textureType, pixels, texture.Width, texture.Height);
                        Console.WriteLine($"[Texture] Loaded {textureType}: {texture.Width}x{texture.Height}");
                    });*/
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Texture] Error loading {texturePath}: {ex.Message}");
                }
            });
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Texture] Error loading {texturePath}: {ex.Message}");
        }
    }
    private async void LoadTexturePreview(TextureInfo textureInfo, string texturePath)
    {
        try
        {
            var gameProvider = GameManager.CurrentGameProvider;
            if (gameProvider == null)
            {
                return;
            }

            // Convert to lowercase for case-insensitive matching
            string texturePathLower = texturePath.ToLowerInvariant();

            // Find the texture file in PAK
            var pakFiles = GameManager.LoadedPakFiles;
            
            PakFile? foundPakFile = null;
            PakEntry? foundEntry = null;

            // Try to find the texture using the partial path
            foreach (var pakFile in pakFiles.Values)
            {
                foundEntry = pakFile.FindEntryByPartialPath(texturePathLower);

                if (foundEntry != null && !string.IsNullOrEmpty(foundEntry.Value.FilePath))
                {
                    foundPakFile = pakFile;
                    break;
                }
            }

            if (foundPakFile == null || foundEntry == null)
            {
                return;
            }
            
            // Extract and decode texture in background thread
            var (success, bitmap) = await Task.Run(() =>
            {
                try
                {
                    // Extract texture data
                    byte[] textureData = gameProvider.PakReader.ExtractFile(foundPakFile, foundEntry.Value);
                    
                    var textureReader = gameProvider.AssetReaders.GetReader<TextureData>(foundEntry.Value.FilePath ?? "");
                    
                    if (textureReader == null)
                    {
                        return (false, (BitmapSource?)null);
                    }

                    var result = textureReader.Read(textureData, foundEntry.Value.FilePath ?? "");
                    if (!result.IsSuccess || result.Value == null)
                    {
                        return (false, (BitmapSource?)null);
                    }

                    // Get texture and first mip
                    var tex = result.Value;
                    
                    if (tex.Mips == null || tex.Mips.Length == 0)
                    {
                        return (false, (BitmapSource?)null);
                    }
                    
                    var mipHeader = tex.Mips[0];
                    
                    // Calculate the correct size for BCn compressed data
                    var bcFormat = ConvertTextureFormat(tex.Format);
                    int blockSize = GetBlockSize(bcFormat);
                    int blocksWide = Math.Max(1, (tex.Width + 3) / 4);
                    int blocksHigh = Math.Max(1, (tex.Height + 3) / 4);
                    int calculatedSize = blocksWide * blocksHigh * blockSize;
                    
                    // Use calculated size instead of header size
                    byte[] mipData = new byte[calculatedSize];
                    Array.Copy(textureData, (int)mipHeader.Offset, mipData, 0, Math.Min(calculatedSize, textureData.Length - (int)mipHeader.Offset));
                    var decoder = new BcDecoder();
                    var decoded2D = decoder.DecodeRaw2D(mipData, tex.Width, tex.Height, bcFormat);
                    
                    // Convert to pixel array (this uses Span)
                    byte[] pixels = new byte[tex.Width * tex.Height * 4];
                    int index = 0;
                    
                    for (int y = 0; y < tex.Height; y++)
                    {
                        var rowSpan = decoded2D.Span.GetRowSpan(y);
                        for (int x = 0; x < tex.Width; x++)
                        {
                            var pixel = rowSpan[x];
                            pixels[index++] = pixel.b;
                            pixels[index++] = pixel.g;
                            pixels[index++] = pixel.r;
                            pixels[index++] = 255; // Force alpha to 255 (fully opaque)
                        }
                    }
                    
                    var bmp = BitmapSource.Create(
                        tex.Width,
                        tex.Height,
                        96,
                        96,
                        PixelFormats.Bgra32,
                        null,
                        pixels,
                        tex.Width * 4);
                    
                    bmp.Freeze(); // Make it accessible from UI thread
                    
                    return (true, bmp);
                }
                catch (Exception ex)
                {
                    return (false, (BitmapSource?)null);
                }
            });
            
            // Update UI thread
            if (success && bitmap != null)
            {
                textureInfo.PreviewImage = bitmap;
                textureInfo.PreviewImageVisibility = Visibility.Collapsed;
            }
        }
        catch (Exception ex)
        {
            // ignored
        }
    }
    
    private CompressionFormat ConvertTextureFormat(TextureFormat format)
    {
        return format switch
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
    
    private int GetBlockSize(CompressionFormat format)
    {
        return format switch
        {
            CompressionFormat.Bc1 => 8,  // BC1 uses 8 bytes per 4x4 block
            CompressionFormat.Bc2 => 16, // BC2 uses 16 bytes per 4x4 block
            CompressionFormat.Bc3 => 16, // BC3 uses 16 bytes per 4x4 block
            CompressionFormat.Bc4 => 8,  // BC4 uses 8 bytes per 4x4 block
            CompressionFormat.Bc5 => 16, // BC5 uses 16 bytes per 4x4 block
            CompressionFormat.Bc7 => 16, // BC7 uses 16 bytes per 4x4 block
            _ => 16
        };
    }

    private void InitializeRenderer()
    {
        try
        {
            int width = (int)RenderBorder.ActualWidth;
            int height = (int)RenderBorder.ActualHeight;

            if (width <= 0 || height <= 0)
            {
                width = 800;
                height = 600;
            }

            // Initialize DirectX11 renderer
            //_renderer = new DirectX11Renderer(width, height);
            
            // Create writeable bitmap for WPF display
            _writeableBitmap = new WriteableBitmap(
                width, 
                height, 
                96, 
                96, 
                PixelFormats.Bgra32, 
                null);
            
            RenderImage.Source = _writeableBitmap;

            // Start render loop
            /*_renderTimer = new DispatcherTimer();
            _renderTimer.Interval = TimeSpan.FromMilliseconds(33); // ~30 FPS for better performance
            _renderTimer.Tick += RenderTimer_Tick;
            _renderTimer.Start();*/

            StatusText.Visibility = Visibility.Collapsed;
        }
        catch (Exception ex)
        {
            StatusText.Text = $"Failed to initialize DirectX11: {ex.Message}";
        }
    }

    private void RenderTimer_Tick(object? sender, EventArgs e)
    {
        /*if (_renderer == null || _writeableBitmap == null)
            return;

        try
        {
            // Render the scene
            _renderer.Render();

            // Copy the rendered image to WPF bitmap
            _writeableBitmap.Lock();
            _renderer.CopyToBuffer(_writeableBitmap.BackBuffer, _writeableBitmap.BackBufferStride);
            _writeableBitmap.AddDirtyRect(new Int32Rect(0, 0, _writeableBitmap.PixelWidth, _writeableBitmap.PixelHeight));
            _writeableBitmap.Unlock();
        }
        catch (Exception ex)
        {
            _renderTimer?.Stop();
            StatusText.Text = $"Render error: {ex.Message}";
            StatusText.Visibility = Visibility.Visible;
        }*/
    }

    private void MaterialViewerWindow_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        /*if (_renderer == null || !IsLoaded)
            return;

        int width = (int)RenderBorder.ActualWidth;
        int height = (int)RenderBorder.ActualHeight;

        if (width <= 0 || height <= 0)
            return;

        try
        {
            _renderer.Resize(width, height);
            
            _writeableBitmap = new WriteableBitmap(
                width, 
                height, 
                96, 
                96, 
                PixelFormats.Bgra32, 
                null);
            
            RenderImage.Source = _writeableBitmap;
        }
        catch (Exception ex)
        {
            StatusText.Text = $"Resize error: {ex.Message}";
            StatusText.Visibility = Visibility.Visible;
        }*/
    }

    private void ResetCameraButton_Click(object sender, RoutedEventArgs e)
    {
        //_renderer?.ResetCamera();
    }

    private void RenderBorder_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _isLeftMouseDown = true;
        _lastMousePosition = e.GetPosition(RenderBorder);
        RenderBorder.CaptureMouse();
    }

    private void RenderBorder_MouseLeftButtonUp(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _isLeftMouseDown = false;
        RenderBorder.ReleaseMouseCapture();
    }

    private void RenderBorder_MouseRightButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _isRightMouseDown = true;
        _lastMousePosition = e.GetPosition(RenderBorder);
        RenderBorder.CaptureMouse();
    }

    private void RenderBorder_MouseRightButtonUp(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _isRightMouseDown = false;
        RenderBorder.ReleaseMouseCapture();
    }

    private void RenderBorder_MouseMove(object sender, System.Windows.Input.MouseEventArgs e)
    {
        if (!_isLeftMouseDown && !_isRightMouseDown)
            return;

        Point currentPosition = e.GetPosition(RenderBorder);
        double deltaX = currentPosition.X - _lastMousePosition.X;
        double deltaY = currentPosition.Y - _lastMousePosition.Y;
        _lastMousePosition = currentPosition;

        if (_isLeftMouseDown)
        {
            // Rotate camera around the sphere (inverted vertical)
            float sensitivity = 0.01f;
            //_renderer?.RotateCamera((float)deltaX * sensitivity, (float)deltaY * sensitivity);
        }
        else if (_isRightMouseDown)
        {
            // Change light direction (inverted vertical for light)
            float sensitivity = 0.01f;
            float deltaYaw = -(float)deltaX * sensitivity;
            float deltaPitch = -(float)deltaY * sensitivity; // Inverted for light
            
           // _renderer?.SetLightDirection(deltaYaw, deltaPitch);
        }
    }

    private void MaterialViewerWindow_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        //_renderTimer?.Stop();
        //_renderer?.Dispose();
    }

    private void TexturePreview_MouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2 && e.ChangedButton == System.Windows.Input.MouseButton.Left)
        {
            // Get the TextureInfo from the Border's Tag
            if (sender is FrameworkElement element && element.Tag is TextureInfo textureInfo)
            {
                OpenTextureViewer(textureInfo.Path);
            }
        }
    }

    private void TextureCheckBox_Changed(object sender, RoutedEventArgs e)
    {
        // Reload textures when checkbox state changes
        LoadMaterialTextures();
    }

    private void OpenTextureViewer(string texturePath)
    {
        try
        {
            var gameProvider = GameManager.CurrentGameProvider;
            if (gameProvider == null)
            {
                return;
            }

            // Convert to lowercase for case-insensitive matching
            string texturePathLower = texturePath.ToLowerInvariant();

            // Find the texture file in PAK using the same logic as LoadTexturePreview
            var pakFiles = GameManager.LoadedPakFiles;
            PakFile? foundPakFile = null;
            PakEntry? foundEntry = null;

            foreach (var pakFile in pakFiles.Values)
            {
                foundEntry = pakFile.FindEntryByPartialPath(texturePathLower);

                if (foundEntry != null && !string.IsNullOrEmpty(foundEntry.Value.FilePath))
                {
                    foundPakFile = pakFile;
                    break;
                }
            }

            if (foundPakFile == null || foundEntry == null)
            {
                return;
            }

            // Open TextureViewerWindow
            var textureViewer = new TextureViewerWindow(foundEntry.Value.FilePath, foundEntry.Value);
            textureViewer.Owner = this; // Set owner to keep it on top
            textureViewer.Show();
            textureViewer.Activate(); // Bring to front
        }
        catch (Exception ex)
        {
            // ignored
        }
    }

    private class TextureInfo : INotifyPropertyChanged
    {
        public string Type { get; set; } = "";
        public string Path { get; set; } = "";
        
        private bool _isEnabled = true;
        public bool IsEnabled
        {
            get => _isEnabled;
            set
            {
                _isEnabled = value;
                OnPropertyChanged();
            }
        }
        
        private ImageSource? _previewImage;
        public ImageSource? PreviewImage
        {
            get => _previewImage;
            set
            {
                _previewImage = value;
                OnPropertyChanged();
            }
        }
        
        private Visibility _previewImageVisibility = Visibility.Visible;
        public Visibility PreviewImageVisibility
        {
            get => _previewImageVisibility;
            set
            {
                _previewImageVisibility = value;
                OnPropertyChanged();
            }
        }

        public event PropertyChangedEventHandler? PropertyChanged;
        
        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }

    private class PropertyInfo
    {
        public string Name { get; set; } = "";
        public List<ParameterInfo> Parameters { get; set; } = new();
        
        public Visibility NoParametersVisibility => Parameters.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }
    
    private class ParameterInfo : INotifyPropertyChanged
    {
        public string Label { get; set; } = "";
        public int Index { get; set; }
        public string PropertyName { get; set; } = "";
        
        private string _value = "";
        public string Value
        {
            get => _value;
            set
            {
                if (_value != value)
                {
                    _value = value;
                    OnPropertyChanged();
                    //Console.WriteLine($"[{PropertyName}] {Label} = {value}");
                }
            }
        }

        public event PropertyChangedEventHandler? PropertyChanged;
        
        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
