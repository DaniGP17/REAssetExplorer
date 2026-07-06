using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace REAssetExplorer.Desktop.Controls;

public sealed class NumberScrubBox : TextBox
{
    public static readonly StyledProperty<double> ValueProperty =
        AvaloniaProperty.Register<NumberScrubBox, double>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

    private const double DragThreshold = 3;

    private bool pressed;
    private bool scrubbing;
    private Point pressPoint;
    private double pressValue;

    public NumberScrubBox()
    {
        AddHandler(PointerPressedEvent, OnPointerPressedTunnel, RoutingStrategies.Tunnel);
        AddHandler(PointerMovedEvent, OnPointerMovedTunnel, RoutingStrategies.Tunnel);
        AddHandler(PointerReleasedEvent, OnPointerReleasedTunnel, RoutingStrategies.Tunnel);
        Cursor = new Cursor(StandardCursorType.SizeWestEast);
        ShowValue();
    }

    public double Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    protected override Type StyleKeyOverride => typeof(TextBox);

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == ValueProperty && !IsFocused) ShowValue();
    }

    protected override void OnGotFocus(FocusChangedEventArgs e)
    {
        base.OnGotFocus(e);
        Cursor = new Cursor(StandardCursorType.Ibeam);
    }

    protected override void OnLostFocus(FocusChangedEventArgs e)
    {
        base.OnLostFocus(e);
        Commit();
        Cursor = new Cursor(StandardCursorType.SizeWestEast);
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            Commit();
            SelectAll();
            e.Handled = true;
            return;
        }
        if (e.Key == Key.Escape)
        {
            ShowValue();
            TopLevel.GetTopLevel(this)?.Focus();
            e.Handled = true;
            return;
        }
        base.OnKeyDown(e);
    }

    private void OnPointerPressedTunnel(object? sender, PointerPressedEventArgs e)
    {
        if (IsFocused || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        pressed = true;
        scrubbing = false;
        pressPoint = e.GetPosition(this);
        pressValue = Value;
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    private void OnPointerMovedTunnel(object? sender, PointerEventArgs e)
    {
        if (!pressed) return;
        double dx = e.GetPosition(this).X - pressPoint.X;
        if (!scrubbing && Math.Abs(dx) < DragThreshold) return;
        scrubbing = true;
        double step = StepFor(pressValue);
        if (e.KeyModifiers.HasFlag(KeyModifiers.Shift)) step *= 0.1;
        if (e.KeyModifiers.HasFlag(KeyModifiers.Control)) step *= 10;
        Value = Math.Round((pressValue + dx * step) / step) * step;
        e.Handled = true;
    }

    private void OnPointerReleasedTunnel(object? sender, PointerReleasedEventArgs e)
    {
        if (!pressed) return;
        pressed = false;
        e.Pointer.Capture(null);
        if (!scrubbing)
        {
            Focus();
            SelectAll();
        }
        scrubbing = false;
        e.Handled = true;
    }

    private static double StepFor(double value) => Math.Abs(value) switch
    {
        < 2 => 0.005,
        < 20 => 0.05,
        < 200 => 0.5,
        _ => 5
    };

    private void Commit()
    {
        if (double.TryParse(Text, NumberStyles.Float, CultureInfo.InvariantCulture, out double parsed)) Value = parsed;
        ShowValue();
    }

    private void ShowValue() => Text = Value.ToString("0.####", CultureInfo.InvariantCulture);
}
