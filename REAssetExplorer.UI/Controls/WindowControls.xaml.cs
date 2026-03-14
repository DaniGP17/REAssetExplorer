using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace REAssetExplorer.App.Controls;

/// <summary>
/// User control for custom window title bar controls.
/// </summary>
public partial class WindowControls : WpfUserControl
{
    public static readonly DependencyProperty CanMaximizeProperty =
        DependencyProperty.Register(
            nameof(CanMaximize),
            typeof(bool),
            typeof(WindowControls),
            new PropertyMetadata(true, OnCanMaximizeChanged));

    public static readonly DependencyProperty CanCloseProperty =
        DependencyProperty.Register(
            nameof(CanClose),
            typeof(bool),
            typeof(WindowControls),
            new PropertyMetadata(true, OnCanCloseChanged));

    public WindowControls()
    {
        InitializeComponent();
    }

    public bool CanMaximize
    {
        get => (bool)GetValue(CanMaximizeProperty);
        set => SetValue(CanMaximizeProperty, value);
    }

    public bool CanClose
    {
        get => (bool)GetValue(CanCloseProperty);
        set => SetValue(CanCloseProperty, value);
    }

    private void Minimize_Click(object sender, RoutedEventArgs e)
    {
        GetParentWindow()?.SetWindowState(WindowState.Minimized);
    }

    private void Maximize_Click(object sender, RoutedEventArgs e)
    {
        var window = GetParentWindow();
        if (window != null)
        {
            window.WindowState = window.WindowState == WindowState.Maximized 
                ? WindowState.Normal 
                : WindowState.Maximized;
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e)
    {
        GetParentWindow()?.Close();
    }

    private void DragWindow(object sender, MouseButtonEventArgs e)
    {
        if (e.LeftButton == MouseButtonState.Pressed)
        {
            GetParentWindow()?.DragMove();
        }
    }

    private Window? GetParentWindow() => Window.GetWindow(this);

    private static void OnCanMaximizeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is WindowControls control)
        {
            control.UpdateMaximizeButtonVisibility();
        }
    }

    private static void OnCanCloseChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is WindowControls control)
        {
            control.UpdateCloseButtonVisibility();
        }
    }

    private void UpdateMaximizeButtonVisibility()
    {
        if (MaximizeButton != null)
        {
            MaximizeButton.Visibility = CanMaximize ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    private void UpdateCloseButtonVisibility()
    {
        if (CloseButton != null)
        {
            CloseButton.Visibility = CanClose ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}

/// <summary>
/// Extension methods for Window class.
/// </summary>
internal static class WindowExtensions
{
    public static void SetWindowState(this Window window, WindowState state)
    {
        window.WindowState = state;
    }
}