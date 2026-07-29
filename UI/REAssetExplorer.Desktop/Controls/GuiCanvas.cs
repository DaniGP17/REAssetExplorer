using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Media.Immutable;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Controls;

public sealed class GuiCanvas : Control
{
    public static readonly StyledProperty<GuiPreviewViewModel?> PreviewProperty =
        AvaloniaProperty.Register<GuiCanvas, GuiPreviewViewModel?>(nameof(Preview));

    private const double FitMargin = 0.92;
    private const double WheelStep = 1.15;
    private const double MinZoom = 0.02;
    private const double MaxZoom = 32;
    private const double HiddenOpacity = 0.35;

    private static readonly IBrush Background = new ImmutableSolidColorBrush(Color.Parse("#1A1A1A"));
    private static readonly IBrush ScreenBrush = new ImmutableSolidColorBrush(Color.Parse("#0E0E0E"));
    private static readonly IBrush CheckerDark = new ImmutableSolidColorBrush(Color.Parse("#262626"));
    private static readonly IBrush CheckerLight = new ImmutableSolidColorBrush(Color.Parse("#303030"));
    private static readonly IPen ScreenPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#5A5A5A")));
    private static readonly IPen BoundsPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#8040C0E0")));
    private static readonly IPen SelectedPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#FF9A28")), 2);
    private static readonly IPen GroupPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#C0FF9A28")), 1,
                                                             new ImmutableDashStyle([4, 3], 0));
    private static readonly IPen MaskPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#C0E060E0")), 1,
                                                            new ImmutableDashStyle([2, 2], 0));
    private static readonly IBrush PendingBrush = new ImmutableSolidColorBrush(Color.Parse("#30FFFFFF"));
    private static readonly IBrush HitBrush = new ImmutableSolidColorBrush(Color.Parse("#2840C878"));
    private static readonly IPen HitPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#A040C878")));
    private static readonly IBrush PlaceholderBrush = new ImmutableSolidColorBrush(Color.Parse("#20A0A0A0"));
    private static readonly IPen PlaceholderPen = new ImmutablePen(new ImmutableSolidColorBrush(Color.Parse("#80A0A0A0")), 1,
                                                                   new ImmutableDashStyle([3, 3], 0));
    private static readonly Typeface TextFace = new(new FontFamily("Segoe UI"));
    private static readonly RenderOptions AddBlend = new() { BitmapBlendingMode = BitmapBlendingMode.Plus };

    private GuiPreviewViewModel? subscribed;
    private double zoom = 1;
    private Vector pan;
    private bool fitPending = true;
    private bool panning;
    private Point lastPoint;
    // Text elements with no region size take the size of their text; kept for picking.
    private readonly Dictionary<GuiDrawItem, Rect> textBoxes = [];

    static GuiCanvas()
    {
        ClipToBoundsProperty.OverrideDefaultValue<GuiCanvas>(true);
        FocusableProperty.OverrideDefaultValue<GuiCanvas>(true);
    }

    public GuiPreviewViewModel? Preview
    {
        get => GetValue(PreviewProperty);
        set => SetValue(PreviewProperty, value);
    }

    private Matrix View => Matrix.CreateScale(zoom, zoom) * Matrix.CreateTranslation(pan.X, pan.Y);

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == PreviewProperty) Subscribe(Preview);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Subscribe(Preview);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Subscribe(null);
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e)
    {
        base.OnSizeChanged(e);
        if (fitPending) Fit();
    }

    private void Subscribe(GuiPreviewViewModel? preview)
    {
        if (subscribed != null)
        {
            subscribed.Invalidated -= InvalidateVisual;
            subscribed.FitRequested -= Fit;
        }
        subscribed = preview;
        if (subscribed != null)
        {
            subscribed.Invalidated += InvalidateVisual;
            subscribed.FitRequested += Fit;
        }
        fitPending = true;
        Fit();
    }

    private void Fit()
    {
        Size screen = Preview?.Scene?.ScreenSize ?? new Size(1920, 1080);
        Size size = Bounds.Size;
        if (size.Width <= 1 || size.Height <= 1)
        {
            fitPending = true;
            return;
        }
        fitPending = false;
        zoom = Math.Min(size.Width / screen.Width, size.Height / screen.Height) * FitMargin;
        pan = new Vector((size.Width - screen.Width * zoom) / 2, (size.Height - screen.Height * zoom) / 2);
        UpdateZoomText();
        InvalidateVisual();
    }

    private void UpdateZoomText()
    {
        if (Preview != null) Preview.ZoomText = (zoom * 100).ToString("0", CultureInfo.InvariantCulture) + "%";
    }

    public override void Render(DrawingContext context)
    {
        context.FillRectangle(Background, new Rect(Bounds.Size));
        textBoxes.Clear();
        GuiPreviewViewModel? preview = Preview;
        if (preview?.Scene == null || preview.Images == null) return;
        Size screen = preview.Scene.ScreenSize;

        using (context.PushTransform(View))
        {
            DrawScreen(context, screen, preview.CheckerBackground);
            foreach (GuiDrawItem item in preview.Items) DrawItem(context, preview, item);
        }

        // Overlays in control space so their lines stay one pixel wide.
        if (preview.ShowBounds)
        {
            foreach (GuiDrawItem item in preview.Items)
            {
                if (item.Kind == GuiDrawKind.HitArea && !preview.ShowHitAreas) continue;
                DrawQuad(context, item.Kind == GuiDrawKind.Mask ? MaskPen : BoundsPen, item.World, BoxOf(item));
            }
        }
        context.DrawRectangle(null, ScreenPen, new Rect(pan.X - 0.5, pan.Y - 0.5, screen.Width * zoom + 1, screen.Height * zoom + 1));
        DrawSelection(context, preview);
    }

    private void DrawScreen(DrawingContext context, Size screen, bool checker)
    {
        var area = new Rect(screen);
        context.FillRectangle(checker ? CheckerDark : ScreenBrush, area);
        if (!checker) return;
        double cell = 16 / zoom;
        Rect visible = new Rect(-pan.X / zoom, -pan.Y / zoom, Bounds.Width / zoom, Bounds.Height / zoom).Intersect(area);
        if (visible.Width <= 0 || visible.Height <= 0) return;
        using (context.PushClip(area))
        {
            int x0 = (int)Math.Floor(visible.Left / cell);
            int y0 = (int)Math.Floor(visible.Top / cell);
            int x1 = (int)Math.Ceiling(visible.Right / cell);
            int y1 = (int)Math.Ceiling(visible.Bottom / cell);
            for (int y = y0; y < y1; y++)
            {
                for (int x = x0; x < x1; x++)
                {
                    if ((x + y) % 2 != 0) context.FillRectangle(CheckerLight, new Rect(x * cell, y * cell, cell, cell));
                }
            }
        }
    }

    private void DrawItem(DrawingContext context, GuiPreviewViewModel preview, GuiDrawItem item)
    {
        if (item.Kind == GuiDrawKind.Mask || (item.Kind == GuiDrawKind.HitArea && !preview.ShowHitAreas)) return;
        var clips = new List<DrawingContext.PushedState>();
        foreach (IReadOnlyList<GuiMask> group in item.Masks) clips.Add(context.PushGeometryClip(MaskGeometry(group)));
        try
        {
            DrawClipped(context, preview, item);
        }
        finally
        {
            for (int i = clips.Count - 1; i >= 0; i--) clips[i].Dispose();
        }
    }

    private static Geometry MaskGeometry(IReadOnlyList<GuiMask> group)
    {
        var shapes = new GeometryGroup { FillRule = FillRule.NonZero };
        foreach (GuiMask mask in group)
        {
            Geometry shape = mask.Ellipse ? new EllipseGeometry(mask.Bounds) : new RectangleGeometry(mask.Bounds);
            shape.Transform = new MatrixTransform(mask.World);
            shapes.Children.Add(shape);
        }
        return shapes;
    }

    private void DrawClipped(DrawingContext context, GuiPreviewViewModel preview, GuiDrawItem item)
    {
        GuiElement e = item.Element;
        Vector4Color color = item.Tint.Multiply(e.ColorValue("Color", Colors.White));
        double faded = item.Hidden ? HiddenOpacity : 1;
        // Rects carry their opacity in their (gradient) colors.
        double opacity = item.Kind == GuiDrawKind.Rect ? faded : Math.Clamp(color.A, 0, 1) * faded;
        if (opacity <= 0.001 && item.Kind != GuiDrawKind.HitArea) return;

        using (context.PushTransform(item.World))
        using (context.PushOpacity(opacity))
        using (item.Additive ? context.PushRenderOptions(AddBlend) : default(DrawingContext.PushedState?))
        {
            switch (item.Kind)
            {
                case GuiDrawKind.Texture:
                    DrawTexture(context, preview.Images!, e, item.Bounds, color);
                    break;
                case GuiDrawKind.TextureSet:
                    DrawTextureSet(context, preview.Images!, e, item.Bounds, color);
                    break;
                case GuiDrawKind.Scale9Grid:
                    DrawScale9(context, preview.Images!, e, item.Bounds, color);
                    break;
                case GuiDrawKind.Text:
                    DrawText(context, item, color);
                    break;
                case GuiDrawKind.Rect:
                    DrawRect(context, e, item.Bounds, item.Tint);
                    break;
                case GuiDrawKind.Circle:
                    DrawCircle(context, e, item.Bounds, color);
                    break;
                case GuiDrawKind.HitArea:
                    context.DrawRectangle(HitBrush, HitPen, item.Bounds);
                    break;
                case GuiDrawKind.Placeholder:
                    context.DrawRectangle(PlaceholderBrush, PlaceholderPen, item.Bounds);
                    break;
            }
        }
    }

    private static void DrawImage(DrawingContext context, GuiImageCache images, Bitmap bitmap, Rect source, Rect dest,
                                  Vector4Color color)
    {
        if (dest.Width <= 0 || dest.Height <= 0 || source.Width <= 0 || source.Height <= 0) return;
        if (color.IsWhite)
        {
            context.DrawImage(bitmap, source, dest);
            return;
        }
        IImage tinted = images.Tinted(bitmap, source, color.ToColor());
        context.DrawImage(tinted, new Rect(tinted.Size), dest);
    }

    private static void DrawTexture(DrawingContext context, GuiImageCache images, GuiElement e, Rect bounds, Vector4Color color)
    {
        (Bitmap Bitmap, Rect Source)? image = null;
        if (e.Text("AssetType") == "Texture" || (e.Text("UVSequence").Length == 0 && e.Text("Texture").Length > 0))
        {
            Bitmap? bitmap = images.Texture(e.Text("Texture"));
            if (bitmap != null)
            {
                Rect source = new(bitmap.Size);
                if (e.Has("U0") && e.Has("U1") && e.Float("U1", 0) > e.Float("U0", 0))
                {
                    source = new Rect(new Point(e.Float("U0", 0), e.Float("V0", 0)), new Point(e.Float("U1", 0), e.Float("V1", 0)));
                }
                image = (bitmap, source);
            }
        }
        else
        {
            image = images.Pattern(e.Text("UVSequence"), (int)e.Float("UVSequenceNo", 0), (int)e.Float("UVPatternNo", 0));
        }
        if (image is { } found) DrawImage(context, images, found.Bitmap, found.Source, bounds, color);
        else context.FillRectangle(PendingBrush, bounds);
    }

    private static void DrawTextureSet(DrawingContext context, GuiImageCache images, GuiElement e, Rect bounds, Vector4Color color)
    {
        Vector region = e.Vector2("RegionSize", default);
        (double ax, double ay) = GuiScene.Anchor(e.Text("ControlPoint"));
        Vector offset = region.X > 0 && region.Y > 0 ? new Vector(-ax * region.X, -ay * region.Y) : default;
        string uvs = e.Text("UVSequence");
        foreach (GuiTextureRegion r in GuiScene.Regions(e))
        {
            Rect dest = r.Rect.Translate(offset);
            if (images.Pattern(uvs, r.Sequence, r.Pattern) is { } image) DrawImage(context, images, image.Bitmap, image.Source, dest, color);
            else context.FillRectangle(PendingBrush, dest);
        }
    }

    private static void DrawScale9(DrawingContext context, GuiImageCache images, GuiElement e, Rect bounds, Vector4Color color)
    {
        float[] border = e.Floats("Border");
        if (border.Length < 4)
        {
            context.FillRectangle(PendingBrush, bounds);
            return;
        }
        double sx = Math.Min(1, bounds.Width / Math.Max(border[0] + border[2], 1e-3));
        double sy = Math.Min(1, bounds.Height / Math.Max(border[1] + border[3], 1e-3));
        double[] xs = [bounds.Left, bounds.Left + border[0] * sx, bounds.Right - border[2] * sx, bounds.Right];
        double[] ys = [bounds.Top, bounds.Top + border[1] * sy, bounds.Bottom - border[3] * sy, bounds.Bottom];
        string uvs = e.Text("UVSequence");
        for (int cell = 0; cell < 9; cell++)
        {
            float[] v = e.Floats($"Cell {cell}");
            if (v.Length < 2) continue;
            int col = cell % 3;
            int row = cell / 3;
            var dest = new Rect(new Point(xs[col], ys[row]), new Point(xs[col + 1], ys[row + 1]));
            if (images.Pattern(uvs, (int)v[0], (int)v[1]) is { } image) DrawImage(context, images, image.Bitmap, image.Source, dest, color);
        }
    }

    private void DrawText(DrawingContext context, GuiDrawItem item, Vector4Color color)
    {
        GuiElement e = item.Element;
        FontFamily? family = Preview?.Fonts?.Slot(e.Text("FontSlot"));
        var face = new Typeface(family ?? TextFace.FontFamily, e.Bool("Italic", false) ? FontStyle.Italic : FontStyle.Normal,
                                e.Bool("Bold", false) ? FontWeight.Bold : FontWeight.Normal);
        string text = GuiScene.DisplayText(e);
        if (text.Length == 0)
        {
            textBoxes[item] = item.Bounds;
            return;
        }
        Vector fontSize = e.Vector2("FontSize", new Vector(24, 24));
        double size = fontSize.Y > 0 ? fontSize.Y : 24;
        Rect box = item.Bounds;
        bool wrap = e.Bool("AutoWrap", false) && box.Width > 0;
        FormattedText formatted = Format(text, face, size, new ImmutableSolidColorBrush(Opaque(color.ToColor())), wrap ? box.Width : 0);
        (double ha, double va) = GuiScene.Anchor(e.Text("LetterAlignment"));
        formatted.TextAlignment = ha < 0.25 ? TextAlignment.Left : ha > 0.75 ? TextAlignment.Right : TextAlignment.Center;
        if (box.Width <= 0 || box.Height <= 0)
        {
            (double ax, double ay) = GuiScene.Anchor(e.Text("ControlPoint"));
            box = new Rect(-ax * formatted.Width, -ay * formatted.Height, formatted.Width, formatted.Height);
        }
        textBoxes[item] = box;
        var origin = new Point(wrap ? box.X : box.X + (box.Width - formatted.Width) * ha, box.Y + (box.Height - formatted.Height) * va);

        if (e.Bool("ShadowEnable", false))
        {
            Color shadow = new Vector4Color(1, 1, 1, 1).Multiply(e.ColorValue("ShadowColor", Colors.Black)).ToColor();
            double distance = e.Float("ShadowDistance", 0) * 0.3;
            double angle = e.Float("ShadowRotation", 45) * Math.PI / 180;
            FormattedText shadowText = Format(text, face, size,
                                              new ImmutableSolidColorBrush(Color.FromArgb((byte)(shadow.A * 0.6), shadow.R, shadow.G, shadow.B)),
                                              wrap ? box.Width : 0);
            shadowText.TextAlignment = formatted.TextAlignment;
            context.DrawText(shadowText, origin + new Vector(Math.Cos(angle), Math.Sin(angle)) * distance);
        }
        context.DrawText(formatted, origin);
    }

    private static FormattedText Format(string text, Typeface face, double size, IBrush brush, double maxWidth)
    {
        var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, face, size, brush);
        if (maxWidth > 0) formatted.MaxTextWidth = maxWidth;
        return formatted;
    }

    private static void DrawRect(DrawingContext context, GuiElement e, Rect bounds, Vector4Color tint)
    {
        Color Tinted(string name) => tint.Multiply(e.ColorValue(name, e.ColorValue("Color", Colors.White))).ToColor();
        IBrush brush;
        string type = e.Text("ColorType");
        if (type != "Fill" && e.Has("ColorLeft") && e.Has("ColorRight"))
        {
            brush = Gradient(Tinted("ColorLeft"), Tinted("ColorRight"), new RelativePoint(0, 0.5, RelativeUnit.Relative),
                             new RelativePoint(1, 0.5, RelativeUnit.Relative));
        }
        else if (type != "Fill" && e.Has("ColorTop") && e.Has("ColorBottom"))
        {
            brush = Gradient(Tinted("ColorTop"), Tinted("ColorBottom"), new RelativePoint(0.5, 0, RelativeUnit.Relative),
                             new RelativePoint(0.5, 1, RelativeUnit.Relative));
        }
        else if (type != "Fill" && e.Has("ColorLeftTop") && e.Has("ColorRightBottom"))
        {
            brush = Gradient(Tinted("ColorLeftTop"), Tinted("ColorRightBottom"), new RelativePoint(0, 0, RelativeUnit.Relative),
                             new RelativePoint(1, 1, RelativeUnit.Relative));
        }
        else
        {
            brush = new ImmutableSolidColorBrush(Tinted("Color"));
        }
        context.FillRectangle(brush, bounds);
    }

    private static IBrush Gradient(Color from, Color to, RelativePoint start, RelativePoint end) => new LinearGradientBrush
    {
        StartPoint = start,
        EndPoint = end,
        GradientStops = [new GradientStop(from, 0), new GradientStop(to, 1)]
    };

    // ArcStart and ArcAngle are degrees from the top, clockwise.
    private static void DrawCircle(DrawingContext context, GuiElement e, Rect bounds, Vector4Color color)
    {
        if (bounds.Width <= 0 || bounds.Height <= 0) return;
        var brush = new ImmutableSolidColorBrush(Opaque(color.ToColor()));
        double inner = Math.Clamp(e.Float("InnerRatio", 0), 0, 0.999);
        float[] arc = e.Floats("ArcAngle");
        double start = e.Float("ArcStart", 0) + (arc.Length >= 2 ? arc[0] : 0);
        double end = e.Float("ArcStart", 0) + (arc.Length >= 2 ? arc[1] : 360);
        if (end - start >= 359.9 && inner <= 0)
        {
            context.DrawEllipse(brush, null, bounds);
            return;
        }
        Point center = bounds.Center;
        double rx = bounds.Width / 2;
        double ry = bounds.Height / 2;
        const int segments = 72;
        var geometry = new StreamGeometry();
        using (StreamGeometryContext g = geometry.Open())
        {
            Point At(double degrees, double scale)
            {
                double r = (degrees - 90) * Math.PI / 180;
                return new Point(center.X + Math.Cos(r) * rx * scale, center.Y + Math.Sin(r) * ry * scale);
            }
            g.BeginFigure(At(start, 1), true);
            for (int s = 1; s <= segments; s++) g.LineTo(At(start + (end - start) * s / segments, 1));
            if (inner > 0)
            {
                for (int s = segments; s >= 0; s--) g.LineTo(At(start + (end - start) * s / segments, inner));
            }
            else
            {
                g.LineTo(center);
            }
            g.EndFigure(true);
        }
        context.DrawGeometry(brush, null, geometry);
    }

    private static Color Opaque(Color c) => Color.FromRgb(c.R, c.G, c.B);

    private Rect BoxOf(GuiDrawItem item) => textBoxes.TryGetValue(item, out Rect box) ? box : item.Bounds;

    private void DrawSelection(DrawingContext context, GuiPreviewViewModel preview)
    {
        string? key = preview.SelectedKey;
        if (string.IsNullOrEmpty(key) || preview.Scene == null) return;
        GuiDrawItem? item = preview.Items.FirstOrDefault(i => i.Element.Key == key);
        if (item != null)
        {
            DrawQuad(context, SelectedPen, item.World, BoxOf(item));
            return;
        }
        GuiElement? element = preview.Scene.Elements.FirstOrDefault(el => el.Key == key);
        if (element == null) return;
        var keys = element.DescendantsAndSelf().Select(el => el.Key).ToHashSet();
        Rect? union = null;
        foreach (GuiDrawItem child in preview.Items)
        {
            if (!keys.Contains(child.Element.Key)) continue;
            Rect r = TransformBounds(child.World, BoxOf(child));
            union = union == null ? r : union.Value.Union(r);
        }
        if (union is { } area) context.DrawRectangle(null, GroupPen, area);
        Point origin = new Point(0, 0).Transform(GuiScene.WorldOf(element) * View);
        context.DrawLine(SelectedPen, origin - new Vector(6, 0), origin + new Vector(6, 0));
        context.DrawLine(SelectedPen, origin - new Vector(0, 6), origin + new Vector(0, 6));
    }

    private void DrawQuad(DrawingContext context, IPen pen, Matrix world, Rect box)
    {
        Matrix m = world * View;
        Point a = box.TopLeft.Transform(m);
        Point b = box.TopRight.Transform(m);
        Point c = box.BottomRight.Transform(m);
        Point d = box.BottomLeft.Transform(m);
        context.DrawLine(pen, a, b);
        context.DrawLine(pen, b, c);
        context.DrawLine(pen, c, d);
        context.DrawLine(pen, d, a);
    }

    private Rect TransformBounds(Matrix world, Rect box)
    {
        Matrix m = world * View;
        Point[] corners = [box.TopLeft.Transform(m), box.TopRight.Transform(m), box.BottomRight.Transform(m), box.BottomLeft.Transform(m)];
        double left = corners.Min(p => p.X);
        double top = corners.Min(p => p.Y);
        return new Rect(left, top, corners.Max(p => p.X) - left, corners.Max(p => p.Y) - top);
    }

    private string? PickAt(Point point)
    {
        GuiPreviewViewModel? preview = Preview;
        if (preview == null) return null;
        for (int i = preview.Items.Count - 1; i >= 0; i--)
        {
            GuiDrawItem item = preview.Items[i];
            if (item.Kind == GuiDrawKind.Mask || (item.Kind == GuiDrawKind.HitArea && !preview.ShowHitAreas)) continue;
            Matrix m = item.World * View;
            if (!m.TryInvert(out Matrix inverse)) continue;
            Rect box = BoxOf(item);
            if (box.Width > 0 && box.Height > 0 && box.Contains(point.Transform(inverse))) return item.Element.Key;
        }
        return null;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        Focus();
        PointerPoint point = e.GetCurrentPoint(this);
        if (point.Properties.IsRightButtonPressed || point.Properties.IsMiddleButtonPressed)
        {
            panning = true;
            lastPoint = point.Position;
            e.Pointer.Capture(this);
            e.Handled = true;
        }
        else if (point.Properties.IsLeftButtonPressed)
        {
            Preview?.Pick(PickAt(point.Position));
            e.Handled = true;
        }
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (!panning) return;
        Point position = e.GetPosition(this);
        pan += position - lastPoint;
        lastPoint = position;
        InvalidateVisual();
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (!panning) return;
        panning = false;
        e.Pointer.Capture(null);
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        Point at = e.GetPosition(this);
        double next = Math.Clamp(zoom * Math.Pow(WheelStep, e.Delta.Y), MinZoom, MaxZoom);
        double factor = next / zoom;
        pan = new Vector(at.X - (at.X - pan.X) * factor, at.Y - (at.Y - pan.Y) * factor);
        zoom = next;
        UpdateZoomText();
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
    }
}
