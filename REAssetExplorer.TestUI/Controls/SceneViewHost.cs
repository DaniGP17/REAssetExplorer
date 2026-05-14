using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.RenderTest2;
using REAssetExplorer.RenderTest2.Scene;
using WinFormsKeys  = System.Windows.Forms.Keys;
using WinFormsPoint = System.Drawing.Point;

namespace REAssetExplorer.TestUI.Controls;

/// <summary>
/// Scene viewport: DX12 rendering + camera controls.
///
/// Architecture:
///   UserControl
///   └── Grid
///       ├── SceneRenderHost  (HwndHost — DX12, returns HTTRANSPARENT)
///       └── Border (Background=Transparent) ← WPF hit-test finds this;
///                                             all input goes here.
///
/// Why the overlay: HTTRANSPARENT tells Win32 to route mouse events to the
/// parent WPF HWND. WPF then hit-tests its own visual tree. The transparent
/// Border is the LAST child of the Grid (highest WPF Z-order) and has a
/// non-null Background, so WPF reliably routes mouse events to it.
/// </summary>
public class SceneViewHost : UserControl
{
    // ── Inner HwndHost ───────────────────────────────────────────────────────

    private sealed class SceneRenderHost : HwndHost
    {
        private const int WS_CHILD        = 0x40000000;
        private const int WS_VISIBLE      = 0x10000000;
        private const int WS_CLIPSIBLINGS = 0x04000000;
        private const int WS_CLIPCHILDREN = 0x02000000;
        private const int GWLP_WNDPROC    = -4;
        private const int WM_NCHITTEST    = 0x0084;
        private const int HTTRANSPARENT   = -1;

        private delegate IntPtr WndProcDelegate(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern IntPtr CreateWindowEx(
            int dwExStyle, string lpClassName, string lpWindowName, int dwStyle,
            int x, int y, int nWidth, int nHeight,
            IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

        [DllImport("user32.dll")]
        private static extern bool DestroyWindow(IntPtr hwnd);

        [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
        private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);

        [DllImport("user32.dll")]
        private static extern IntPtr CallWindowProc(IntPtr lpPrevWndFunc, IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        private IntPtr           _childHwnd;
        private IntPtr           _originalWndProc;
        private WndProcDelegate? _wndProcDelegate;

        public IntPtr ChildHwnd => _childHwnd;

        protected override HandleRef BuildWindowCore(HandleRef hwndParent)
        {
            _childHwnd = CreateWindowEx(
                0, "STATIC", string.Empty,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                0, 0, 800, 600,
                hwndParent.Handle, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);

            _wndProcDelegate = WndProc;
            _originalWndProc = SetWindowLongPtr(
                _childHwnd, GWLP_WNDPROC,
                Marshal.GetFunctionPointerForDelegate(_wndProcDelegate));

            return new HandleRef(this, _childHwnd);
        }

        protected override void DestroyWindowCore(HandleRef hwnd)
        {
            DestroyWindow(hwnd.Handle);
        }

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam)
        {
            // Mouse-transparent: let WPF root HWND receive all mouse messages.
            if (msg == WM_NCHITTEST)
                return new IntPtr(HTTRANSPARENT);

            return CallWindowProc(_originalWndProc, hwnd, msg, wParam, lParam);
        }
    }

    // ── Render state ─────────────────────────────────────────────────────────

    private Renderer? _renderer;
    private Camera?   _camera;
    private Thread?   _renderThread;
    private volatile bool _running;
    private volatile int  _renderWidth  = 800;
    private volatile int  _renderHeight = 600;
    private volatile bool _pendingResize;

    // ── Thread-safe key state ────────────────────────────────────────────────

    private readonly object               _keysLock    = new();
    private readonly HashSet<WinFormsKeys> _pendingKeys = new();
    private          HashSet<WinFormsKeys> _activeKeys  = new();

    // ── Mouse tracking (UI thread) ───────────────────────────────────────────

    private System.Windows.Point _lastMousePos;
    private bool _keyboardSubscribed;

    // ── Inner controls ───────────────────────────────────────────────────────

    private readonly SceneRenderHost _renderHost;
    private readonly Border          _inputOverlay;

    public SceneViewHost()
    {
        _renderHost = new SceneRenderHost();

        // Transparent but non-null Background makes it hit-testable by WPF.
        _inputOverlay = new Border { Background = Brushes.Transparent };

        var grid = new Grid();
        grid.Children.Add(_renderHost);
        grid.Children.Add(_inputOverlay); // last child = highest WPF Z-order

        Content = grid;

        _inputOverlay.MouseLeftButtonDown  += OnOverlayLeftClick;
        _inputOverlay.MouseRightButtonDown += OnOverlayRightDown;
        _inputOverlay.MouseRightButtonUp   += OnOverlayRightUp;
        _inputOverlay.MouseMove            += OnOverlayMouseMove;
        _inputOverlay.MouseWheel           += OnOverlayMouseWheel;

        _renderHost.SizeChanged += (_, _) => OnRenderHostSizeChanged();
    }

    // ── Public API ───────────────────────────────────────────────────────────

    /// <summary>Raised on the UI thread when the user clicks a scene object. Arg is the scene node Id.</summary>
    public event Action<int>? SceneObjectPicked;

    public void LoadScene(SceneData scene)
    {
        EnsureRendererInitialized();
        _renderer?.LoadScene(scene);
    }

    /// <summary>
    /// Loads a single .mesh file, replacing the current scene geometry.
    /// Paths are relative to the game's FilesPath. <paramref name="mdfPath"/> may be empty.
    /// </summary>
    public void LoadMesh(string meshPath, string mdfPath)
    {
        EnsureRendererInitialized();
        _renderer?.LoadMesh(meshPath, mdfPath);
    }

    // ── Renderer initialization ──────────────────────────────────────────────

    private void EnsureRendererInitialized()
    {
        var hwnd = _renderHost.ChildHwnd;
        if (_renderer != null || hwnd == IntPtr.Zero) return;

        var session = AssetSession.Current;
        if (session == null) return;

        var pakFiles = GameLoader.LoadedPakFiles.ToDictionary(k => k.Key, v => v.Value);
        var provider = session.Loader.GameProvider;

        _camera   = new Camera(ref _activeKeys);
        _renderer = new Renderer(pakFiles, provider);

        var w = Math.Max(1, (int)_renderHost.ActualWidth);
        var h = Math.Max(1, (int)_renderHost.ActualHeight);
        _renderWidth  = w;
        _renderHeight = h;

        if (!_renderer.Initialize(hwnd, _renderWidth, _renderHeight))
        {
            Console.WriteLine("[SceneViewHost] Renderer.Initialize failed");
            _renderer.Dispose();
            _renderer = null;
            return;
        }

        _renderer.SetCamera(_camera);

        _running = true;
        _renderThread = new Thread(RenderLoop) { IsBackground = true, Name = "DX12RenderThread" };
        _renderThread.Start();
    }

    protected override void OnVisualParentChanged(DependencyObject oldParent)
    {
        base.OnVisualParentChanged(oldParent);
        if (oldParent != null && Parent == null)
        {
            // Control is being removed from the tree — shut down render thread.
            _running = false;
            _renderThread?.Join(3000);
            _renderer?.Dispose();
            _renderer = null;
        }
    }

    private void OnRenderHostSizeChanged()
    {
        var w = Math.Max(1, (int)_renderHost.ActualWidth);
        var h = Math.Max(1, (int)_renderHost.ActualHeight);
        if (w != _renderWidth || h != _renderHeight)
        {
            _renderWidth   = w;
            _renderHeight  = h;
            _pendingResize = true;
        }
    }

    // ── Render thread ────────────────────────────────────────────────────────

    private void RenderLoop()
    {
        while (_running)
        {
            try
            {
                lock (_keysLock)
                {
                    _activeKeys.Clear();
                    foreach (var k in _pendingKeys)
                        _activeKeys.Add(k);
                }

                if (_pendingResize && _renderer?.IsInitialized == true)
                {
                    _pendingResize = false;
                    _renderer.Resize(_renderWidth, _renderHeight);
                }

                if (_renderer?.IsInitialized == true)
                    _renderer.RenderFrame(new System.Drawing.Size(_renderWidth, _renderHeight));
                else
                    Thread.Sleep(16);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[RenderThread] {ex.Message}");
                Thread.Sleep(16);
            }
        }
    }

    // ── Overlay mouse handlers ────────────────────────────────────────────────

    private void OnOverlayLeftClick(object sender, MouseButtonEventArgs e)
    {
        if (_renderer == null) return;
        var pos    = e.GetPosition(_inputOverlay);
        int nodeId = _renderer.PickObject((float)pos.X, (float)pos.Y, _renderWidth, _renderHeight);
        _renderer.SetSelectedNode(nodeId); // -1 clears the outline
        if (nodeId >= 0)
            SceneObjectPicked?.Invoke(nodeId);
        e.Handled = true;
    }

    private void OnOverlayRightDown(object sender, MouseButtonEventArgs e)
    {
        EnsureRendererInitialized();
        _lastMousePos = e.GetPosition(_inputOverlay);
        _camera?.StartRotating(new WinFormsPoint((int)_lastMousePos.X, (int)_lastMousePos.Y));
        _inputOverlay.CaptureMouse();
        SubscribeKeyboard();
        e.Handled = true;
    }

    private void OnOverlayRightUp(object sender, MouseButtonEventArgs e)
    {
        _camera?.StopRotating();
        _inputOverlay.ReleaseMouseCapture();
        UnsubscribeKeyboard();
        e.Handled = true;
    }

    private void OnOverlayMouseMove(object sender, MouseEventArgs e)
    {
        if (_camera?.IsRotating != true) return;

        var pos = e.GetPosition(_inputOverlay);
        int dx = (int)(pos.X - _lastMousePos.X);
        int dy = (int)(pos.Y - _lastMousePos.Y);

        if (dx != 0 || dy != 0)
        {
            _camera.HandleMouseRotation(dx, dy);
            _camera.UpdateLastMousePos(new WinFormsPoint((int)pos.X, (int)pos.Y));
            _lastMousePos = pos;
        }
    }

    private void OnOverlayMouseWheel(object sender, MouseWheelEventArgs e)
    {
        EnsureRendererInitialized();
        _camera?.HandleMouseWheel(e.Delta);
        e.Handled = true;
    }

    // ── Keyboard (subscribed to parent Window while right-mouse held) ─────────

    private void SubscribeKeyboard()
    {
        if (_keyboardSubscribed) return;
        var win = Window.GetWindow(this);
        if (win == null) return;
        win.KeyDown += OnWindowKeyDown;
        win.KeyUp   += OnWindowKeyUp;
        _keyboardSubscribed = true;
    }

    private void UnsubscribeKeyboard()
    {
        if (!_keyboardSubscribed) return;
        var win = Window.GetWindow(this);
        if (win != null)
        {
            win.KeyDown -= OnWindowKeyDown;
            win.KeyUp   -= OnWindowKeyUp;
        }
        lock (_keysLock) _pendingKeys.Clear();
        _keyboardSubscribed = false;
    }

    private void OnWindowKeyDown(object sender, KeyEventArgs e)
    {
        var key = MapWpfKey(e.Key);
        if (key == WinFormsKeys.None) return;
        lock (_keysLock) _pendingKeys.Add(key);
        e.Handled = true;
    }

    private void OnWindowKeyUp(object sender, KeyEventArgs e)
    {
        var key = MapWpfKey(e.Key);
        if (key == WinFormsKeys.None) return;
        lock (_keysLock) _pendingKeys.Remove(key);
        e.Handled = true;
    }

    private static WinFormsKeys MapWpfKey(Key key) => key switch
    {
        Key.W          => WinFormsKeys.W,
        Key.A          => WinFormsKeys.A,
        Key.S          => WinFormsKeys.S,
        Key.D          => WinFormsKeys.D,
        Key.Q          => WinFormsKeys.Q,
        Key.E          => WinFormsKeys.E,
        Key.LeftShift  => WinFormsKeys.ShiftKey,
        Key.RightShift => WinFormsKeys.ShiftKey,
        _              => WinFormsKeys.None
    };
}
