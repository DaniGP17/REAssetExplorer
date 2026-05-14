using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Markup;
using System.Windows.Media;

namespace REAssetExplorer.TestUI.Controls;

// PanelContent redirige el hijo XAML (short-form) a la DP propia,
// dejando UserControl.Content libre para la estructura visual interna.
[ContentProperty(nameof(PanelContent))]
public partial class FloatingPanel : UserControl
{
    private double _savedHeight    = double.NaN;
    private double _savedMinHeight = double.NaN;
    private double _savedWidth     = double.NaN;
    private double _savedMinWidth  = double.NaN;

    // ── Dependency Properties ────────────────────────────────────────────

    public static readonly DependencyProperty TitleProperty =
        DependencyProperty.Register(nameof(Title), typeof(string), typeof(FloatingPanel),
            new PropertyMetadata("Panel"));

    public static readonly DependencyProperty SubtitleProperty =
        DependencyProperty.Register(nameof(Subtitle), typeof(string), typeof(FloatingPanel),
            new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty PanelContentProperty =
        DependencyProperty.Register(nameof(PanelContent), typeof(object), typeof(FloatingPanel));

    public object? PanelContent
    {
        get => GetValue(PanelContentProperty);
        set => SetValue(PanelContentProperty, value);
    }

    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(nameof(IsCompact), typeof(bool), typeof(FloatingPanel),
            new PropertyMetadata(false, OnIsCompactChanged));

    public static readonly DependencyProperty IsDockedProperty =
        DependencyProperty.Register(nameof(IsDocked), typeof(bool), typeof(FloatingPanel),
            new PropertyMetadata(false, OnIsDockedChanged));

    public static readonly DependencyProperty IsCompactSideProperty =
        DependencyProperty.Register(nameof(IsCompactSide), typeof(bool), typeof(FloatingPanel),
            new PropertyMetadata(false, OnIsCompactSideChanged));

    public static readonly DependencyProperty IsCompactSecondChildProperty =
        DependencyProperty.Register(nameof(IsCompactSecondChild), typeof(bool), typeof(FloatingPanel),
            new PropertyMetadata(false, OnIsCompactSecondChildChanged));

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public string Subtitle
    {
        get => (string)GetValue(SubtitleProperty);
        set => SetValue(SubtitleProperty, value);
    }

    public bool IsCompact
    {
        get => (bool)GetValue(IsCompactProperty);
        set => SetValue(IsCompactProperty, value);
    }

    public bool IsDocked
    {
        get => (bool)GetValue(IsDockedProperty);
        set => SetValue(IsDockedProperty, value);
    }

    public bool IsCompactSide
    {
        get => (bool)GetValue(IsCompactSideProperty);
        set => SetValue(IsCompactSideProperty, value);
    }

    public bool IsCompactSecondChild
    {
        get => (bool)GetValue(IsCompactSecondChildProperty);
        set => SetValue(IsCompactSecondChildProperty, value);
    }

    // ── Routed Event: PanelClosed ────────────────────────────────────────

    public static readonly RoutedEvent PanelClosedEvent =
        EventManager.RegisterRoutedEvent(nameof(PanelClosed), RoutingStrategy.Bubble,
            typeof(RoutedEventHandler), typeof(FloatingPanel));

    public event RoutedEventHandler PanelClosed
    {
        add    => AddHandler(PanelClosedEvent, value);
        remove => RemoveHandler(PanelClosedEvent, value);
    }

    // ── Relay events para drag (gestionados por MainWindow) ─────────────

    public event MouseButtonEventHandler? HeaderMouseLeftButtonDown;
    public event MouseEventHandler?       HeaderMouseMove;
    public event MouseButtonEventHandler? HeaderMouseLeftButtonUp;

    public FloatingPanel()
    {
        InitializeComponent();
        MinWidth  = 260;
        MinHeight = 60;

        // Mouse capturado por el panel: los eventos de Move/Up llegan aquí
        MouseMove         += Panel_MouseMove;
        MouseLeftButtonUp += Panel_MouseLeftButtonUp;
    }

    // ── Drag relay ───────────────────────────────────────────────────────

    private void Header_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        CaptureMouse();
        Mouse.OverrideCursor = Cursors.SizeAll;
        HeaderMouseLeftButtonDown?.Invoke(this, e);
        e.Handled = true;
    }

    private void Panel_MouseMove(object sender, MouseEventArgs e)
    {
        if (!IsMouseCaptured) return;
        HeaderMouseMove?.Invoke(this, e);
    }

    private void Panel_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (!IsMouseCaptured) return;
        ReleaseMouseCapture();
        Mouse.OverrideCursor = null;
        HeaderMouseLeftButtonUp?.Invoke(this, e);
        e.Handled = true;
    }

    // ── Botones de cabecera ──────────────────────────────────────────────

    private void Compact_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        IsCompact = !IsCompact;
        e.Handled = true;
    }

    private void Close_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        RaiseEvent(new RoutedEventArgs(PanelClosedEvent));
        e.Handled = true;
    }

    // ── Resize ───────────────────────────────────────────────────────────

    private void Thumb_DragDelta(object sender, DragDeltaEventArgs e)
    {
        if (IsCompact || IsDocked) return;
        Width  = Math.Max(MinWidth,  ActualWidth  + e.HorizontalChange);
        Height = Math.Max(MinHeight, ActualHeight + e.VerticalChange);
    }

    // ── Compact icon rotation ────────────────────────────────────────────

    private void UpdateCompactIconAngle()
    {
        // Base angle points toward the collapse direction (where the panel will go).
        // IsCompactSide=true  → horizontal split: left(0°) or right(180°)
        // IsCompactSide=false → vertical split:   top(-90°) or bottom(90°)
        double base_ = IsCompactSide
            ? (IsCompactSecondChild ? 180.0 : 0.0)
            : (IsCompactSecondChild ? 90.0  : -90.0);

        // When compact, flip 180° to point back toward expand direction.
        double angle = base_ + (IsCompact ? 180.0 : 0.0);

        CompactIconRotation.Angle     = angle;
        SideCompactIconRotation.Angle = angle;
    }

    // ── IsDockedChanged ──────────────────────────────────────────────────

    private static void OnIsDockedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var panel = (FloatingPanel)d;
        panel.ResizeThumb.Visibility = (bool)e.NewValue ? Visibility.Collapsed : Visibility.Visible;
    }

    // ── IsCompactSideChanged / IsCompactSecondChildChanged — set by DockingArea ──

    private static void OnIsCompactSideChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var panel = (FloatingPanel)d;
        panel.UpdateCompactIconAngle();
        if (!panel.IsCompact) return;

        // Panel is already compact: switch the visual to the new orientation
        // without touching the saved dimensions (RebuildLayout handles sizing).
        bool sideMode = (bool)e.NewValue;
        panel.MainLayout.Visibility = sideMode ? Visibility.Collapsed : Visibility.Visible;
        panel.SideStrip.Visibility  = sideMode ? Visibility.Visible   : Visibility.Collapsed;

        if (sideMode)
        {
            panel.Width               = 31;
            panel.Height              = double.NaN;
            panel.HorizontalAlignment = HorizontalAlignment.Left;
            panel.VerticalAlignment   = VerticalAlignment.Stretch;
        }
        else
        {
            panel.Width               = double.NaN;
            panel.Height              = 31;
            panel.HorizontalAlignment = HorizontalAlignment.Stretch;
            panel.VerticalAlignment   = VerticalAlignment.Top;
        }
    }

    private static void OnIsCompactSecondChildChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((FloatingPanel)d).UpdateCompactIconAngle();

    // ── IsCompactChanged ─────────────────────────────────────────────────

    private static void OnIsCompactChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var panel    = (FloatingPanel)d;
        var compact  = (bool)e.NewValue;
        bool sideMode = compact && panel.IsCompactSide;

        panel.MainLayout.Visibility  = sideMode ? Visibility.Collapsed : Visibility.Visible;
        panel.SideStrip.Visibility   = sideMode ? Visibility.Visible   : Visibility.Collapsed;

        var vis = compact ? Visibility.Collapsed : Visibility.Visible;
        panel.ContentArea.Visibility     = vis;
        panel.ResizeThumb.Visibility     = vis;
        panel.HeaderSeparator.Visibility = vis;

        if (compact)
        {
            panel._savedHeight    = panel.Height;
            panel._savedMinHeight = panel.MinHeight;
            panel.MinHeight       = 0;

            if (sideMode)
            {
                panel._savedWidth    = panel.Width;
                panel._savedMinWidth = panel.MinWidth;
                panel.MinWidth                = 0;
                panel.Width                   = 31;
                panel.Height                  = double.NaN;
                panel.HorizontalAlignment     = HorizontalAlignment.Left;
                panel.VerticalAlignment       = VerticalAlignment.Stretch;
            }
            else
            {
                panel.Height            = 31;
                panel.VerticalAlignment = VerticalAlignment.Top;
            }
        }
        else
        {
            panel.MinHeight = panel._savedMinHeight;
            if (!double.IsNaN(panel._savedMinWidth))
                panel.MinWidth = panel._savedMinWidth;

            panel.Height = panel.IsDocked ? double.NaN : panel._savedHeight;
            panel.Width  = panel.IsDocked ? double.NaN
                         : (!double.IsNaN(panel._savedWidth) ? panel._savedWidth : panel.Width);

            panel.VerticalAlignment   = panel.IsDocked ? VerticalAlignment.Stretch   : VerticalAlignment.Top;
            panel.HorizontalAlignment = panel.IsDocked ? HorizontalAlignment.Stretch  : HorizontalAlignment.Left;

            panel._savedHeight    = double.NaN;
            panel._savedMinHeight = double.NaN;
            panel._savedWidth     = double.NaN;
            panel._savedMinWidth  = double.NaN;
        }

        panel.UpdateCompactIconAngle();

        if (panel.IsDocked)
            panel.Dispatcher.BeginInvoke(() => FindAncestor<DockingArea>(panel)?.RebuildLayout());
    }

    private static T? FindAncestor<T>(DependencyObject child) where T : DependencyObject
    {
        var current = VisualTreeHelper.GetParent(child);
        while (current != null)
        {
            if (current is T found) return found;
            current = VisualTreeHelper.GetParent(current);
        }
        return null;
    }
}
