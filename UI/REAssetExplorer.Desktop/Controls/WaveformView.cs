using System.Globalization;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace REAssetExplorer.Desktop.Controls;

public sealed class WaveformView : Control
{
    // rae_audio_waveform layout.
    public static readonly StyledProperty<float[]?> PeaksProperty =
        AvaloniaProperty.Register<WaveformView, float[]?>(nameof(Peaks));

    public static readonly StyledProperty<int> ChannelsProperty =
        AvaloniaProperty.Register<WaveformView, int>(nameof(Channels));

    public static readonly StyledProperty<double> PositionProperty =
        AvaloniaProperty.Register<WaveformView, double>(nameof(Position));

    public static readonly StyledProperty<double> DurationProperty =
        AvaloniaProperty.Register<WaveformView, double>(nameof(Duration));

    public static readonly StyledProperty<ICommand?> SeekCommandProperty =
        AvaloniaProperty.Register<WaveformView, ICommand?>(nameof(SeekCommand));

    private const double RulerHeight = 16;
    private const double MinLabelSpacing = 80;

    private static readonly double[] TickSteps =
        [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600];

    private static readonly IBrush Background = new ImmutableSolidColorBrush(Color.Parse("#141414"));
    private static readonly IBrush RulerBrush = new ImmutableSolidColorBrush(Color.Parse("#232323"));
    private static readonly IBrush LabelBrush = new ImmutableSolidColorBrush(Color.Parse("#969696"));
    private static readonly IBrush WaveBrush = new ImmutableSolidColorBrush(Color.Parse("#4A7DB0"));
    private static readonly IBrush PlayedBrush = new ImmutableSolidColorBrush(Color.Parse("#8CB8E8"));
    private static readonly IBrush PlayheadBrush = new ImmutableSolidColorBrush(Color.Parse("#E8B04A"));
    private static readonly IBrush ProgressBrush = new ImmutableSolidColorBrush(Color.Parse("#2D4B6E"));
    private static readonly IPen BorderPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#3C3C3C")));
    private static readonly IPen CenterPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#2A2A2A")));
    private static readonly IPen TickPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#5E5E5E")));
    private static readonly IPen HoverPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#70FFFFFF")));
    private static readonly IPen PlayheadPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#E8B04A")));
    private static readonly Typeface LabelFace = new(new FontFamily("Segoe UI"));

    private Geometry? wave;
    private Size waveSize;
    private double? hoverX;
    private bool dragging;

    static WaveformView()
    {
        AffectsRender<WaveformView>(PeaksProperty, ChannelsProperty, PositionProperty, DurationProperty);
        ClipToBoundsProperty.OverrideDefaultValue<WaveformView>(true);
    }

    public float[]? Peaks
    {
        get => GetValue(PeaksProperty);
        set => SetValue(PeaksProperty, value);
    }

    public int Channels
    {
        get => GetValue(ChannelsProperty);
        set => SetValue(ChannelsProperty, value);
    }

    public double Position
    {
        get => GetValue(PositionProperty);
        set => SetValue(PositionProperty, value);
    }

    public double Duration
    {
        get => GetValue(DurationProperty);
        set => SetValue(DurationProperty, value);
    }

    public ICommand? SeekCommand
    {
        get => GetValue(SeekCommandProperty);
        set => SetValue(SeekCommandProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == PeaksProperty || change.Property == ChannelsProperty) wave = null;
    }

    public override void Render(DrawingContext context)
    {
        Size size = Bounds.Size;
        if (size.Width <= 0 || size.Height <= RulerHeight) return;
        context.FillRectangle(Background, new Rect(size));

        int channels = Math.Max(Channels, 1);
        double laneHeight = (size.Height - RulerHeight) / channels;
        for (int c = 0; c < channels; c++)
        {
            double top = RulerHeight + laneHeight * c;
            double center = Math.Floor(top + laneHeight / 2) + 0.5;
            context.DrawLine(CenterPen, new Point(0, center), new Point(size.Width, center));
            if (c > 0) context.DrawLine(BorderPen, new Point(0, Math.Floor(top) + 0.5), new Point(size.Width, Math.Floor(top) + 0.5));
        }

        if (wave == null || waveSize != size)
        {
            wave = BuildWave(size);
            waveSize = size;
        }
        double playX = X(Position, size.Width);
        if (wave == null && Duration > 0)
        {
            context.FillRectangle(ProgressBrush, new Rect(0, RulerHeight, playX, size.Height - RulerHeight));
        }
        if (wave != null)
        {
            using (context.PushClip(new Rect(0, RulerHeight, playX, size.Height - RulerHeight)))
            {
                context.DrawGeometry(PlayedBrush, null, wave);
            }
            using (context.PushClip(new Rect(playX, RulerHeight, size.Width - playX, size.Height - RulerHeight)))
            {
                context.DrawGeometry(WaveBrush, null, wave);
            }
        }
        if (Channels == 2)
        {
            DrawLabel(context, "L", new Point(4, RulerHeight + 1));
            DrawLabel(context, "R", new Point(4, RulerHeight + laneHeight + 1));
        }
        else if (Channels > 2)
        {
            for (int c = 0; c < Channels; c++) DrawLabel(context, (c + 1).ToString(CultureInfo.InvariantCulture), new Point(4, RulerHeight + laneHeight * c + 1));
        }

        DrawRuler(context, size);

        if (hoverX is { } hover && !dragging)
        {
            double x = Math.Floor(hover) + 0.5;
            context.DrawLine(HoverPen, new Point(x, RulerHeight), new Point(x, size.Height));
        }
        if (Duration > 0)
        {
            double x = Math.Floor(playX) + 0.5;
            context.DrawLine(PlayheadPen, new Point(x, 0), new Point(x, size.Height));
            var marker = new StreamGeometry();
            using (StreamGeometryContext g = marker.Open())
            {
                g.BeginFigure(new Point(x - 5, 0), true);
                g.LineTo(new Point(x + 5, 0));
                g.LineTo(new Point(x, 6));
                g.EndFigure(true);
            }
            context.DrawGeometry(PlayheadBrush, null, marker);
        }
    }

    private void DrawRuler(DrawingContext context, Size size)
    {
        context.FillRectangle(RulerBrush, new Rect(0, 0, size.Width, RulerHeight));
        context.DrawLine(BorderPen, new Point(0, RulerHeight - 0.5), new Point(size.Width, RulerHeight - 0.5));
        if (Duration <= 0) return;

        double pixelsPerSecond = size.Width / Duration;
        double step = TickSteps.FirstOrDefault(s => s * pixelsPerSecond >= MinLabelSpacing, TickSteps[^1]);
        int decimals = step >= 1 ? 0 : step >= 0.1 ? 1 : 2;
        double minor = step / 5;
        bool drawMinor = minor * pixelsPerSecond >= 6;
        for (int i = 0; ; i++)
        {
            double t = i * (drawMinor ? minor : step);
            if (t > Duration) break;
            double x = Math.Floor(X(t, size.Width)) + 0.5;
            bool major = !drawMinor || i % 5 == 0;
            context.DrawLine(TickPen, new Point(x, major ? 2 : RulerHeight - 4), new Point(x, RulerHeight - 1));
            if (major) DrawLabel(context, Label(t, decimals), new Point(x + 3, 1));
        }
    }

    private static void DrawLabel(DrawingContext context, string text, Point at)
    {
        var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, LabelFace, 10, LabelBrush);
        context.DrawText(formatted, at);
    }

    private string Label(double seconds, int decimals)
    {
        string format = decimals == 0 ? "0" : "0." + new string('0', decimals);
        if (Duration < 60) return seconds.ToString(format, CultureInfo.InvariantCulture);
        int minutes = (int)(seconds / 60);
        double rest = seconds - minutes * 60;
        return $"{minutes}:{rest.ToString("0" + format, CultureInfo.InvariantCulture)}";
    }

    private double X(double seconds, double width) => Duration > 0 ? Math.Clamp(seconds / Duration, 0, 1) * width : 0;

    private Geometry? BuildWave(Size size)
    {
        float[]? peaks = Peaks;
        int channels = Channels;
        int width = (int)Math.Ceiling(size.Width);
        if (peaks == null || channels <= 0 || width <= 0) return null;
        int columns = peaks.Length / (channels * 2);
        if (columns == 0) return null;

        double laneHeight = (size.Height - RulerHeight) / channels;
        double half = Math.Max(laneHeight / 2 - 2, 1);
        var top = new double[width];
        var bottom = new double[width];
        var geometry = new StreamGeometry();
        using StreamGeometryContext g = geometry.Open();
        for (int c = 0; c < channels; c++)
        {
            double center = Math.Floor(RulerHeight + laneHeight * c + laneHeight / 2) + 0.5;
            for (int x = 0; x < width; x++)
            {
                int begin = (int)((long)columns * x / width);
                int end = Math.Max((int)((long)columns * (x + 1) / width), begin + 1);
                float low = 0;
                float high = 0;
                for (int i = begin; i < end && i < columns; i++)
                {
                    int at = (c * columns + i) * 2;
                    low = Math.Min(low, peaks[at]);
                    high = Math.Max(high, peaks[at + 1]);
                }
                top[x] = Math.Min(center - high * half, center - 0.5);
                bottom[x] = Math.Max(center - low * half, center + 0.5);
            }
            g.BeginFigure(new Point(0, top[0]), true);
            for (int x = 0; x < width; x++)
            {
                g.LineTo(new Point(x, top[x]));
                g.LineTo(new Point(x + 1, top[x]));
            }
            for (int x = width - 1; x >= 0; x--)
            {
                g.LineTo(new Point(x + 1, bottom[x]));
                g.LineTo(new Point(x, bottom[x]));
            }
            g.EndFigure(true);
        }
        return geometry;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed || Duration <= 0) return;
        dragging = true;
        e.Pointer.Capture(this);
        SeekTo(e.GetPosition(this).X);
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        hoverX = e.GetPosition(this).X;
        if (dragging) SeekTo(hoverX.Value);
        InvalidateVisual();
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (!dragging) return;
        dragging = false;
        e.Pointer.Capture(null);
        e.Handled = true;
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        base.OnPointerCaptureLost(e);
        dragging = false;
    }

    protected override void OnPointerExited(PointerEventArgs e)
    {
        base.OnPointerExited(e);
        hoverX = null;
        InvalidateVisual();
    }

    private void SeekTo(double x)
    {
        if (Bounds.Width <= 0 || Duration <= 0) return;
        double seconds = Math.Clamp(x / Bounds.Width, 0, 1) * Duration;
        // Moves the playhead now instead of on the next status poll.
        SetCurrentValue(PositionProperty, seconds);
        if (SeekCommand?.CanExecute(seconds) == true) SeekCommand.Execute(seconds);
    }
}
