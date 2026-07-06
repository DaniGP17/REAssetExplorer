using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using Avalonia.Data.Converters;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

// Values match rae_viewport_set_shading.
public enum ViewportShading
{
    Lit,
    Unlit,
    Wireframe,
    Collision,
    AiMap,
    Normals,
    Roughness,
    Metallic,
    Occlusion,
    BaseColor,
    Emissive,
    Depth,
    Velocity,
    LightingOnly,
    LodColoration,
    LightComplexity,
    Overdraw
}

public sealed record SnapStep(string Label, float Value)
{
    public override string ToString() => Label;
}

// Outlives the panels that show it: docking reparents the host control, and
// recreating the native viewport would drop the device and the loaded asset.
public sealed partial class ViewportViewModel : ObservableObject
{
    private static readonly ViewportShading[] SceneShadingModes = Enum.GetValues<ViewportShading>();
    private static readonly ViewportShading[] AssetShadingModes =
    [
        ViewportShading.Lit, ViewportShading.Unlit, ViewportShading.Wireframe, ViewportShading.LightingOnly,
        ViewportShading.BaseColor, ViewportShading.Normals, ViewportShading.Roughness, ViewportShading.Metallic,
        ViewportShading.Occlusion, ViewportShading.Emissive, ViewportShading.Depth, ViewportShading.Velocity,
        ViewportShading.LodColoration, ViewportShading.Overdraw
    ];

    private readonly HashSet<string> shownCollisionGroups = ["Terrain"];
    private readonly Dictionary<string, bool> aiMapChoices = [];
    private object? owner;
    private uint selectionSerial;

    // Must run on the UI thread: the window belongs to the thread that creates it.
    public ViewportViewModel()
    {
        Display = new ViewportDisplayViewModel(this);
        if (!OperatingSystem.IsWindows()) return;
        Handle = NativeMethods.rae_viewport_create(IntPtr.Zero);
        if (Handle == IntPtr.Zero)
        {
            NativeLog.Write(LogLevel.Error, "Viewport creation failed: " + NativeMethods.LastError());
            return;
        }
        NativeMethods.rae_viewport_set_shading(Handle, (int)Shading);
        Display.Apply();
        ApplySnap();
    }

    public IntPtr Handle { get; private set; }

    public ViewportDisplayViewModel Display { get; }

    public AssetKind? Kind { get; private set; }

    // CharacterAssembly.PartsText (null loads the mesh alone); EditedPart indexes it.
    public string? CharacterParts { get; set; }
    public int EditedPart { get; set; }

    [ObservableProperty]
    private bool is3D = true;

    public event Action<string, string>? Picked;

    public event Action? HideRequested;

    // A Transform parameter key (param:xform:...) and the values the gizmo gave it.
    public event Action<string, float[]>? TransformEdited;

    public static FuncValueConverter<ViewportShading, string> ShadingName { get; } = new(DisplayName);

    public static string DisplayName(ViewportShading mode) => mode switch
    {
        ViewportShading.AiMap => "AI Map",
        ViewportShading.BaseColor => "Base Color",
        ViewportShading.LightingOnly => "Lighting Only",
        ViewportShading.LodColoration => "LOD Coloration",
        ViewportShading.LightComplexity => "Light Complexity",
        _ => mode.ToString()
    };

    [ObservableProperty]
    private IReadOnlyList<ViewportShading> shadingModes = SceneShadingModes;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsCollisionView))]
    [NotifyPropertyChangedFor(nameof(IsAiMapView))]
    private ViewportShading shading = ViewportShading.Unlit;

    public bool IsCollisionView => Shading == ViewportShading.Collision;

    public bool IsAiMapView => Shading == ViewportShading.AiMap;

    public ObservableCollection<ViewportFilterItem> CollisionGroups { get; } = [];

    // Native override key (LIGHT_STATE_OVERRIDE) of the light state to load.
    public const string LightStateOverride = "lightstate";

    // Light environment states (morning, day...) of the scene; choosing one reloads it.
    public ObservableCollection<string> LightStates { get; } = [];

    public bool HasLightStates => LightStates.Count > 0;

    [ObservableProperty]
    private string? lightState;

    private bool refreshingLightStates;

    public event Action<string>? LightStateChosen;

    partial void OnLightStateChanged(string? value)
    {
        if (!refreshingLightStates && value != null) LightStateChosen?.Invoke(value);
    }

    public ObservableCollection<ViewportFilterItem> AiMapGroups { get; } = [];

    public bool HasAiMapGroups => AiMapGroups.Count > 0;

    public string AiMapFilterText => FilterText(AiMapGroups, "No AI maps");

    public bool HasCollisionGroups => CollisionGroups.Count > 0;

    public string CollisionFilterText => FilterText(CollisionGroups, "No colliders");

    private static string FilterText(IReadOnlyCollection<ViewportFilterItem> items, string empty)
    {
        if (items.Count == 0) return empty;
        List<string> shown = items.Where(g => g.IsShown).Select(g => g.Name).ToList();
        if (shown.Count == 0) return "None";
        if (shown.Count == items.Count) return "All";
        return shown.Count <= 2 ? string.Join(", ", shown) : $"{shown.Count} of {items.Count}";
    }

    [ObservableProperty]
    private bool showGrid = true;

    [ObservableProperty]
    private bool showGizmos = true;

    [ObservableProperty]
    private bool showPointLights = true;

    [ObservableProperty]
    private bool showSpotLights = true;

    [ObservableProperty]
    private bool showDirectionalLights = true;

    [ObservableProperty]
    private bool showSelectionOutline = true;

    [ObservableProperty]
    private bool isSceneKind;

    // 0 move, 1 rotate, 2 scale, as rae_viewport_set_transform_tool.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsMoveTool), nameof(IsRotateTool), nameof(IsScaleTool))]
    private int transformMode;

    [ObservableProperty]
    private bool isLocalSpace;

    private bool syncingTool;

    public bool IsMoveTool
    {
        get => TransformMode == 0;
        set => ChooseTool(0);
    }

    public bool IsRotateTool
    {
        get => TransformMode == 1;
        set => ChooseTool(1);
    }

    public bool IsScaleTool
    {
        get => TransformMode == 2;
        set => ChooseTool(2);
    }

    // A second click on the active tool keeps it checked.
    private void ChooseTool(int mode)
    {
        TransformMode = mode;
        OnPropertyChanged(nameof(IsMoveTool));
        OnPropertyChanged(nameof(IsRotateTool));
        OnPropertyChanged(nameof(IsScaleTool));
    }

    [RelayCommand]
    private void ToggleSpace() => IsLocalSpace = !IsLocalSpace;

    public static IReadOnlyList<SnapStep> MoveSteps { get; } =
    [
        new("1 cm", 0.01f), new("5 cm", 0.05f), new("10 cm", 0.1f), new("50 cm", 0.5f), new("1 m", 1f), new("5 m", 5f), new("10 m", 10f)
    ];

    public static IReadOnlyList<SnapStep> RotateSteps { get; } =
    [
        new("1°", 1f), new("5°", 5f), new("10°", 10f), new("15°", 15f), new("30°", 30f), new("45°", 45f), new("90°", 90f)
    ];

    public static IReadOnlyList<SnapStep> ScaleSteps { get; } =
    [
        new("0.0625", 0.0625f), new("0.125", 0.125f), new("0.25", 0.25f), new("0.5", 0.5f), new("1", 1f)
    ];

    // Unreal's defaults: move and rotate snapping on.
    [ObservableProperty]
    private bool snapMove = true;

    [ObservableProperty]
    private bool snapRotate = true;

    [ObservableProperty]
    private bool snapScale;

    [ObservableProperty]
    private SnapStep moveStep = MoveSteps[2];

    [ObservableProperty]
    private SnapStep rotateStep = RotateSteps[2];

    [ObservableProperty]
    private SnapStep scaleStep = ScaleSteps[2];

    partial void OnTransformModeChanged(int value) => ApplyTool();

    partial void OnIsLocalSpaceChanged(bool value) => ApplyTool();

    private void ApplyTool()
    {
        if (!syncingTool && Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_transform_tool(Handle, TransformMode, IsLocalSpace ? 1 : 0);
    }

    partial void OnSnapMoveChanged(bool value) => ApplySnap();

    partial void OnSnapRotateChanged(bool value) => ApplySnap();

    partial void OnSnapScaleChanged(bool value) => ApplySnap();

    partial void OnMoveStepChanged(SnapStep value) => ApplySnap();

    partial void OnRotateStepChanged(SnapStep value) => ApplySnap();

    partial void OnScaleStepChanged(SnapStep value) => ApplySnap();

    private void ApplySnap()
    {
        if (Handle == IntPtr.Zero) return;
        int flags = (SnapMove ? 1 : 0) | (SnapRotate ? 2 : 0) | (SnapScale ? 4 : 0);
        NativeMethods.rae_viewport_set_snap(Handle, flags, MoveStep.Value, RotateStep.Value, ScaleStep.Value);
    }

    [ObservableProperty]
    private bool showSkeletons;

    [ObservableProperty]
    private bool showStats;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(FrameCommand))]
    private bool hasScene;

    [ObservableProperty]
    private bool isLoading;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasMessage))]
    private string message = string.Empty;

    public bool HasMessage => Message.Length > 0;

    // A stale host (docking already moved the viewport) must not detach it.
    public IntPtr Attach(object host, IntPtr parentHwnd)
    {
        owner = host;
        NativeMethods.rae_viewport_set_parent(Handle, parentHwnd);
        return NativeMethods.rae_viewport_hwnd(Handle);
    }

    public void Detach(object host)
    {
        if (Handle == IntPtr.Zero || owner != host) return;
        owner = null;
        NativeMethods.rae_viewport_set_parent(Handle, IntPtr.Zero);
    }

    // Call before the host window is destroyed: Windows destroys child windows with their parent.
    public void Park()
    {
        owner = null;
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_parent(Handle, IntPtr.Zero);
    }

    // A reload (keepCamera) keeps showing the previous frame instead of the placeholder.
    public async Task<bool> LoadAsync(AssetNode node, IntPtr gameHandle, string overrides = "", bool keepCamera = false)
    {
        if (Handle == IntPtr.Zero) return false;
        IsLoading = true;
        Message = string.Empty;
        Kind = node.Kind;
        Is3D = node.Kind is not (AssetKind.Texture or AssetKind.UvSequence or AssetKind.RenderTarget or AssetKind.Movie);
        bool isScene = node.Kind is AssetKind.Scene or AssetKind.Prefab;
        IsSceneKind = isScene;
        if (!isScene && Shading is ViewportShading.Collision or ViewportShading.AiMap) Shading = ViewportShading.Unlit;
        ShadingModes = isScene ? SceneShadingModes : AssetShadingModes;
        Display.SetScopeKind(isScene);
        var clock = Stopwatch.StartNew();

        string path = node.FullPath;
        AssetKind kind = node.Kind;
        IntPtr viewport = Handle;
        string? parts = kind == AssetKind.Mesh ? CharacterParts : null;
        int editedPart = EditedPart;
        (bool ok, string? error) = await Task.Run(() =>
        {
            int keep = keepCamera ? 1 : 0;
            int result = kind switch
            {
                AssetKind.Scene or AssetKind.Prefab => NativeMethods.rae_viewport_load_scene(viewport, gameHandle, path, overrides, keep),
                AssetKind.Material => NativeMethods.rae_viewport_load_material(viewport, gameHandle, path, overrides, keep),
                AssetKind.Effect => NativeMethods.rae_viewport_load_effect(viewport, gameHandle, path, overrides, keep),
                AssetKind.Texture or AssetKind.RenderTarget => NativeMethods.rae_viewport_load_texture(viewport, gameHandle, path, keep),
                AssetKind.Movie => NativeMethods.rae_viewport_load_movie(viewport, gameHandle, path, keep),
                AssetKind.Collision => NativeMethods.rae_viewport_load_collision(viewport, gameHandle, path, keep),
                AssetKind.AiMap => NativeMethods.rae_viewport_load_aimap(viewport, gameHandle, path, keep),
                AssetKind.UvSequence => NativeMethods.rae_viewport_load_uvs(viewport, gameHandle, path, keep),
                _ when parts != null => NativeMethods.rae_viewport_load_character(viewport, gameHandle, parts, editedPart, overrides, keep),
                _ => NativeMethods.rae_viewport_load_mesh(viewport, gameHandle, path, overrides, keep)
            };
            return (result != 0, result != 0 ? null : NativeMethods.LastError());
        });

        IsLoading = false;
        HasScene = ok;
        RefreshCollisionGroups();
        RefreshAiMapGroups();
        RefreshLightStates();
        if (ok) NativeLog.Write(LogLevel.Info, $"Loaded {node.Name} in {clock.Elapsed.TotalSeconds:0.0} s");
        else Message = $"Failed to load {node.Name}: {error}";
        return ok;
    }

    public void SetHiddenObjects(IEnumerable<string> keys)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_hidden_objects(Handle, string.Join('\n', keys));
    }

    public void SetMaterialParam(string key, float[] values)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_material_param(Handle, key, values, values.Length);
    }

    public void Clear()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_clear(Handle);
        Kind = null;
        HasScene = false;
        Message = string.Empty;
        RefreshCollisionGroups();
        RefreshAiMapGroups();
        RefreshLightStates();
    }

    [RelayCommand(CanExecute = nameof(HasScene))]
    private void Frame()
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_reset_camera(Handle);
    }

    [ObservableProperty]
    private string cameraText = string.Empty;

    [ObservableProperty]
    private bool cameraTextInvalid;

    public void RefreshCamera()
    {
        if (Handle == IntPtr.Zero) return;
        CameraText = Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_get_camera(Handle)) ?? string.Empty;
        CameraTextInvalid = false;
    }

    [RelayCommand]
    private void ApplyCamera()
    {
        CameraTextInvalid = Handle == IntPtr.Zero || NativeMethods.rae_viewport_set_camera(Handle, CameraText.Trim()) == 0;
    }

    // Scene folders clear the outline: outlining them would light up whole levels.
    public void ShowSelection(OutlineNode? node, IReadOnlyList<OutlineNode>? selected = null)
    {
        if (Handle == IntPtr.Zero) return;
        if (Kind == AssetKind.Material)
        {
            if (node?.Kind == "material" && int.TryParse(node.Key, out int slot)) NativeMethods.rae_viewport_select(Handle, slot, -1);
        }
        else if (Kind == AssetKind.Mesh)
        {
            bool isJoint = node?.Kind == "joint" && node.Key.StartsWith("joint:", StringComparison.Ordinal);
            int joint = isJoint && int.TryParse(node!.Key.AsSpan(6), out int j) ? j : -1;
            if (joint >= 0) ShowSkeletons = true;
            NativeMethods.rae_viewport_select_joint(Handle, joint);
            string[] key = node?.Kind is "lod" or "submesh" ? node.Key.Split(':') : [];
            int lod = key.Length > 0 && int.TryParse(key[0], out int l) ? l : -1;
            int submesh = key.Length > 1 && int.TryParse(key[1], out int s) ? s : -1;
            NativeMethods.rae_viewport_select(Handle, lod, submesh);
        }
        else if (Kind is AssetKind.Texture or AssetKind.UvSequence or AssetKind.RenderTarget)
        {
            // Texture keys "image:i" / "mip:i:m", atlas keys "seq:s" / "pat:s:p".
            string[] key = node?.Kind is "image" or "mip" or "sequence" or "pattern" ? node.Key.Split(':') : [];
            int first = key.Length > 1 && int.TryParse(key[1], out int a) ? a : -1;
            int second = key.Length > 2 && int.TryParse(key[2], out int b) ? b : -1;
            NativeMethods.rae_viewport_select(Handle, first, second);
        }
        else if (Kind is AssetKind.Scene or AssetKind.Prefab)
        {
            // The active object first: the viewport reports it as the selected one.
            IEnumerable<OutlineNode> nodes = node == null ? selected ?? [] : new[] { node }.Concat(selected ?? []);
            IEnumerable<string> keys = nodes.Where(n => n.Kind == "gameobject")
                .SelectMany(n => n.DescendantsAndSelf().Where(d => d.Kind == "gameobject"))
                .Select(n => n.Key)
                .Distinct();
            NativeMethods.rae_viewport_select_objects(Handle, string.Join('\n', keys));
        }
    }

    public void Tick()
    {
        if (Handle == IntPtr.Zero) return;
        PollSelection();
        if (NativeMethods.rae_viewport_take_hide_request(Handle) != 0) HideRequested?.Invoke();
        if (NativeMethods.rae_viewport_take_gizmo_toggles(Handle) % 2 == 1 && IsSceneKind) ShowGizmos = !ShowGizmos;
        PollTransformTool();
        PollTransformChanges();
        PollViewOptions();
    }

    // W/E/R and Ctrl+` in the viewport change the tool natively.
    private void PollTransformTool()
    {
        NativeMethods.rae_viewport_get_transform_tool(Handle, out int mode, out int local);
        if (mode == TransformMode && (local != 0) == IsLocalSpace) return;
        syncingTool = true;
        TransformMode = mode;
        IsLocalSpace = local != 0;
        syncingTool = false;
    }

    private void PollTransformChanges()
    {
        string text = Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_take_transform_changes(Handle)) ?? string.Empty;
        string[] fields = ["position", "rotation", "scale"];
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] parts = line.Split('\t');
            if (parts.Length < 4) continue;
            for (int i = 0; i < 3; i++)
            {
                float[] values = parts[i + 1].Split(',').Select(v => float.Parse(v, CultureInfo.InvariantCulture)).ToArray();
                if (values.Length == 3) TransformEdited?.Invoke($"param:xform:{parts[0]}#{fields[i]}", values);
            }
        }
    }

    partial void OnShowStatsChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_stats_overlay(Handle, value ? 1 : 0);
    }

    public void Destroy()
    {
        if (Handle == IntPtr.Zero) return;
        NativeMethods.rae_viewport_destroy(Handle);
        Handle = IntPtr.Zero;
        owner = null;
        HasScene = false;
    }

    private void PollSelection()
    {
        uint serial = NativeMethods.rae_viewport_get_selection(Handle, out int lod, out int submesh);
        if (serial == selectionSerial) return;
        selectionSerial = serial;
        if (Kind == AssetKind.Mesh)
        {
            Picked?.Invoke("submesh", submesh < 0 ? string.Empty : $"{lod}:{submesh}");
        }
        else if (Kind == AssetKind.UvSequence)
        {
            if (submesh >= 0) Picked?.Invoke("pattern", $"pat:{lod}:{submesh}");
            else Picked?.Invoke("sequence", lod >= 0 ? $"seq:{lod}" : string.Empty);
        }
        else if (Kind is AssetKind.Scene or AssetKind.Prefab)
        {
            Picked?.Invoke("gameobject",
                           Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_get_selected_object(Handle)) ?? string.Empty);
        }
    }

    private void RefreshLightStates()
    {
        string text = Handle == IntPtr.Zero
            ? string.Empty
            : Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_get_light_states(Handle)) ?? string.Empty;
        string[] lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        refreshingLightStates = true;
        LightStates.Clear();
        foreach (string state in lines.Skip(1)) LightStates.Add(state);
        LightState = lines.Length > 0 ? lines[0] : null;
        refreshingLightStates = false;
        OnPropertyChanged(nameof(HasLightStates));
    }

    private void RefreshCollisionGroups()
    {
        CollisionGroups.Clear();
        string text = Handle == IntPtr.Zero
            ? string.Empty
            : Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_get_collision_groups(Handle)) ?? string.Empty;
        var groups = new List<(string Name, int Colliders, Color Color, bool Volume)>();
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] fields = line.Split('\t');
            if (fields.Length < 4 || !Color.TryParse("#" + fields[2], out Color color)) continue;
            groups.Add((fields[0], int.TryParse(fields[1], out int count) ? count : 0, color, fields[3] == "1"));
        }
        bool anyChosen = groups.Any(g => shownCollisionGroups.Contains(g.Name));
        foreach (var group in groups)
        {
            bool shown = !anyChosen || shownCollisionGroups.Contains(group.Name);
            CollisionGroups.Add(new ViewportFilterItem(group.Name, group.Colliders, group.Color,
                                                       group.Volume ? "Trigger volumes, drawn see-through" : null, shown,
                                                       OnCollisionGroupToggled));
        }
        ApplyCollisionGroups();
        OnPropertyChanged(nameof(HasCollisionGroups));
    }

    private void OnCollisionGroupToggled(ViewportFilterItem item)
    {
        foreach (ViewportFilterItem group in CollisionGroups)
        {
            if (group.IsShown) shownCollisionGroups.Add(group.Name);
            else shownCollisionGroups.Remove(group.Name);
        }
        ApplyCollisionGroups();
    }

    private void ApplyCollisionGroups()
    {
        if (Handle != IntPtr.Zero)
        {
            NativeMethods.rae_viewport_set_collision_groups(
                Handle, string.Join('\n', CollisionGroups.Where(g => g.IsShown).Select(g => g.Name)));
        }
        OnPropertyChanged(nameof(CollisionFilterText));
    }

    // Volume spaces start hidden: their boxes fill whole rooms and hide the navmeshes.
    private void RefreshAiMapGroups()
    {
        AiMapGroups.Clear();
        string text = Handle == IntPtr.Zero
            ? string.Empty
            : Marshal.PtrToStringUTF8(NativeMethods.rae_viewport_get_aimap_groups(Handle)) ?? string.Empty;
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] fields = line.Split('\t');
            if (fields.Length < 4 || !Color.TryParse("#" + fields[3], out Color color)) continue;
            string name = fields[0];
            string type = fields[1];
            int nodes = int.TryParse(fields[2], out int count) ? count : 0;
            bool shown = aiMapChoices.TryGetValue(name, out bool chosen) ? chosen : type != "Volume space";
            AiMapGroups.Add(new ViewportFilterItem(name, nodes, color, $"{type}, {nodes} nodes", shown, OnAiMapToggled));
        }
        ApplyAiMapGroups();
        OnPropertyChanged(nameof(HasAiMapGroups));
    }

    private void OnAiMapToggled(ViewportFilterItem item)
    {
        aiMapChoices[item.Name] = item.IsShown;
        ApplyAiMapGroups();
    }

    private void ApplyAiMapGroups()
    {
        if (Handle != IntPtr.Zero)
        {
            NativeMethods.rae_viewport_set_aimap_groups(
                Handle, string.Join('\n', AiMapGroups.Where(g => g.IsShown).Select(g => g.Name)));
        }
        OnPropertyChanged(nameof(AiMapFilterText));
    }

    partial void OnShowGridChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_grid(Handle, value ? 1 : 0);
    }

    partial void OnShowGizmosChanged(bool value) => ApplyGizmos();

    partial void OnShowPointLightsChanged(bool value) => ApplyGizmos();

    partial void OnShowSpotLightsChanged(bool value) => ApplyGizmos();

    partial void OnShowDirectionalLightsChanged(bool value) => ApplyGizmos();

    partial void OnShowSelectionOutlineChanged(bool value) => ApplyGizmos();

    private void ApplyGizmos()
    {
        if (Handle == IntPtr.Zero) return;
        uint mask = !ShowGizmos ? 0u
            : 0x80000000u | (ShowPointLights ? 1u : 0u) | (ShowSpotLights ? 2u : 0u) | (ShowDirectionalLights ? 4u : 0u) |
              (ShowSelectionOutline ? 8u : 0u);
        NativeMethods.rae_viewport_set_gizmos(Handle, mask);
    }

    partial void OnShowSkeletonsChanged(bool value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_skeletons(Handle, value ? 1 : 0);
    }

    partial void OnShadingChanged(ViewportShading value)
    {
        if (Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_shading(Handle, (int)value);
        OnPropertyChanged(nameof(ShadingLabel));
        foreach (ViewModeGroup group in ViewModeGroups)
        {
            foreach (ViewModeItem item in group.Items) item.Refresh();
        }
    }
}
