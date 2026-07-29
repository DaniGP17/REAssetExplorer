using System.Globalization;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Media;

namespace REAssetExplorer.Desktop.Models;

public sealed class GuiElement
{
    private readonly Dictionary<string, string> values = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> animated = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> animatedBy = new(StringComparer.Ordinal);

    public GuiElement(OutlineNode node, GuiElement? parent, int index)
    {
        Node = node;
        Parent = parent;
        Index = index;
        foreach (OutlineProperty prop in node.Properties)
        {
            if (prop.Section == node.Detail || prop.Section is "Regions" or "Grid") values[prop.Key] = prop.Value;
        }
        foreach (OutlineNode child in node.Children)
        {
            if (child.Kind == "guielement") Children.Add(new GuiElement(child, this, Children.Count));
        }
    }

    public OutlineNode Node { get; }
    public GuiElement? Parent { get; }
    public int Index { get; }
    public string Key => Node.Key;
    // Class without the via.gui. prefix.
    public string Class => Node.Detail;
    public List<GuiElement> Children { get; } = [];

    public string Text(string name) => Value(name) ?? string.Empty;

    public bool Has(string name) => Value(name) != null;

    public bool Bool(string name, bool fallback) => Value(name) is { } value ? value == "true" : fallback;

    public void SetAnimated(string name, string value, string source)
    {
        animated[name] = value;
        animatedBy[name] = source;
    }

    public void ClearAnimated()
    {
        animated.Clear();
        animatedBy.Clear();
    }

    public IEnumerable<(string Attribute, string Value, string Source)> AnimatedValues() =>
        animated.Select(a => (a.Key, a.Value, animatedBy.GetValueOrDefault(a.Key, string.Empty)));

    private string? Value(string name) =>
        animated.TryGetValue(name, out string? value) || values.TryGetValue(name, out value) ? value : null;

    public float[] Floats(string name)
    {
        if (Value(name) is not { Length: > 0 } value) return [];
        string[] parts = value.Split(',', StringSplitOptions.TrimEntries);
        var result = new float[parts.Length];
        for (int i = 0; i < parts.Length; i++)
        {
            float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out result[i]);
        }
        return result;
    }

    public float Float(string name, float fallback)
    {
        float[] v = Floats(name);
        return v.Length > 0 ? v[0] : fallback;
    }

    public Vector Vector2(string name, Vector fallback)
    {
        float[] v = Floats(name);
        return v.Length >= 2 ? new Vector(v[0], v[1]) : fallback;
    }

    // Color attributes are "r, g, b, a" in 0..255.
    public Color ColorValue(string name, Color fallback)
    {
        float[] v = Floats(name);
        if (v.Length < 4) return fallback;
        return Color.FromArgb(ToByte(v[3]), ToByte(v[0]), ToByte(v[1]), ToByte(v[2]));
    }

    public IEnumerable<GuiElement> DescendantsAndSelf()
    {
        yield return this;
        foreach (GuiElement child in Children)
        {
            foreach (GuiElement e in child.DescendantsAndSelf()) yield return e;
        }
    }

    private static byte ToByte(float v) => (byte)Math.Clamp(Math.Round(v), 0, 255);
}

public enum GuiDrawKind
{
    None,
    Texture,
    TextureSet,
    Scale9Grid,
    Text,
    Rect,
    Circle,
    HitArea,
    Placeholder,
    Mask
}

// An element with MaskType Mask (or MaskTop, MaskTopMost) cuts its siblings and their subtrees to its box.
public readonly record struct GuiMask(Matrix World, Rect Bounds, bool Ellipse);

public sealed class GuiDrawItem(GuiElement element, GuiDrawKind kind, Matrix world, Rect bounds, Vector4Color tint, bool hidden,
                                IReadOnlyList<IReadOnlyList<GuiMask>> masks)
{
    // Each group clips by the union of its shapes; the groups intersect.
    public IReadOnlyList<IReadOnlyList<GuiMask>> Masks { get; } = masks;
    public GuiElement Element { get; } = element;
    public GuiDrawKind Kind { get; } = kind;
    public Matrix World { get; } = world;
    public Rect Bounds { get; } = bounds;
    public Vector4Color Tint { get; } = tint;
    public bool Hidden { get; } = hidden;
    public bool Additive => Element.Text("BlendType") is "Add" or "AddAlpha";
}

public readonly record struct Vector4Color(double R, double G, double B, double A)
{
    public static readonly Vector4Color White = new(1, 1, 1, 1);

    public Vector4Color Multiply(Color c) => new(R * c.R / 255.0, G * c.G / 255.0, B * c.B / 255.0, A * c.A / 255.0);

    public Vector4Color Multiply(Vector4Color c) => new(R * c.R, G * c.G, B * c.B, A * c.A);

    public Color ToColor() => Color.FromArgb(Byte(A), Byte(R), Byte(G), Byte(B));

    public bool IsWhite => R >= 0.999 && G >= 0.999 && B >= 0.999;

    private static byte Byte(double v) => (byte)Math.Clamp(Math.Round(v * 255), 0, 255);
}

// via.gui: Position is in the parent's space (screen pixels, y down); ControlPoint
// names the point of the element's box that sits there.
public sealed partial class GuiScene
{
    public GuiScene(OutlineNode fileNode)
    {
        OutlineNode? view = fileNode.Children.FirstOrDefault(n => n.Kind == "guielement");
        View = view != null ? new GuiElement(view, null, 0) : null;
        string size = fileNode.Properties.FirstOrDefault(p => p.Key == "Screen size")?.Value ?? "1920, 1080";
        string[] parts = size.Split(',', StringSplitOptions.TrimEntries);
        double w = parts.Length > 0 && double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out double a) ? a : 1920;
        double h = parts.Length > 1 && double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out double b) ? b : 1080;
        ScreenSize = new Size(w > 0 ? w : 1920, h > 0 ? h : 1080);
    }

    public GuiElement? View { get; }
    public Size ScreenSize { get; }

    public IEnumerable<GuiElement> Elements => View?.DescendantsAndSelf() ?? [];

    // Draw order, back to front. Elements hidden in the hierarchy are left out;
    // Visible = false ones only when showHidden.
    public List<GuiDrawItem> Layout(IReadOnlySet<string> hiddenKeys, bool showHidden)
    {
        var items = new List<GuiDrawItem>();
        if (View != null) Walk(View, Matrix.Identity, Vector4Color.White, false, [], hiddenKeys, showHidden, items);
        return items;
    }

    public static Matrix WorldOf(GuiElement element)
    {
        Matrix world = Matrix.Identity;
        for (GuiElement? e = element; e != null; e = e.Parent) world *= Local(e);
        return world;
    }

    private static void Walk(GuiElement e, Matrix parentWorld, Vector4Color parentTint, bool parentHidden,
                             IReadOnlyList<IReadOnlyList<GuiMask>> masks, IReadOnlySet<string> hiddenKeys, bool showHidden,
                             List<GuiDrawItem> items)
    {
        if (hiddenKeys.Contains(e.Key)) return;
        bool hidden = parentHidden || !e.Bool("Visible", true);
        if (hidden && !showHidden) return;

        Matrix world = Local(e) * parentWorld;
        Vector4Color tint = parentTint;
        float[] colorScale = e.Floats("ColorScale");
        if (colorScale.Length >= 4) tint = tint.Multiply(new Vector4Color(colorScale[0], colorScale[1], colorScale[2], colorScale[3]));

        GuiDrawKind kind = KindOf(e);
        if (kind != GuiDrawKind.None)
        {
            bool isMask = IsMask(e);
            IReadOnlyList<IReadOnlyList<GuiMask>> clip = isMask || e.Text("MaskType") == "NonTarget" ? [] : masks;
            items.Add(new GuiDrawItem(e, isMask ? GuiDrawKind.Mask : kind, world, BoundsOf(e, kind), tint, hidden, clip));
        }

        IReadOnlyList<IReadOnlyList<GuiMask>> childMasks = e.Text("MaskMode") == "Disable" ? [] : masks;
        var level = new List<GuiMask>();
        foreach (GuiElement child in e.Children)
        {
            if (!IsMask(child) || !child.Bool("Visible", true) || hiddenKeys.Contains(child.Key)) continue;
            GuiDrawKind childKind = KindOf(child);
            level.Add(new GuiMask(Local(child) * world, BoundsOf(child, childKind), childKind == GuiDrawKind.Circle));
        }
        if (level.Count > 0) childMasks = [.. childMasks, level];

        // Higher Priority on top; among equals, the child listed first.
        foreach (GuiElement child in e.Children.OrderBy(c => c.Float("Priority", 0)).ThenByDescending(c => c.Index))
        {
            Walk(child, world, tint, hidden, childMasks, hiddenKeys, showHidden, items);
        }
    }

    private static bool IsMask(GuiElement e) => e.Text("MaskType").StartsWith("Mask", StringComparison.Ordinal);

    private static Matrix Local(GuiElement e)
    {
        if (e.Class == "View") return Matrix.Identity;
        float[] position = e.Floats("Position");
        float[] rotation = e.Floats("Rotation");
        float[] scale = e.Floats("Scale");
        double sx = scale.Length >= 1 ? scale[0] : 1;
        double sy = scale.Length >= 2 ? scale[1] : 1;
        double angle = rotation.Length >= 3 ? rotation[2] : 0;
        double px = position.Length >= 1 ? position[0] : 0;
        double py = position.Length >= 2 ? position[1] : 0;
        return Matrix.CreateScale(sx, sy) * Matrix.CreateRotation(angle * Math.PI / 180) * Matrix.CreateTranslation(px, py);
    }

    private static GuiDrawKind KindOf(GuiElement e) => e.Class switch
    {
        "Texture" => GuiDrawKind.Texture,
        "TextureSet" => GuiDrawKind.TextureSet,
        "Scale9Grid" => GuiDrawKind.Scale9Grid,
        "Text" or "MaterialText" => GuiDrawKind.Text,
        "Rect" => GuiDrawKind.Rect,
        "Circle" => GuiDrawKind.Circle,
        "HitArea" => GuiDrawKind.HitArea,
        "Material" or "BlurFilter" => GuiDrawKind.Placeholder,
        _ => GuiDrawKind.None
    };

    public static Rect BoundsOf(GuiElement e, GuiDrawKind kind)
    {
        Vector size = kind is GuiDrawKind.Text or GuiDrawKind.TextureSet
            ? e.Vector2("RegionSize", default)
            : e.Vector2("Size", default);
        if (kind == GuiDrawKind.TextureSet && (size.X <= 0 || size.Y <= 0))
        {
            Rect union = default;
            foreach (GuiTextureRegion region in Regions(e))
            {
                union = union.Width <= 0 ? region.Rect : union.Union(region.Rect);
            }
            return union;
        }
        (double ax, double ay) = Anchor(e.Text("ControlPoint"));
        return new Rect(-ax * size.X, -ay * size.Y, Math.Max(size.X, 0), Math.Max(size.Y, 0));
    }

    public static (double X, double Y) Anchor(string controlPoint)
    {
        double x = controlPoint.StartsWith("Left", StringComparison.Ordinal) ? 0
            : controlPoint.StartsWith("Right", StringComparison.Ordinal) ? 1 : 0.5;
        double y = controlPoint.EndsWith("Top", StringComparison.Ordinal) ? 0
            : controlPoint.EndsWith("Bottom", StringComparison.Ordinal) ? 1 : 0.5;
        return (x, y);
    }

    public static IEnumerable<GuiTextureRegion> Regions(GuiElement e)
    {
        for (int i = 0; ; i++)
        {
            float[] v = e.Floats($"Region {i}");
            if (v.Length < 6) yield break;
            yield return new GuiTextureRegion((int)v[0], (int)v[1], new Rect(new Point(v[2], v[3]), new Point(v[4], v[5])));
        }
    }

    // "Message text" is the resolved message; "Message" the literal text.
    public static string DisplayText(GuiElement e)
    {
        string text = e.Text("Message text");
        if (text.Length == 0) text = e.Text("Message");
        text = IconTag().Replace(text, m => "[" + m.Groups[1].Value + "]");
        return AnyTag().Replace(text, string.Empty).Replace("\r\n", "\n");
    }

    [GeneratedRegex(@"<ICON\s+([^>]+)>")]
    private static partial Regex IconTag();

    [GeneratedRegex(@"<[^>]*>")]
    private static partial Regex AnyTag();
}

public readonly record struct GuiTextureRegion(int Sequence, int Pattern, Rect Rect);
