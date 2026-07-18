using System.ComponentModel;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Controls;

public sealed class FsmGraphCanvas : Control
{
    public static readonly StyledProperty<FsmGraphViewModel?> GraphProperty =
        AvaloniaProperty.Register<FsmGraphCanvas, FsmGraphViewModel?>(nameof(Graph));

    private const double FitMargin = 0.9;
    private const double WheelStep = 1.15;
    private const double MinZoom = 0.05;
    private const double MaxZoom = 4;
    private const double GridStep = 16;
    private const double ArrowSize = 7;
    private const double StripWidth = 4;

    private static readonly IBrush Background = new ImmutableSolidColorBrush(Color.Parse("#1D1D1D"));
    private static readonly IPen GridPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#242424")));
    private static readonly IPen GridMajorPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#2C2C2C")));
    private static readonly IBrush BoxBrush = new ImmutableSolidColorBrush(Color.Parse("#2F2F2F"));
    private static readonly IBrush PseudoBrush = new ImmutableSolidColorBrush(Color.Parse("#262626"));
    private static readonly IPen BoxPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#4A4A4A")));
    private static readonly IPen HoverPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#8A8A8A")));
    private static readonly IPen SelectedPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#FF9A28")), 2);
    private static readonly IBrush TitleBrush = new ImmutableSolidColorBrush(Color.Parse("#E4E4E4"));
    private static readonly IBrush SubtitleBrush = new ImmutableSolidColorBrush(Color.Parse("#9A9A9A"));
    private static readonly IBrush LabelBackground = new ImmutableSolidColorBrush(Color.Parse("#E0181818"));
    private static readonly Color EdgeColor = Color.Parse("#5E5E5E");
    private static readonly Color OutgoingColor = Color.Parse("#FF9A28");
    private static readonly Color IncomingColor = Color.Parse("#4FA3E8");
    private static readonly Color StartColor = Color.Parse("#4FB06A");

    private static readonly Dictionary<FsmBoxKind, IBrush> StripBrushes = new()
    {
        [FsmBoxKind.State] = new ImmutableSolidColorBrush(Color.Parse("#4A7FB5")),
        [FsmBoxKind.Group] = new ImmutableSolidColorBrush(Color.Parse("#C0943C")),
        [FsmBoxKind.End] = new ImmutableSolidColorBrush(Color.Parse("#B85454")),
        [FsmBoxKind.Entry] = new ImmutableSolidColorBrush(Color.Parse("#4FB06A")),
        [FsmBoxKind.AnyState] = new ImmutableSolidColorBrush(Color.Parse("#8A8A8A")),
    };

    private static readonly Typeface TitleFace = new(new FontFamily("Segoe UI"), FontStyle.Normal, FontWeight.SemiBold);
    private static readonly Typeface TextFace = new(new FontFamily("Segoe UI"));

    private FsmGraphViewModel? subscribed;
    private double zoom = 1;
    private Vector pan;
    private bool fitPending = true;
    // Until the user pans or zooms, a resize frames the graph again.
    private bool moved;
    private bool panning;
    private Point lastPoint;
    private FsmLayoutBox? hover;

    static FsmGraphCanvas()
    {
        ClipToBoundsProperty.OverrideDefaultValue<FsmGraphCanvas>(true);
        FocusableProperty.OverrideDefaultValue<FsmGraphCanvas>(true);
    }

    public FsmGraphViewModel? Graph
    {
        get => GetValue(GraphProperty);
        set => SetValue(GraphProperty, value);
    }

    private FsmGraphLayout? Layout => Graph?.Layout;

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == GraphProperty) Subscribe(Graph);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Subscribe(Graph);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Subscribe(null);
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e)
    {
        base.OnSizeChanged(e);
        if (fitPending || !moved) Fit();
    }

    private void Subscribe(FsmGraphViewModel? graph)
    {
        if (subscribed == graph) return;
        if (subscribed != null)
        {
            subscribed.FitRequested -= Fit;
            subscribed.RevealRequested -= Reveal;
            subscribed.PropertyChanged -= OnGraphChanged;
        }
        subscribed = graph;
        if (subscribed != null)
        {
            subscribed.FitRequested += Fit;
            subscribed.RevealRequested += Reveal;
            subscribed.PropertyChanged += OnGraphChanged;
        }
        fitPending = true;
        Fit();
    }

    private void OnGraphChanged(object? sender, PropertyChangedEventArgs e) => InvalidateVisual();

    private void Fit()
    {
        Size size = Bounds.Size;
        if (Layout == null || size.Width <= 1 || size.Height <= 1)
        {
            fitPending = true;
            InvalidateVisual();
            return;
        }
        fitPending = false;
        moved = false;
        Rect bounds = Layout.Bounds.Inflate(40);
        zoom = Math.Clamp(Math.Min(size.Width / bounds.Width, size.Height / bounds.Height) * FitMargin, MinZoom, 1.25);
        pan = new Vector(size.Width / 2 - bounds.Center.X * zoom, size.Height / 2 - bounds.Center.Y * zoom);
        InvalidateVisual();
    }

    private void Reveal()
    {
        if (Layout == null || Graph?.Selected is not { } node || Layout.BoxOf(node) is not { } box) return;
        Rect onScreen = new(ToScreen(box.Rect.TopLeft), ToScreen(box.Rect.BottomRight));
        if (!new Rect(Bounds.Size).Contains(onScreen))
        {
            pan = new Vector(Bounds.Width / 2 - box.Rect.Center.X * zoom, Bounds.Height / 2 - box.Rect.Center.Y * zoom);
        }
        InvalidateVisual();
    }

    private Point ToScreen(Point p) => new(p.X * zoom + pan.X, p.Y * zoom + pan.Y);

    private Point ToWorld(Point p) => new((p.X - pan.X) / zoom, (p.Y - pan.Y) / zoom);

    public override void Render(DrawingContext context)
    {
        context.FillRectangle(Background, new Rect(Bounds.Size));
        DrawGrid(context);
        if (Layout == null) return;

        FsmGraphNode? selected = Graph?.Selected;
        using (context.PushTransform(Matrix.CreateScale(zoom, zoom) * Matrix.CreateTranslation(pan.X, pan.Y)))
        {
            foreach (FsmLayoutEdge edge in Layout.Edges)
            {
                if (EdgeColorOf(edge, selected) == null) DrawEdge(context, edge, EdgeColor, 1.2, false);
            }
            foreach (FsmLayoutEdge edge in Layout.Edges)
            {
                if (EdgeColorOf(edge, selected) is { } color) DrawEdge(context, edge, color, 2, true);
            }
            foreach (FsmLayoutBox box in Layout.Boxes) DrawBox(context, box, box.Node != null && box.Node == selected);
        }
    }

    private static Color? EdgeColorOf(FsmLayoutEdge edge, FsmGraphNode? selected)
    {
        if (selected == null) return null;
        if (edge.From.Node == selected) return OutgoingColor;
        if (edge.To.Node == selected) return edge.Kind == FsmEdgeKind.Transition ? IncomingColor : StartColor;
        return null;
    }

    private void DrawGrid(DrawingContext context)
    {
        double step = GridStep;
        while (step * zoom < 8) step *= 8;
        Point topLeft = ToWorld(default);
        for (long i = (long)Math.Floor(topLeft.X / step); ; i++)
        {
            double x = Math.Round(i * step * zoom + pan.X) + 0.5;
            if (x > Bounds.Width) break;
            context.DrawLine(i % 8 == 0 ? GridMajorPen : GridPen, new Point(x, 0), new Point(x, Bounds.Height));
        }
        for (long i = (long)Math.Floor(topLeft.Y / step); ; i++)
        {
            double y = Math.Round(i * step * zoom + pan.Y) + 0.5;
            if (y > Bounds.Height) break;
            context.DrawLine(i % 8 == 0 ? GridMajorPen : GridPen, new Point(0, y), new Point(Bounds.Width, y));
        }
    }

    private static void DrawEdge(DrawingContext context, FsmLayoutEdge edge, Color color, double thickness, bool labelled)
    {
        Rect from = edge.From.Rect;
        Rect to = edge.To.Rect;
        var brush = new ImmutableSolidColorBrush(color);
        var pen = new ImmutablePen(brush, thickness);
        Point start;
        Point end;
        Point c1;
        Point c2;
        if (edge.From == edge.To)
        {
            start = new Point(from.Right - 30, from.Top);
            end = new Point(from.Right - 60, from.Top);
            c1 = start + new Vector(10, -45);
            c2 = end + new Vector(-10, -45);
        }
        else
        {
            start = new Point(from.Right, from.Top + from.Height * edge.FromPort);
            end = new Point(to.Left, to.Top + to.Height * edge.ToPort);
            // Backward edges bulge further out so they do not run through the boxes between.
            double reach = Math.Max(60, Math.Abs(end.X - start.X) * 0.45);
            if (end.X < start.X + 20) reach = 90 + Math.Abs(end.Y - start.Y) * 0.15;
            c1 = start + new Vector(reach, 0);
            c2 = end - new Vector(reach, 0);
        }
        var geometry = new StreamGeometry();
        using (StreamGeometryContext g = geometry.Open())
        {
            g.BeginFigure(start, false);
            g.CubicBezierTo(c1, c2, end);
            g.EndFigure(false);
        }
        context.DrawGeometry(null, pen, geometry);

        Vector tangent = end - c2;
        double length = Math.Sqrt(tangent.X * tangent.X + tangent.Y * tangent.Y);
        Vector dir = length > 0.001 ? tangent / length : new Vector(1, 0);
        Vector normal = new(-dir.Y, dir.X);
        Point tip = end;
        Point baseCenter = tip - dir * ArrowSize * 1.4;
        var arrow = new StreamGeometry();
        using (StreamGeometryContext g = arrow.Open())
        {
            g.BeginFigure(tip, true);
            g.LineTo(baseCenter + normal * ArrowSize * 0.6);
            g.LineTo(baseCenter - normal * ArrowSize * 0.6);
            g.EndFigure(true);
        }
        context.DrawGeometry(brush, null, arrow);

        if (!labelled || edge.Label.Length == 0) return;
        // Label at the curve's midpoint.
        Point mid = new(0.125 * start.X + 0.375 * c1.X + 0.375 * c2.X + 0.125 * end.X,
                        0.125 * start.Y + 0.375 * c1.Y + 0.375 * c2.Y + 0.125 * end.Y);
        FormattedText text = Text(edge.Label, TextFace, 11, new ImmutableSolidColorBrush(color), 0);
        var box = new Rect(mid.X - text.Width / 2 - 4, mid.Y - text.Height / 2 - 1, text.Width + 8, text.Height + 2);
        context.FillRectangle(LabelBackground, box, 3);
        context.DrawText(text, new Point(box.X + 4, box.Y + 1));
    }

    private void DrawBox(DrawingContext context, FsmLayoutBox box, bool selected)
    {
        Rect rect = box.Rect;
        bool pseudo = box.Kind is FsmBoxKind.Entry or FsmBoxKind.AnyState;
        IPen pen = selected ? SelectedPen : box == hover ? HoverPen : BoxPen;
        context.DrawRectangle(pseudo ? PseudoBrush : BoxBrush, pen, rect, pseudo ? 12 : 2);
        if (!pseudo) context.FillRectangle(StripBrushes[box.Kind], new Rect(rect.X + 1, rect.Y + 1, StripWidth, rect.Height - 2));

        double textLeft = rect.X + (pseudo ? 12 : StripWidth + 8);
        double width = rect.Right - textLeft - 6;
        string title = box.Kind == FsmBoxKind.Group ? box.Title + "  \u25B8" : box.Title;
        FormattedText titleText = Text(title, TitleFace, 12, TitleBrush, width);
        if (box.Subtitle.Length == 0)
        {
            context.DrawText(titleText, new Point(textLeft, rect.Center.Y - titleText.Height / 2));
            return;
        }
        FormattedText subtitle = Text(box.Subtitle, TextFace, 11, SubtitleBrush, width);
        context.DrawText(titleText, new Point(textLeft, rect.Y + 5));
        context.DrawText(subtitle, new Point(textLeft, rect.Y + 6 + titleText.Height));
    }

    private static FormattedText Text(string text, Typeface face, double size, IBrush brush, double maxWidth)
    {
        var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, face, size, brush)
        {
            MaxLineCount = 1,
            Trimming = TextTrimming.CharacterEllipsis
        };
        if (maxWidth > 0) formatted.MaxTextWidth = maxWidth;
        return formatted;
    }

    private FsmLayoutBox? BoxAt(Point screen)
    {
        if (Layout == null) return null;
        Point world = ToWorld(screen);
        return Layout.Boxes.LastOrDefault(b => b.Rect.Contains(world));
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        Focus();
        PointerPoint point = e.GetCurrentPoint(this);
        FsmLayoutBox? box = BoxAt(point.Position);
        if (point.Properties.IsLeftButtonPressed && box?.Node is { } node)
        {
            if (e.ClickCount >= 2 && node.IsGroup) Graph?.Open(node);
            else Graph?.Pick(node);
            e.Handled = true;
            return;
        }
        if (point.Properties.IsLeftButtonPressed || point.Properties.IsRightButtonPressed || point.Properties.IsMiddleButtonPressed)
        {
            panning = true;
            lastPoint = point.Position;
            e.Pointer.Capture(this);
            e.Handled = true;
        }
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        Point position = e.GetPosition(this);
        if (panning)
        {
            moved = true;
            pan += position - lastPoint;
            lastPoint = position;
            InvalidateVisual();
            return;
        }
        FsmLayoutBox? over = BoxAt(position);
        if (over == hover) return;
        hover = over;
        InvalidateVisual();
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (!panning) return;
        panning = false;
        e.Pointer.Capture(null);
    }

    protected override void OnPointerExited(PointerEventArgs e)
    {
        base.OnPointerExited(e);
        if (hover == null) return;
        hover = null;
        InvalidateVisual();
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        Point at = e.GetPosition(this);
        double next = Math.Clamp(zoom * Math.Pow(WheelStep, e.Delta.Y), MinZoom, MaxZoom);
        double factor = next / zoom;
        pan = new Vector(at.X - (at.X - pan.X) * factor, at.Y - (at.Y - pan.Y) * factor);
        zoom = next;
        moved = true;
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.F)
        {
            Fit();
            e.Handled = true;
        }
        else if (e.Key == Key.Back && Graph?.UpCommand.CanExecute(null) == true)
        {
            Graph.UpCommand.Execute(null);
            e.Handled = true;
        }
    }
}
