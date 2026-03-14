using System;
using System.Collections.Generic;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Games.RE7;
using REAssetExplorer.Games.RE8;
using REAssetExplorer.RenderTest2.Scene;

namespace REAssetExplorer.RenderTest2;

public class RenderWindow : Form
{
    private Renderer? _renderer;
    private Camera? _camera;
    
    // Input state
    private HashSet<Keys> _pressedKeys = new HashSet<Keys>();
    private EventHandler? _idleHandler;
    
    private bool AppStillIdle
    {
        get
        {
            NativeMethods.PeekMessage(out NativeMethods.Message msg, IntPtr.Zero, 0, 0, 0);
            return !NativeMethods.PeekMessage(out msg, IntPtr.Zero, 0, 0, 0);
        }
    }

    public RenderWindow()
    {
        Text = "REAssetExplorer - DX12 Renderer Test";
        ClientSize = new Size(1280, 720);
        
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.Opaque, true);
        
        Load += OnLoad;
        FormClosing += OnFormClosing;
        Resize += OnResize;
        
        // Mouse events
        MouseDown += OnMouseDown;
        MouseMove += OnMouseMove;
        MouseUp += OnMouseUp;
        MouseWheel += OnMouseWheel;
        
        // Keyboard events
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
        
        _idleHandler = (s, e) =>
        {
            while (AppStillIdle)
            {
                Render();
            }
        };
        Application.Idle += _idleHandler;
    }

    private async void OnLoad(object? sender, EventArgs e)
    {
        // Load pak files asynchronously to avoid blocking the UI thread
        Dictionary<string, PakFile>? pakFiles = null;
        RE7Provider? provider = null;
        MaterialsCache? materialsCache = null;

        try
        {
            provider = new RE7Provider();
            provider.GameDirectory = "D:\\SteamLibrary\\steamapps\\common\\RESIDENT EVIL 7 biohazard";
            
            var pakLoader = new PakLoader();
            var pakResult = await pakLoader.LoadPakFilesAsync(
                provider,
                "D:\\Projects\\REAssetExplorer\\FileLists\\re7.txt",
                cachePath: "Cache/Materials.bin",
                loadMaterials: true,
                onProgress: Console.WriteLine
            );
            
            if (pakResult.IsSuccess)
            {
                pakFiles = pakResult.Value;
                materialsCache = pakLoader.GetMaterialsCache();
                Console.WriteLine($"Loaded {pakFiles.Count} PAK files");
                Console.WriteLine($"Materials cache: {materialsCache?.Count ?? 0} materials");
            }
            else
            {
                Console.WriteLine($"Failed to load PAK files: {pakResult.Error}");
                MessageBox.Show($"Failed to load PAK files: {pakResult.Error}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close();
                return;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to load pak files: {ex.Message}");
            MessageBox.Show($"Failed to load pak files: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            Close();
            return;
        }

        if (pakFiles == null || provider == null)
        {
            MessageBox.Show("Failed to load PAK files. Cannot continue.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            Close();
            return;
        }

        // Now create renderer and camera after PAK files are loaded
        _renderer = new Renderer(pakFiles, provider, materialsCache);
        _camera = new Camera(ref _pressedKeys);
        _renderer.SetCamera(_camera);
        
        bool success = _renderer.Initialize(Handle, ClientSize.Width, ClientSize.Height);
        
        if (!success)
        {
            MessageBox.Show("Failed to initialize renderer", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            Close();
            return;
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            // Detener el loop de renderizado antes de cerrar
            if (_idleHandler != null)
            {
                Application.Idle -= _idleHandler;
                _idleHandler = null;
            }
            
            _renderer?.Shutdown();
            _renderer?.Dispose();
        }
        base.Dispose(disposing);
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        // Shutdown will be called in Dispose
    }

    private void OnResize(object? sender, EventArgs e)
    {
        if (ClientSize.Width > 0 && ClientSize.Height > 0)
        {
            _renderer?.Resize(ClientSize.Width, ClientSize.Height);
        }
    }

    private void Render()
    {
        if (_renderer?.IsInitialized == true)
        {
            _renderer.RenderFrame(ClientSize);
            
            Text = $"REAssetExplorer - Renderer Test | {_renderer.GetRenderStats()}";
            
            Invalidate();
        }
    }
    
    #region Window Mode Management
    
    private WindowMode _currentWindowMode = WindowMode.Windowed;
    private FormBorderStyle _previousBorderStyle;
    private FormWindowState _previousWindowState;
    private Size _previousSize;
    private Point _previousLocation;
    
    /// <summary>Gets current window mode</summary>
    public WindowMode GetWindowMode()
    {
        return _currentWindowMode;
    }
    
    /// <summary>Sets window mode</summary>
    public void SetWindowMode(WindowMode mode)
    {
        if (_currentWindowMode == mode)
            return;
            
        switch (mode)
        {
            case WindowMode.Windowed:
                SetWindowedMode();
                break;
            case WindowMode.Borderless:
                SetBorderlessMode();
                break;
            case WindowMode.Fullscreen:
                SetFullscreenMode();
                break;
        }
        
        _currentWindowMode = mode;
    }
    
    /// <summary>Check if in fullscreen mode</summary>
    public bool IsFullScreenMode()
    {
        return _currentWindowMode == WindowMode.Fullscreen;
    }
    
    /// <summary>Set fullscreen mode</summary>
    public void SetFullScreenMode(bool fullscreen)
    {
        SetWindowMode(fullscreen ? WindowMode.Fullscreen : WindowMode.Windowed);
    }
    
    /// <summary>Switch to borderless window mode</summary>
    public void SwitchBorderlessWindowMode()
    {
        if (_currentWindowMode == WindowMode.Borderless)
        {
            SetWindowMode(WindowMode.Windowed);
        }
        else
        {
            SetWindowMode(WindowMode.Borderless);
        }
    }
    
    private void SetWindowedMode()
    {
        FormBorderStyle = _previousBorderStyle != FormBorderStyle.None ? _previousBorderStyle : FormBorderStyle.Sizable;
        WindowState = FormWindowState.Normal;
        
        if (_previousSize != Size.Empty)
        {
            ClientSize = _previousSize;
        }
        
        if (_previousLocation != Point.Empty)
        {
            Location = _previousLocation;
        }
    }
    
    private void SetBorderlessMode()
    {
        _previousBorderStyle = FormBorderStyle;
        _previousWindowState = WindowState;
        _previousSize = ClientSize;
        _previousLocation = Location;
        
        FormBorderStyle = FormBorderStyle.None;
        WindowState = FormWindowState.Maximized;
    }
    
    private void SetFullscreenMode()
    {
        _previousBorderStyle = FormBorderStyle;
        _previousWindowState = WindowState;
        _previousSize = ClientSize;
        _previousLocation = Location;
        
        FormBorderStyle = FormBorderStyle.None;
        WindowState = FormWindowState.Normal;
        
        var screen = Screen.FromControl(this);
        Bounds = screen.Bounds;
    }
    
    #endregion
    
    private void OnMouseDown(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Right && _camera != null)
        {
            _camera.StartRotating(e.Location);
            Cursor.Hide();
        }
    }
    
    private void OnMouseMove(object? sender, MouseEventArgs e)
    {
        if (_camera?.IsRotating == true)
        {
            int deltaX = e.X - _camera.LastMousePos.X;
            int deltaY = e.Y - _camera.LastMousePos.Y;
            
            _camera.HandleMouseRotation(deltaX, deltaY);
            
            Point center = new Point(ClientSize.Width / 2, ClientSize.Height / 2);
            _camera.UpdateLastMousePos(center);
            Cursor.Position = PointToScreen(center);
        }
    }
    
    private void OnMouseUp(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Right && _camera != null)
        {
            _camera.StopRotating();
            Cursor.Show();
        }
    }
    
    private void OnMouseWheel(object? sender, MouseEventArgs e)
    {
        _camera?.HandleMouseWheel(e.Delta);
    }
    
    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        _pressedKeys.Add(e.KeyCode);
        e.Handled = true;
    }
    
    private void OnKeyUp(object? sender, KeyEventArgs e)
    {
        _pressedKeys.Remove(e.KeyCode);
        e.Handled = true;
    }

    protected override void OnPaint(PaintEventArgs e) { }

    protected override void OnPaintBackground(PaintEventArgs e) { }
}
