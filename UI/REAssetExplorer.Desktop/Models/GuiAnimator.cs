using System.Globalization;

namespace REAssetExplorer.Desktop.Models;

// As the engine: each element whose container has clips is in one state (its PlayState,
// else DEFAULT) that animates the container's children.
public sealed class GuiAnimator
{
    private readonly List<State> states = [];
    private readonly Dictionary<string, State> byKey = new(StringComparer.Ordinal);

    public GuiAnimator(GuiScene scene, GuiClipTable table)
    {
        Scene = scene;
        // Tree order: a parent's clip applies before its children's own clips.
        foreach (GuiElement element in scene.Elements)
        {
            if (!table.ByElement.TryGetValue(element.Key, out List<GuiClip>? clips) || clips.Count == 0) continue;
            var state = new State(element, clips);
            states.Add(state);
            byKey[element.Key] = state;
        }
        Fps = scene.View?.Float("BaseFps", 60) is > 0 and var fps ? fps : 60;
        Reset();
    }

    public GuiScene Scene { get; }
    public double Fps { get; }
    public bool Loop { get; set; }

    public IEnumerable<GuiElement> Owners => states.Select(s => s.Owner);

    public bool IsAnimating => states.Any(s => s.Playing && s.Current is { IsStatic: false });

    public IReadOnlyList<GuiClip> ClipsOf(GuiElement owner) =>
        byKey.TryGetValue(owner.Key, out State? state) ? state.Available : [];

    public GuiClip? CurrentClip(GuiElement owner) => byKey.GetValueOrDefault(owner.Key)?.Current;

    public double CurrentFrame(GuiElement owner) => byKey.GetValueOrDefault(owner.Key)?.Frame ?? 0;

    public void Reset()
    {
        foreach (State state in states)
        {
            state.RequestedByParent = null;
            string initial = state.Owner.Text("PlayState");
            state.Switch(state.Find(initial.Length > 0 ? initial : "DEFAULT") ?? state.Find("DEFAULT") ?? state.Available.FirstOrDefault());
            state.Frame = Math.Max(state.Owner.Float("PlayFrame", 0), 0);
            state.Playing = state.Owner.Bool("Play", true);
        }
        Apply();
    }

    public void Play(GuiElement owner, string clip)
    {
        if (!byKey.TryGetValue(owner.Key, out State? state)) return;
        state.Switch(state.Find(clip));
        state.Playing = true;
        Apply();
    }

    public void Seek(GuiElement owner, double frame)
    {
        if (!byKey.TryGetValue(owner.Key, out State? state)) return;
        state.Frame = Math.Max(frame, 0);
        Apply();
    }

    public void Advance(double seconds)
    {
        double frames = seconds * Fps;
        foreach (State state in states)
        {
            if (!state.Playing || state.Current is not { IsStatic: false } clip) continue;
            state.Frame += frames;
            if (state.Frame < clip.Frames) continue;
            if (clip.Next.Length > 0 && state.Find(clip.Next) is { } next)
            {
                double over = state.Frame - clip.Frames;
                state.Switch(next);
                state.Frame = next.IsStatic ? 0 : Math.Min(over, next.Frames);
            }
            else if (Loop || clip.Loops)
            {
                state.Frame %= clip.Frames;
            }
            else
            {
                state.Frame = clip.Frames;
                state.Playing = false;
            }
        }
        Apply();
    }

    public void Apply()
    {
        foreach (GuiElement element in Scene.Elements) element.ClearAnimated();
        // A PlayState key can switch a panel whose state was applied already: settle in a few passes.
        for (int pass = 0; pass < 3; pass++)
        {
            bool switched = false;
            foreach (State state in states)
            {
                if (state.Current == null) continue;
                foreach (GuiClipTrack track in state.Current.Tracks)
                {
                    GuiElement? target = track.Root ? state.Owner : state.Owner.Children.FirstOrDefault(c => c.Node.Name == track.Name);
                    if (target == null) continue;
                    foreach (GuiClipCurve curve in track.Curves)
                    {
                        string? value = Evaluate(curve, state.Frame);
                        if (value == null) continue;
                        // Only a change of the requested state switches the child, so a clip
                        // played on it by hand is not overridden every frame.
                        if (curve.Attribute == "PlayState" && byKey.TryGetValue(target.Key, out State? child) &&
                            child.RequestedByParent != value)
                        {
                            child.RequestedByParent = value;
                            if (child.Current?.Name != value && child.Find(value) is { } clip)
                            {
                                child.Switch(clip);
                                child.Playing = true;
                                switched = true;
                            }
                        }
                        Set(target, curve, value, $"{state.Owner.Node.Name} {state.Current.Name}");
                    }
                }
            }
            if (!switched) break;
            foreach (GuiElement element in Scene.Elements) element.ClearAnimated();
        }
    }

    private static void Set(GuiElement target, GuiClipCurve curve, string value, string source)
    {
        if (curve.Component.Length == 0)
        {
            target.SetAnimated(curve.Attribute, curve.AttributeType == 1 ? (value != "0" ? "true" : "false") : value, source);
            return;
        }
        int index = ComponentIndex(curve.AttributeType, curve.Component);
        if (index < 0) return;
        float[] current = target.Floats(curve.Attribute);
        // Attributes absent from the file start at their engine defaults.
        string fallback = curve.AttributeType == 0x18 ? "255" : curve.Attribute is "ColorScale" or "Scale" ? "1" : "0";
        var parts = new string[Math.Max(current.Length, Math.Max(index + 1, ComponentCount(curve.AttributeType)))];
        for (int i = 0; i < parts.Length; i++) parts[i] = i < current.Length ? Format(current[i]) : fallback;
        parts[index] = value;
        target.SetAnimated(curve.Attribute, string.Join(", ", parts), source);
    }

    private static int ComponentCount(int attributeType) => attributeType switch
    {
        0x15 or 0x19 or 0x1A or 0x1D or 0x1F or 0x26 or 0x23 => 2,
        0x16 or 0x1B or 0x1E or 0x27 or 0x24 or 0x39 => 3,
        _ => 4
    };

    private static int ComponentIndex(int attributeType, string component) => (attributeType, component) switch
    {
        (0x18, "r") => 0,
        (0x18, "g") => 1,
        (0x18, "b") => 2,
        (0x18, "a") => 3,
        (0x1F, "w") => 0,
        (0x1F, "h") => 1,
        (_, "x" or "r" or "left" or "s") => 0,
        (_, "y" or "g" or "top") => 1,
        (_, "z" or "b" or "right") => 2,
        (_, "w" or "a" or "bottom") => 3,
        _ => -1
    };

    private static string? Evaluate(GuiClipCurve curve, double frame)
    {
        List<GuiClipKey> keys = curve.Keys;
        if (keys.Count == 0) return null;
        if (frame <= keys[0].Frame) return keys[0].Value;
        int i = keys.Count - 1;
        while (i > 0 && keys[i].Frame > frame) i--;
        if (i == keys.Count - 1) return keys[i].Value;
        GuiClipKey a = keys[i];
        GuiClipKey b = keys[i + 1];
        if (a.Interpolation == 1 || b.Frame <= a.Frame || !TryNumber(a.Value, out double va) || !TryNumber(b.Value, out double vb))
        {
            return a.Value;
        }
        double t = (frame - a.Frame) / (b.Frame - a.Frame);
        if (a.Interpolation == 5) t = Bezier(t, a.X1, a.Y1, a.X2, a.Y2);
        return Format(va + (vb - va) * t);
    }

    // Cubic bezier easing through (0,0), (x1,y1), (x2,y2), (1,1): y where x = t.
    private static double Bezier(double t, double x1, double y1, double x2, double y2)
    {
        double u = t;
        for (int step = 0; step < 8; step++)
        {
            double x = Cubic(u, x1, x2) - t;
            double dx = 3 * (1 - u) * (1 - u) * x1 + 6 * (1 - u) * u * (x2 - x1) + 3 * u * u * (1 - x2);
            if (Math.Abs(dx) < 1e-6) break;
            u = Math.Clamp(u - x / dx, 0, 1);
        }
        return Cubic(u, y1, y2);
    }

    private static double Cubic(double u, double p1, double p2) =>
        3 * (1 - u) * (1 - u) * u * p1 + 3 * (1 - u) * u * u * p2 + u * u * u;

    private static bool TryNumber(string s, out double v) => double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out v);

    private static string Format(double v) => v.ToString("0.######", CultureInfo.InvariantCulture);

    private sealed class State(GuiElement owner, List<GuiClip> clips)
    {
        public GuiElement Owner { get; } = owner;
        public List<GuiClip> Available { get; } = clips.Where(c => c.Pattern == (int)owner.Float("StatePattern", 0)).ToList() is { Count: > 0 } set
            ? set
            : clips.Where(c => c.Pattern == 0).ToList();
        public GuiClip? Current { get; private set; }
        public double Frame { get; set; }
        public bool Playing { get; set; }
        public string? RequestedByParent { get; set; }

        public GuiClip? Find(string name) => Available.FirstOrDefault(c => c.Name == name);

        public void Switch(GuiClip? clip)
        {
            Current = clip;
            Frame = 0;
        }
    }
}
