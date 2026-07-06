using System.Runtime.InteropServices;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record SkyOption(string Path, string Name);

// Starts from the last settings used in any viewport and saves every change as the new default.
public sealed partial class ViewportDisplayViewModel : ObservableObject
{
    private const string BackgroundColorKey = "background";
    private const string GradientTopKey = "gradientTop";
    private const string GradientBottomKey = "gradientBottom";
    private const string BoneColorKey = "bone";
    private const string SelectedBoneColorKey = "selectedBone";

    private static readonly ViewportDisplaySettings Defaults = new();
    private static readonly string[] MeshScopes = ["All", "Selected joint", "Selected joint and parents", "Selected joint and children"];
    private static readonly string[] SceneScopes = ["All", "Selected objects"];
    private static DispatcherTimer? saveTimer;

    private readonly ViewportViewModel viewport;
    private readonly ViewportDisplaySettings saved = AppSettings.Current.Viewport;
    private readonly ParameterEdits colorEdits = new();
    private NativeGame? game;
    private Task skyTask = Task.CompletedTask;
    private bool applying;

    public ViewportDisplayViewModel(ViewportViewModel viewport)
    {
        this.viewport = viewport;
        background = Math.Clamp(saved.Background, 0, BackgroundModes.Count - 1);
        skyIntensity = saved.SkyIntensity;
        skyRotation = saved.SkyRotation;
        skyBlur = saved.SkyBlur;
        bones = Math.Clamp(saved.Bones, 0, BoneStyles.Count - 1);
        joints = Math.Clamp(saved.Joints, 0, JointStyles.Count - 1);
        jointAxes = saved.JointAxes;
        occlusion = Math.Clamp(saved.Occlusion, 0, OcclusionModes.Count - 1);
        scope = Math.Clamp(saved.Scope, 0, MeshScopes.Length - 1);
        boneColors = Math.Clamp(saved.BoneColors, 0, BoneColorModes.Count - 1);
        boneSize = saved.BoneSize;
        boneOpacity = saved.BoneOpacity;
        BackgroundColor = ColorSetting(BackgroundColorKey, Defaults.BackgroundColor, saved.BackgroundColor);
        GradientTop = ColorSetting(GradientTopKey, Defaults.GradientTop, saved.GradientTop);
        GradientBottom = ColorSetting(GradientBottomKey, Defaults.GradientBottom, saved.GradientBottom);
        BoneColor = ColorSetting(BoneColorKey, Defaults.BoneColor, saved.BoneColor);
        SelectedBoneColor = ColorSetting(SelectedBoneColorKey, Defaults.SelectedBoneColor, saved.SelectedBoneColor);
        colorEdits.Changed += OnColorChanged;
    }

    public static IReadOnlyList<string> BackgroundModes { get; } = ["Sky", "Solid color", "Gradient"];
    public static IReadOnlyList<string> BoneStyles { get; } = ["None", "Octahedral", "Solid", "Stick"];
    public static IReadOnlyList<string> JointStyles { get; } = ["None", "Sphere", "Solid sphere", "Cross"];
    public static IReadOnlyList<string> OcclusionModes { get; } = ["Draw on top", "Dim when hidden", "Hide when hidden"];
    public static IReadOnlyList<string> BoneColorModes { get; } = ["Uniform", "By side (L / R)", "By depth", "By skeleton"];

    public MaterialParameter BackgroundColor { get; }
    public MaterialParameter GradientTop { get; }
    public MaterialParameter GradientBottom { get; }
    public MaterialParameter BoneColor { get; }
    public MaterialParameter SelectedBoneColor { get; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsSky), nameof(IsSolidColor), nameof(IsGradient))]
    private int background;

    public bool IsSky => Background == 0;
    public bool IsSolidColor => Background == 1;
    public bool IsGradient => Background == 2;

    [ObservableProperty]
    private IReadOnlyList<SkyOption> skies = [];

    [ObservableProperty]
    private SkyOption? sky;

    [ObservableProperty]
    private double skyIntensity;

    [ObservableProperty]
    private double skyRotation;

    [ObservableProperty]
    private double skyBlur;

    [ObservableProperty]
    private int bones;

    [ObservableProperty]
    private int joints;

    [ObservableProperty]
    private bool jointAxes;

    [ObservableProperty]
    private int occlusion;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ScopeIndex))]
    private int scope;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ScopeIndex))]
    private IReadOnlyList<string> scopes = MeshScopes;

    // The ComboBox writes -1 when its items change; scenes offer fewer scopes.
    public int ScopeIndex
    {
        get => Math.Min(Scope, Scopes.Count - 1);
        set
        {
            if (value >= 0) Scope = value;
        }
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsUniformBoneColor))]
    private int boneColors;

    public bool IsUniformBoneColor => BoneColors == 0;

    [ObservableProperty]
    private double boneSize;

    [ObservableProperty]
    private double boneOpacity;

    public void SetGame(NativeGame opened)
    {
        game = opened;
        string text = Marshal.PtrToStringUTF8(NativeMethods.rae_game_skies(opened.Handle)) ?? string.Empty;
        string[] paths = text.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        Dictionary<string, int> names = paths.GroupBy(SkyName).ToDictionary(g => g.Key, g => g.Count());
        var options = paths.Select(path =>
        {
            string name = SkyName(path);
            if (names[name] == 1) return new SkyOption(path, name);
            string[] folders = path.Split('/');
            return new SkyOption(path, $"{name} ({folders[^2]})");
        }).ToList();
        applying = true;
        Skies = options;
        Sky = options.FirstOrDefault(o => saved.Skies.TryGetValue(opened.Id, out string? chosen) && o.Path == chosen) ??
              options.FirstOrDefault();
        applying = false;
        LoadSky();
    }

    public void SetScopeKind(bool scene)
    {
        Scopes = scene ? SceneScopes : MeshScopes;
        PushSkeletonStyle();
    }

    public void Apply()
    {
        PushBackground();
        PushSkeletonStyle();
    }

    [RelayCommand]
    private void ResetBackground()
    {
        applying = true;
        Background = Defaults.Background;
        SkyIntensity = Defaults.SkyIntensity;
        SkyRotation = Defaults.SkyRotation;
        SkyBlur = Defaults.SkyBlur;
        ResetColor(BackgroundColor);
        ResetColor(GradientTop);
        ResetColor(GradientBottom);
        Sky = Skies.FirstOrDefault();
        applying = false;
        if (game != null) saved.Skies.Remove(game.Id);
        SaveBackground();
        LoadSky();
        PushBackground();
    }

    [RelayCommand]
    private void ResetSkeleton()
    {
        applying = true;
        Bones = Defaults.Bones;
        Joints = Defaults.Joints;
        JointAxes = Defaults.JointAxes;
        Occlusion = Defaults.Occlusion;
        Scope = Defaults.Scope;
        BoneColors = Defaults.BoneColors;
        BoneSize = Defaults.BoneSize;
        BoneOpacity = Defaults.BoneOpacity;
        ResetColor(BoneColor);
        ResetColor(SelectedBoneColor);
        applying = false;
        SaveSkeleton();
        PushSkeletonStyle();
    }

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (applying) return;
        switch (e.PropertyName)
        {
            case nameof(Background) or nameof(SkyIntensity) or nameof(SkyRotation) or nameof(SkyBlur):
                SaveBackground();
                PushBackground();
                break;
            case nameof(Sky):
                if (game != null)
                {
                    if (Sky == null || Sky == Skies.FirstOrDefault()) saved.Skies.Remove(game.Id);
                    else saved.Skies[game.Id] = Sky.Path;
                    ScheduleSave();
                }
                LoadSky();
                break;
            case nameof(Bones) or nameof(Joints) or nameof(JointAxes) or nameof(Occlusion) or nameof(Scope)
                or nameof(BoneColors) or nameof(BoneSize) or nameof(BoneOpacity):
                SaveSkeleton();
                PushSkeletonStyle();
                break;
        }
    }

    partial void OnSkyIntensityChanged(double value)
    {
        if (value is < 0 or > 8) SkyIntensity = Math.Clamp(value, 0, 8);
    }

    partial void OnSkyRotationChanged(double value)
    {
        if (value is < 0 or > 360) SkyRotation = Math.Clamp(value, 0, 360);
    }

    partial void OnSkyBlurChanged(double value)
    {
        if (value is < 0 or > 1) SkyBlur = Math.Clamp(value, 0, 1);
    }

    partial void OnBoneSizeChanged(double value)
    {
        if (value is < 0.1 or > 10) BoneSize = Math.Clamp(value, 0.1, 10);
    }

    partial void OnBoneOpacityChanged(double value)
    {
        if (value is < 0.05 or > 1) BoneOpacity = Math.Clamp(value, 0.05, 1);
    }

    private static string SkyName(string path)
    {
        string file = path[(path.LastIndexOf('/') + 1)..];
        int tex = file.IndexOf(".tex", StringComparison.OrdinalIgnoreCase);
        return tex > 0 ? file[..tex] : file;
    }

    private MaterialParameter ColorSetting(string key, float[] defaults, float[] current)
    {
        if (current.Length == defaults.Length && !current.SequenceEqual(defaults)) colorEdits.Set(key, current);
        return new MaterialParameter(key + " color", key, defaults, colorEdits);
    }

    private static void ResetColor(MaterialParameter color)
    {
        if (color.IsEdited) color.ResetCommand.Execute(null);
    }

    private static float[] Values(MaterialParameter color) => color.Components.Select(c => (float)c.Value).ToArray();

    private void OnColorChanged(string key, float[] values)
    {
        switch (key)
        {
            case BackgroundColorKey:
                saved.BackgroundColor = values;
                break;
            case GradientTopKey:
                saved.GradientTop = values;
                break;
            case GradientBottomKey:
                saved.GradientBottom = values;
                break;
            case BoneColorKey:
                saved.BoneColor = values;
                break;
            case SelectedBoneColorKey:
                saved.SelectedBoneColor = values;
                break;
        }
        if (applying) return;
        ScheduleSave();
        if (key is BoneColorKey or SelectedBoneColorKey) PushSkeletonStyle();
        else PushBackground();
    }

    private void SaveBackground()
    {
        saved.Background = Background;
        saved.SkyIntensity = SkyIntensity;
        saved.SkyRotation = SkyRotation;
        saved.SkyBlur = SkyBlur;
        ScheduleSave();
    }

    private void SaveSkeleton()
    {
        saved.Bones = Bones;
        saved.Joints = Joints;
        saved.JointAxes = JointAxes;
        saved.Occlusion = Occlusion;
        saved.Scope = Scope;
        saved.BoneColors = BoneColors;
        saved.BoneSize = BoneSize;
        saved.BoneOpacity = BoneOpacity;
        ScheduleSave();
    }

    private static void ScheduleSave()
    {
        saveTimer ??= new DispatcherTimer(TimeSpan.FromMilliseconds(500), DispatcherPriority.Background, (_, _) =>
        {
            saveTimer!.Stop();
            AppSettings.Current.Save();
        });
        saveTimer.Stop();
        saveTimer.Start();
    }

    // Chained so a slow decode cannot land after a later choice.
    private void LoadSky()
    {
        IntPtr handle = viewport.Handle;
        if (handle == IntPtr.Zero || game == null) return;
        IntPtr gameHandle = game.Handle;
        string? path = Sky?.Path;
        skyTask = skyTask.ContinueWith(_ =>
        {
            if (NativeMethods.rae_viewport_set_sky(handle, gameHandle, path) == 0)
            {
                NativeLog.Write(LogLevel.Warning, "Sky not loaded: " + NativeMethods.LastError());
            }
        }, TaskScheduler.Default);
    }

    private unsafe void PushBackground()
    {
        if (viewport.Handle == IntPtr.Zero) return;
        float[] top = Background == 2 ? Values(GradientTop) : Values(BackgroundColor);
        float[] bottom = Values(GradientBottom);
        var native = new ViewportBackground
        {
            Mode = Background,
            SkyIntensity = (float)SkyIntensity,
            SkyRotation = (float)SkyRotation,
            SkyBlur = (float)SkyBlur
        };
        for (int i = 0; i < 3; i++)
        {
            native.Color[i] = top[i];
            native.Bottom[i] = bottom[i];
        }
        NativeMethods.rae_viewport_set_background(viewport.Handle, in native);
    }

    private void PushSkeletonStyle()
    {
        if (viewport.Handle == IntPtr.Zero) return;
        var native = new SkeletonStyle
        {
            Bones = Bones,
            Joints = Joints,
            Axes = JointAxes ? 1 : 0,
            Occlusion = Occlusion,
            Scope = ScopeIndex,
            Colors = BoneColors,
            Color = PackRgba(Values(BoneColor)),
            SelectedColor = PackRgba(Values(SelectedBoneColor)),
            Size = (float)BoneSize,
            Opacity = (float)BoneOpacity
        };
        NativeMethods.rae_viewport_set_skeleton_style(viewport.Handle, in native);
    }

    private static uint PackRgba(float[] rgba)
    {
        uint Channel(int i) => (uint)Math.Round(Math.Clamp(i < rgba.Length ? rgba[i] : 1, 0, 1) * 255);
        return Channel(0) | Channel(1) << 8 | Channel(2) << 16 | Channel(3) << 24;
    }
}
