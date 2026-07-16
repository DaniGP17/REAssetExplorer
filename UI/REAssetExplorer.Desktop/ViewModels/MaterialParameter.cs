using Avalonia.Media;
using Avalonia.Media.Immutable;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed partial class ParameterComponent(MaterialParameter owner, string label, IBrush? axisBrush, double value,
                                               bool showLabel = false)
    : ObservableObject
{
    private bool silent;

    public string Label { get; } = label;
    public IBrush? AxisBrush { get; } = axisBrush;
    public bool HasAxis => AxisBrush != null;
    public bool ShowLabel { get; } = showLabel;

    [ObservableProperty]
    private double value = value;

    internal void Assign(double newValue)
    {
        silent = true;
        Value = newValue;
        silent = false;
    }

    partial void OnValueChanged(double value)
    {
        if (!silent) owner.Push();
    }
}

public sealed partial class MaterialParameter : ObservableObject
{
    private static readonly IBrush[] AxisBrushes =
    [
        new ImmutableSolidColorBrush(Color.Parse("#C0504D")),
        new ImmutableSolidColorBrush(Color.Parse("#6BA84F")),
        new ImmutableSolidColorBrush(Color.Parse("#4F81BD")),
        new ImmutableSolidColorBrush(Color.Parse("#A0A0A0"))
    ];

    private readonly float[] original;
    private readonly ParameterEdits edits;

    public MaterialParameter(string name, string key, float[] original, ParameterEdits edits, string componentNames = "")
    {
        Key = key;
        this.original = original;
        this.edits = edits;
        float[] current = edits.TryGet(key, out float[] edited) ? edited : original;
        // Names are the only hint the mdf2 gives (AmbientColor, BloodColor...).
        IsColor = original.Length >= 3 && name.Contains("color", StringComparison.OrdinalIgnoreCase);
        string[] named = componentNames.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        bool axes = named.Length == 0 || named[0] is "X" or "R";
        string[] labels = named.Length == original.Length ? named : IsColor ? ["R", "G", "B", "A"] : ["X", "Y", "Z", "W"];
        bool vector = original.Length > 1;
        Components = current.Select((v, i) => new ParameterComponent(
            this, vector ? labels[i] : string.Empty, vector && axes ? AxisBrushes[i] : null, v, vector && !axes)).ToList();
        IsEdited = edits.TryGet(key, out _);
    }

    public string Key { get; }
    public bool IsColor { get; }
    public IReadOnlyList<ParameterComponent> Components { get; }

    [ObservableProperty]
    private bool isEdited;

    public Color SwatchColor
    {
        get
        {
            byte Channel(int i, double fallback) =>
                (byte)Math.Round(Math.Clamp(i < Components.Count ? Components[i].Value : fallback, 0, 1) * 255);
            return Color.FromArgb(Channel(3, 1), Channel(0, 0), Channel(1, 0), Channel(2, 0));
        }
    }

    public IBrush SwatchBrush => new ImmutableSolidColorBrush(Color.FromRgb(SwatchColor.R, SwatchColor.G, SwatchColor.B));

    public void SetColor(double r, double g, double b, double a)
    {
        double[] channels = [r, g, b, a];
        for (int i = 0; i < Components.Count && i < 4; i++) Components[i].Assign(channels[i]);
        Push();
    }

    [RelayCommand]
    private void Reset()
    {
        for (int i = 0; i < Components.Count; i++) Components[i].Assign(original[i]);
        edits.Reset(Key, original);
        IsEdited = false;
        OnPropertyChanged(nameof(SwatchColor));
        OnPropertyChanged(nameof(SwatchBrush));
    }

    // Shows values set elsewhere (the viewport's gizmo) without sending them back.
    public void Reflect(float[] current)
    {
        for (int i = 0; i < Components.Count && i < current.Length; i++) Components[i].Assign(current[i]);
        IsEdited = !current.SequenceEqual(original);
    }

    internal void Push()
    {
        float[] current = Components.Select(c => (float)c.Value).ToArray();
        if (current.SequenceEqual(original))
        {
            edits.Reset(Key, original);
            IsEdited = false;
        }
        else
        {
            edits.Set(Key, current);
            IsEdited = true;
        }
        OnPropertyChanged(nameof(SwatchColor));
        OnPropertyChanged(nameof(SwatchBrush));
    }
}
