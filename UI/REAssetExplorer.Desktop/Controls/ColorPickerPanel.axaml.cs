using System.ComponentModel;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Controls;

public partial class ColorPickerPanel : UserControl
{
    private MaterialParameter? parameter;
    // Kept here rather than derived from RGB, so hue survives grays and black.
    private double hue;
    private double saturation;
    private double brightness;
    private double alpha = 1;
    private bool dragging;
    private bool pushing;

    public ColorPickerPanel()
    {
        InitializeComponent();
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (parameter != null) parameter.PropertyChanged -= OnParameterChanged;
        parameter = DataContext as MaterialParameter;
        if (parameter == null) return;
        parameter.PropertyChanged += OnParameterChanged;
        OriginalSwatch.Background = parameter.SwatchBrush;
        ReadParameter();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        if (parameter != null) parameter.PropertyChanged -= OnParameterChanged;
        parameter = null;
    }

    private void OnParameterChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MaterialParameter.SwatchColor) && !pushing) ReadParameter();
    }

    private void ReadParameter()
    {
        if (parameter == null) return;
        IReadOnlyList<ParameterComponent> c = parameter.Components;
        double r = Math.Clamp(c[0].Value, 0, 1);
        double g = Math.Clamp(c[1].Value, 0, 1);
        double b = Math.Clamp(c[2].Value, 0, 1);
        alpha = c.Count > 3 ? Math.Clamp(c[3].Value, 0, 1) : 1;
        double max = Math.Max(r, Math.Max(g, b));
        double min = Math.Min(r, Math.Min(g, b));
        brightness = max;
        saturation = max > 0 ? (max - min) / max : 0;
        if (max - min > 1e-6)
        {
            double h = max == r ? (g - b) / (max - min) : max == g ? 2 + (b - r) / (max - min) : 4 + (r - g) / (max - min);
            hue = (h * 60 + 360) % 360;
        }
        Refresh();
    }

    private void Push()
    {
        if (parameter == null) return;
        (double r, double g, double b) = HsvToRgb(hue, saturation, brightness);
        pushing = true;
        parameter.SetColor(r, g, b, alpha);
        pushing = false;
        Refresh();
    }

    private void Refresh()
    {
        (double r, double g, double b) = HsvToRgb(hue, 1, 1);
        HueLayer.Background = new ImmutableSolidColorBrush(ToColor(r, g, b, 1));
        (double cr, double cg, double cb) = HsvToRgb(hue, saturation, brightness);
        var alphaBrush = new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
            EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
            GradientStops = { new GradientStop(ToColor(cr, cg, cb, 1), 0), new GradientStop(ToColor(cr, cg, cb, 0), 1) }
        };
        AlphaLayer.Background = alphaBrush;
        Canvas.SetLeft(SvHandle, saturation * SvArea.Width - SvHandle.Width / 2);
        Canvas.SetTop(SvHandle, (1 - brightness) * SvArea.Height - SvHandle.Height / 2);
        Canvas.SetTop(HueHandle, hue / 360 * HueArea.Height - HueHandle.Height / 2);
        Canvas.SetTop(AlphaHandle, (1 - alpha) * AlphaArea.Height - AlphaHandle.Height / 2);
        if (!HexBox.IsFocused) HexBox.Text = ToColor(cr, cg, cb, alpha).ToString()[3..].ToUpperInvariant() + ToHexByte(alpha);
    }

    private void OnSvPressed(object? sender, PointerPressedEventArgs e) => Begin(e, SvArea, () => PickSv(e.GetPosition(SvArea)));

    private void OnSvMoved(object? sender, PointerEventArgs e)
    {
        if (dragging && ReferenceEquals(e.Pointer.Captured, SvArea)) PickSv(e.GetPosition(SvArea));
    }

    private void OnHuePressed(object? sender, PointerPressedEventArgs e) => Begin(e, HueArea, () => PickHue(e.GetPosition(HueArea)));

    private void OnHueMoved(object? sender, PointerEventArgs e)
    {
        if (dragging && ReferenceEquals(e.Pointer.Captured, HueArea)) PickHue(e.GetPosition(HueArea));
    }

    private void OnAlphaPressed(object? sender, PointerPressedEventArgs e) =>
        Begin(e, AlphaArea, () => PickAlpha(e.GetPosition(AlphaArea)));

    private void OnAlphaMoved(object? sender, PointerEventArgs e)
    {
        if (dragging && ReferenceEquals(e.Pointer.Captured, AlphaArea)) PickAlpha(e.GetPosition(AlphaArea));
    }

    private void OnAreaReleased(object? sender, PointerReleasedEventArgs e)
    {
        dragging = false;
        e.Pointer.Capture(null);
    }

    private void Begin(PointerPressedEventArgs e, Control area, Action pick)
    {
        dragging = true;
        e.Pointer.Capture(area);
        pick();
        e.Handled = true;
    }

    private void PickSv(Point p)
    {
        saturation = Math.Clamp(p.X / SvArea.Width, 0, 1);
        brightness = 1 - Math.Clamp(p.Y / SvArea.Height, 0, 1);
        Push();
    }

    private void PickHue(Point p)
    {
        hue = Math.Clamp(p.Y / HueArea.Height, 0, 1) * 360;
        if (hue >= 360) hue = 359.999;
        Push();
    }

    private void PickAlpha(Point p)
    {
        alpha = 1 - Math.Clamp(p.Y / AlphaArea.Height, 0, 1);
        Push();
    }

    private void OnHexKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        ApplyHex();
        e.Handled = true;
    }

    private void OnHexLostFocus(object? sender, Avalonia.Interactivity.RoutedEventArgs e) => ApplyHex();

    private void ApplyHex()
    {
        if (parameter == null) return;
        string text = (HexBox.Text ?? string.Empty).Trim().TrimStart('#');
        if ((text.Length == 6 || text.Length == 8) &&
            uint.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint packed))
        {
            if (text.Length == 6) packed = (packed << 8) | 0xFF;
            double Channel(int shift) => ((packed >> shift) & 0xFF) / 255.0;
            parameter.SetColor(Channel(24), Channel(16), Channel(8), Channel(0));
        }
        ReadParameter();
    }

    private static (double R, double G, double B) HsvToRgb(double h, double s, double v)
    {
        double c = v * s;
        double x = c * (1 - Math.Abs(h / 60 % 2 - 1));
        double m = v - c;
        (double r, double g, double b) = (h / 60) switch
        {
            < 1 => (c, x, 0.0),
            < 2 => (x, c, 0.0),
            < 3 => (0.0, c, x),
            < 4 => (0.0, x, c),
            < 5 => (x, 0.0, c),
            _ => (c, 0.0, x)
        };
        return (r + m, g + m, b + m);
    }

    private static Color ToColor(double r, double g, double b, double a) =>
        Color.FromArgb((byte)Math.Round(a * 255), (byte)Math.Round(r * 255), (byte)Math.Round(g * 255),
                       (byte)Math.Round(b * 255));

    private static string ToHexByte(double value) => ((byte)Math.Round(value * 255)).ToString("X2", CultureInfo.InvariantCulture);
}
