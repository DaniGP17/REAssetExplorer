using System;
using System.Collections.Generic;
using System.Windows.Forms;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Ventana de renderizado usando Windows Forms
/// </summary>
public class RenderWindow : Form
{
    public new event Action<float>? Update;
    public event Action? Render;
    public new event Action? Load;
    
    // Camera control events
    public event Action<float, float, bool>? OnCameraRotate;  // deltaX, deltaY, isOrbital (ALT pressed)
    public event Action<float, float>? OnCameraPan;
    public event Action<float>? OnCameraMoveSpeedAdjust;  // Adjust camera move speed (wheel + right mouse)
    public event Action? OnOrbitModeStart;  // Called when entering orbital mode
    public event Action? OnOrbitModeEnd;  // Called when exiting orbital mode

    private DateTime _lastFrameTime;
    private bool _isRunning;
    private bool _isRightMouseDown;
    private bool _isMiddleMouseDown;
    private bool _isLeftMouseDown;
    private bool _wasInOrbitMode = false;  // Track if we were in orbital mode last frame
    private System.Drawing.Point _lastMousePosition;
    
    // Keyboard input tracking
    private HashSet<Keys> _pressedKeys = new HashSet<Keys>();

    public RenderWindow(string title, int width, int height)
    {
        Text = title;
        ClientSize = new System.Drawing.Size(width, height);
        FormBorderStyle = FormBorderStyle.Sizable;
        StartPosition = FormStartPosition.CenterScreen;

        _lastFrameTime = DateTime.UtcNow;
    }

    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        Load?.Invoke();
    }

    public void Run()
    {
        _isRunning = true;
        Show();

        // Game loop
        Application.Idle += OnApplicationIdle;
        Application.Run(this);
    }

    private void OnApplicationIdle(object? sender, EventArgs e)
    {
        while (_isRunning && IsApplicationIdle())
        {
            var currentTime = DateTime.UtcNow;
            var deltaTime = (float)(currentTime - _lastFrameTime).TotalSeconds;
            _lastFrameTime = currentTime;

            Update?.Invoke(deltaTime);
            Render?.Invoke();
        }
    }

    private bool IsApplicationIdle()
    {
        NativeMessage result;
        return PeekMessage(out result, IntPtr.Zero, 0, 0, 0) == 0;
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _isRunning = false;
        base.OnFormClosing(e);
    }
    
    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        
        if (e.Button == MouseButtons.Left)
        {
            _isLeftMouseDown = true;
            _lastMousePosition = e.Location;
            if ((ModifierKeys & Keys.Alt) == Keys.Alt)
            {
                Cursor = Cursors.SizeAll;
            }
        }
        else if (e.Button == MouseButtons.Right)
        {
            _isRightMouseDown = true;
            _lastMousePosition = e.Location;
            Cursor = Cursors.SizeAll;
        }
        else if (e.Button == MouseButtons.Middle)
        {
            _isMiddleMouseDown = true;
            _lastMousePosition = e.Location;
            Cursor = Cursors.SizeAll;
        }
    }
    
    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        
        if (e.Button == MouseButtons.Left)
        {
            // Si estábamos en modo orbital, disparar evento de salida
            if (_wasInOrbitMode)
            {
                OnOrbitModeEnd?.Invoke();
                _wasInOrbitMode = false;
            }
            
            _isLeftMouseDown = false;
            Cursor = Cursors.Default;
        }
        else if (e.Button == MouseButtons.Right)
        {
            _isRightMouseDown = false;
            Cursor = Cursors.Default;
        }
        else if (e.Button == MouseButtons.Middle)
        {
            _isMiddleMouseDown = false;
            Cursor = Cursors.Default;
        }
    }
    
    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        
        bool isInOrbitMode = _isLeftMouseDown && (ModifierKeys & Keys.Alt) == Keys.Alt;
        
        // Detectar cuando entramos en modo orbital
        if (isInOrbitMode && !_wasInOrbitMode)
        {
            OnOrbitModeStart?.Invoke();
        }
        // Detectar cuando salimos del modo orbital
        else if (!isInOrbitMode && _wasInOrbitMode)
        {
            OnOrbitModeEnd?.Invoke();
        }
        
        _wasInOrbitMode = isInOrbitMode;
        
        if (isInOrbitMode)
        {
            // ALT + Left Mouse = Orbital rotation
            float deltaX = e.X - _lastMousePosition.X;
            float deltaY = e.Y - _lastMousePosition.Y;
            
            OnCameraRotate?.Invoke(deltaX, deltaY, true);
            
            _lastMousePosition = e.Location;
        }
        else if (_isRightMouseDown)
        {
            // Right Mouse = Free look rotation
            float deltaX = e.X - _lastMousePosition.X;
            float deltaY = e.Y - _lastMousePosition.Y;
            
            OnCameraRotate?.Invoke(deltaX, deltaY, false);
            
            _lastMousePosition = e.Location;
        }
        else if (_isMiddleMouseDown)
        {
            float deltaX = e.X - _lastMousePosition.X;
            float deltaY = e.Y - _lastMousePosition.Y;
            
            OnCameraPan?.Invoke(deltaX, deltaY);
            
            _lastMousePosition = e.Location;
        }
    }
    
    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        
        float delta = e.Delta / 120.0f; // 120 es el valor estándar de un "click" de rueda
        
        // Si se mantiene el botón derecho, ajustar velocidad de movimiento
        if (_isRightMouseDown)
        {
            OnCameraMoveSpeedAdjust?.Invoke(delta);
        }
        // Si no, el wheel no hace nada (antes hacía zoom)
    }
    
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        _pressedKeys.Add(e.KeyCode);
    }
    
    protected override void OnKeyUp(KeyEventArgs e)
    {
        base.OnKeyUp(e);
        _pressedKeys.Remove(e.KeyCode);
    }
    
    public bool IsKeyPressed(Keys key)
    {
        return _pressedKeys.Contains(key);
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct NativeMessage
    {
        public IntPtr Handle;
        public uint Message;
        public IntPtr WParameter;
        public IntPtr LParameter;
        public uint Time;
        public System.Drawing.Point Location;
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern int PeekMessage(out NativeMessage message, IntPtr window, uint filterMin, uint filterMax, uint remove);
}
