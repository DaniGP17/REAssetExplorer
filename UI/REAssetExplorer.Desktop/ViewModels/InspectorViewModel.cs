using System.Globalization;
using Avalonia.Collections;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed class InspectorRow
{
    public InspectorRow(string key, MaterialParameter parameter)
    {
        Key = key;
        Value = string.Empty;
        Parameter = parameter;
    }

    public InspectorRow(string key, string value, AssetReference? asset = null)
    {
        Key = key;
        Value = value;
        Asset = asset;
        if (asset != null) return;
        if (value is "true" or "false")
        {
            IsBool = true;
            BoolValue = value == "true";
            return;
        }
        string[] parts = value.Split(',', StringSplitOptions.TrimEntries);
        if (parts.Length is >= 2 and <= 4 && parts.All(p => double.TryParse(p, NumberStyles.Float, CultureInfo.InvariantCulture, out _)))
        {
            IsVector = true;
            Parts = parts;
        }
    }

    public string Key { get; }
    public string Value { get; }
    public bool IsBool { get; }
    public bool BoolValue { get; }
    public bool IsVector { get; }
    public IReadOnlyList<string> Parts { get; } = [];
    public AssetReference? Asset { get; }
    public MaterialParameter? Parameter { get; }
    public bool IsParameter => Parameter != null;
    public InspectorRow? ElementOf { get; init; }
    public bool IsLastElement { get; init; }
    public bool IsElement => ElementOf != null;
    public bool IsReference => Asset != null && !IsElement;
    public bool IsText => !IsBool && !IsVector && Asset == null && Parameter == null && !IsLongText;
    public bool IsLongText => !IsBool && !IsVector && Asset == null && Parameter == null &&
                              (Value.Contains('\n') || Value.Length > LongTextLength);

    private const int LongTextLength = 60;
}

public sealed partial class InspectorSection(string title, IReadOnlyList<InspectorRow> rows) : ObservableObject
{
    public string Title { get; } = title;
    public IReadOnlyList<InspectorRow> Rows { get; } = rows;

    [ObservableProperty]
    private bool isExpanded = true;
}

public sealed partial class InspectorViewModel : ObservableObject
{
    private List<(string Title, List<InspectorRow> Rows)> source = [];
    // Bumped by every Show; async work for an older target is dropped.
    private int generation;

    public AvaloniaList<InspectorSection> Sections { get; } = [];

    [ObservableProperty]
    private string title = string.Empty;

    [ObservableProperty]
    private string subtitle = string.Empty;

    [ObservableProperty]
    private Geometry? icon;

    [ObservableProperty]
    private IBrush? iconBrush;

    [ObservableProperty]
    private string filterText = string.Empty;

    [ObservableProperty]
    private bool hasTarget;

    public IAssetServices? Assets { get; set; }
    public AssetOverrides? Overrides { get; set; }
    public ParameterEdits? Parameters { get; set; }

    public void ShowAsset(AssetNode node)
    {
        List<InspectorRow> rows = node.IsFolder
            ? [new("Files", node.FileCount.ToString("N0")), new("Path", node.FullPath)]
            : [new("Size", node.SizeText), new("Version", node.Version ?? "-"), CreateRow("File", node.FullPath)];
        Show(node.Name, node.KindLabel, node.Icon, node.KindBrush, [(node.IsFolder ? "Folder" : "File", rows)]);
        ListMaterialElements();
    }

    public void ShowOutline(OutlineNode node)
    {
        var sections = new List<(string, List<InspectorRow>)>();
        foreach (OutlineProperty prop in node.Properties)
        {
            int index = sections.FindIndex(s => s.Item1 == prop.Section);
            if (index < 0)
            {
                sections.Add((prop.Section, []));
                index = sections.Count - 1;
            }
            sections[index].Item2.Add(CreateRow(prop.Key, prop.Value, prop.OverrideKey, prop.Components));
        }
        PairMeshesWithMaterials(sections.SelectMany(s => s.Item2));
        Show(node.Name, node.KindLabel, node.Icon, node.IconBrush, sections);
        ListMaterialElements();
    }

    public void Clear() => Show(string.Empty, string.Empty, null, null, []);

    public void UpdateParameter(string key, float[] values)
    {
        foreach ((string _, List<InspectorRow> rows) in source)
        {
            foreach (InspectorRow row in rows)
            {
                if (row.Parameter?.Key == key) row.Parameter.Reflect(values);
            }
        }
    }

    // In a mesh component's override keys, |v2 is the mesh and |v3 its material file.
    private void PairMeshesWithMaterials(IEnumerable<InspectorRow> rows)
    {
        List<AssetReference> editable = rows.Select(r => r.Asset).OfType<AssetReference>().Where(a => a.CanChange).ToList();
        foreach (AssetReference mesh in editable.Where(a => a.OverrideKey.EndsWith("|v2", StringComparison.Ordinal)))
        {
            string materialKey = mesh.OverrideKey[..^2] + "v3";
            AssetReference? material = editable.FirstOrDefault(a => a.OverrideKey == materialKey);
            if (material == null) continue;
            mesh.Chosen += node =>
            {
                if (Assets?.DefaultMaterial(node) is { } mdf) material.Choose(mdf);
            };
            mesh.WasReset += () => material.ResetCommand.Execute(null);
        }
    }

    private InspectorRow CreateRow(string key, string value, string overrideKey = "", string components = "")
    {
        if (Parameters != null && overrideKey.StartsWith("param:", StringComparison.Ordinal) && ParseFloats(value) is { } floats)
        {
            return new InspectorRow(key, new MaterialParameter(key, overrideKey, floats, Parameters, components));
        }
        return Assets != null && AssetTree.IsAssetPath(value)
            ? new InspectorRow(key, value, new AssetReference(value, overrideKey, Assets, Overrides))
            : new InspectorRow(key, value);
    }

    private static float[]? ParseFloats(string value)
    {
        string[] parts = value.Split(',', StringSplitOptions.TrimEntries);
        var floats = new float[parts.Length];
        for (int i = 0; i < parts.Length; i++)
        {
            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out floats[i])) return null;
        }
        return floats.Length is >= 1 and <= 4 ? floats : null;
    }

    private void Show(string name, string kind, Geometry? geometry, IBrush? brush, List<(string, List<InspectorRow>)> sections)
    {
        generation++;
        Title = name;
        Subtitle = kind;
        Icon = geometry;
        IconBrush = brush;
        HasTarget = !string.IsNullOrEmpty(name);
        source = sections;
        Rebuild();
    }

    partial void OnFilterTextChanged(string value) => Rebuild();

    private void ListMaterialElements()
    {
        foreach ((string _, List<InspectorRow> rows) in source)
        {
            foreach (InspectorRow row in rows.ToList())
            {
                if (row.Asset is not { Kind: AssetKind.Material, Element: null } file) continue;
                _ = ShowElementsAsync(rows, row);
                file.Chosen += chosen => _ = ShowElementsAsync(rows, row);
                file.WasReset += () => _ = ShowElementsAsync(rows, row);
            }
        }
    }

    private async Task ShowElementsAsync(List<InspectorRow> rows, InspectorRow fileRow)
    {
        int current = generation;
        AssetReference file = fileRow.Asset!;
        AssetNode? listed = file.Node;
        IReadOnlyList<string> names = listed != null && Assets != null ? await Assets.MaterialNamesAsync(listed) : [];
        if (current != generation || file.Node != listed) return;
        rows.RemoveAll(r => r.ElementOf == fileRow);
        if (file.Node is { } mdf && Assets != null)
        {
            rows.InsertRange(rows.IndexOf(fileRow) + 1, names.Select((name, i) =>
                new InspectorRow($"Element {i}", name, new AssetReference(mdf.FullPath, "", Assets, null, name))
                {
                    ElementOf = fileRow,
                    IsLastElement = i == names.Count - 1
                }));
        }
        Rebuild();
    }

    private void Rebuild()
    {
        string filter = FilterText.Trim();
        Dictionary<string, bool> expanded = Sections.GroupBy(s => s.Title).ToDictionary(g => g.Key, g => g.First().IsExpanded);
        Sections.Clear();
        foreach ((string sectionTitle, List<InspectorRow> rows) in source)
        {
            IReadOnlyList<InspectorRow> visible = filter.Length == 0 || sectionTitle.Contains(filter, StringComparison.OrdinalIgnoreCase)
                ? rows.ToList()
                : rows.Where(r => r.Key.Contains(filter, StringComparison.OrdinalIgnoreCase)).ToList();
            if (visible.Count == 0) continue;
            Sections.Add(new InspectorSection(sectionTitle, visible) { IsExpanded = expanded.GetValueOrDefault(sectionTitle, true) });
        }
    }
}
