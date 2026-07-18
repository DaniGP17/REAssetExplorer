using System.Globalization;
using System.Text.RegularExpressions;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record MotionItem(int Index, string Name, string Detail)
{
    public override string ToString() => string.IsNullOrEmpty(Detail) ? Name : $"{Name}   ({Detail})";
}

public sealed partial class AnimationToolbarViewModel : ObservableObject
{
    private const int MaxPickerItems = 500;
    // Character ids in paths: RE7 "em3000" / "pl0000", RE8 "ch05_1000".
    private static readonly Regex CharacterId = new(@"ch\d{2}_\d{4}|[a-z]{2}\d{4}", RegexOptions.IgnoreCase);

    private readonly ViewportViewModel viewport;
    private readonly IAssetServices assets;
    private readonly AssetNode mesh;
    private readonly DispatcherTimer timer = new(DispatcherPriority.Normal) { Interval = TimeSpan.FromMilliseconds(33) };
    private NativeGame? game;
    private bool pickerPrepared;
    private bool partPickerPrepared;
    private List<AssetNode> bankMotlists = [];
    private string bankName = string.Empty;

    public AnimationToolbarViewModel(ViewportViewModel viewport, IAssetServices assets, AssetNode mesh)
    {
        this.viewport = viewport;
        this.assets = assets;
        this.mesh = mesh;
        timer.Tick += (_, _) => Tick();
        timer.Start();
    }

    public IReadOnlyList<PlaybackSpeed> Speeds { get; } = [new(0.1), new(0.25), new(0.5), new(1), new(2)];

    [ObservableProperty]
    private IReadOnlyList<CharacterOption> characters = [];

    [ObservableProperty]
    private CharacterOption? character;

    [ObservableProperty]
    private string characterStatus = "Finding characters...";

    public event Action<CharacterAssembly?>? CharacterChanged;

    [ObservableProperty]
    private string partFilter = string.Empty;

    [ObservableProperty]
    private IReadOnlyList<AssetNode> partItems = [];

    [ObservableProperty]
    private string partStatus = string.Empty;

    [ObservableProperty]
    private bool isAvailable;

    [ObservableProperty]
    private AssetNode? motlist;

    [ObservableProperty]
    private string pickerFilter = string.Empty;

    [ObservableProperty]
    private IReadOnlyList<AssetNode> pickerItems = [];

    [ObservableProperty]
    private string pickerStatus = string.Empty;

    [ObservableProperty]
    private IReadOnlyList<MotionItem> motions = [];

    [ObservableProperty]
    private MotionItem? selectedMotion;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsPlaying))]
    private bool isPaused;

    [ObservableProperty]
    private bool isPlaybackActive;

    [ObservableProperty]
    private bool loop = true;

    // Mirrors the viewport: flying the camera there turns it off.
    [ObservableProperty]
    private bool followCharacter = true;
    private bool syncingFollow;

    [ObservableProperty]
    private PlaybackSpeed speed = new(1);

    [ObservableProperty]
    private double position;

    [ObservableProperty]
    private double duration;

    [ObservableProperty]
    private string statusText = string.Empty;

    public bool IsPlaying => !IsPaused;
    public string MotlistName => Motlist?.Name ?? "Choose a motion list...";

    public void Attach(NativeGame source) => game = source;

    // Only a body (part 0) opens with its parts; any other mesh opens alone.
    public async Task LoadCharactersAsync()
    {
        if (game is not { } current) return;
        string path = mesh.FullPath;
        string? text = await Task.Run(() => current.CharacterAssemblies(path));
        List<CharacterAssembly> found = text == null ? [] : CharacterAssembly.Parse(text);
        var options = new List<CharacterOption> { new("This mesh only", null, mesh.FullPath) };
        foreach (CharacterAssembly assembly in found)
        {
            options.Add(new CharacterOption($"{assembly.Name}  ({assembly.Parts.Count} meshes)", assembly,
                                            $"{assembly.Source}\nUsed by {assembly.Uses} scene(s) or prefab(s)"));
        }
        Characters = options;
        CharacterStatus = found.Count == 0 ? "No character uses this mesh" : $"{found.Count} character(s)";
        Character = options.Skip(1).FirstOrDefault(o => o.Assembly!.IndexOf(path) == 0) ?? options[0];
    }

    partial void OnCharacterChanged(CharacterOption? value)
    {
        CharacterChanged?.Invoke(value?.Assembly);
        _ = LoadBankAsync(value?.Assembly?.Motbank ?? string.Empty);
    }

    private async Task LoadBankAsync(string motbank)
    {
        bankMotlists = [];
        bankName = string.Empty;
        if (motbank.Length > 0 && game is { } current)
        {
            IReadOnlyList<string> paths = await Task.Run(() => current.MotbankMotlists(motbank));
            bankMotlists = paths.Select(assets.Resolve).OfType<AssetNode>().ToList();
            bankName = Path.GetFileName(motbank);
        }
        if (PickerFilter.Length == 0 || bankMotlists.Count > 0)
        {
            // An id filter set before the bank was known gives way to the bank.
            if (bankMotlists.Count > 0 && PickerFilter.Length > 0 && !pickerTouched) PickerFilter = string.Empty;
            else OnPickerFilterChanged(PickerFilter);
        }
    }

    private bool pickerTouched;

    // Added parts follow the first part's joints.
    public void AddPart(AssetNode node)
    {
        CharacterAssembly? from = Character?.Assembly;
        var custom = new CharacterAssembly("Custom", string.Empty, from?.Motbank ?? string.Empty, 1);
        if (from != null) custom.Parts.AddRange(from.Parts);
        else custom.Parts.Add(CharacterPart.Root(mesh.FullPath));
        if (custom.IndexOf(node.FullPath) < 0) custom.Parts.Add(CharacterPart.Added(node.FullPath));
        var option = new CharacterOption($"Custom  ({custom.Parts.Count} meshes)", custom, "Put together by hand");
        Characters = Characters.Where(o => o.Assembly?.Name != "Custom" || o.Assembly.Source.Length > 0).Append(option).ToList();
        Character = option;
    }

    public void PreparePartPicker()
    {
        if (!partPickerPrepared)
        {
            partPickerPrepared = true;
            string? id = CharacterId.Matches(mesh.Name).Select(m => m.Value).FirstOrDefault();
            if (id != null && PartFilter.Length == 0)
            {
                PartFilter = id;
                return;
            }
        }
        OnPartFilterChanged(PartFilter);
    }

    partial void OnPartFilterChanged(string value)
    {
        string filter = value.Trim();
        IReadOnlyList<AssetNode> candidates = assets.AssetsOfKind(AssetKind.Mesh);
        IEnumerable<AssetNode> matches = filter.Length == 0
            ? candidates
            : candidates.Where(n => n.FullPath.Contains(filter, StringComparison.OrdinalIgnoreCase));
        List<AssetNode> shown = matches.Where(n => !string.Equals(n.FullPath, mesh.FullPath, StringComparison.OrdinalIgnoreCase))
            .Take(MaxPickerItems + 1).ToList();
        PartStatus = shown.Count > MaxPickerItems ? $"First {MaxPickerItems} matches, refine the search" : $"{shown.Count} meshes";
        if (shown.Count > MaxPickerItems) shown.RemoveAt(shown.Count - 1);
        PartItems = shown;
    }

    public void Replay()
    {
        if (SelectedMotion is { } motion) OnSelectedMotionChanged(motion);
    }

    public void PreparePicker()
    {
        if (!pickerPrepared)
        {
            if (bankMotlists.Count > 0)
            {
                pickerPrepared = true;
                OnPickerFilterChanged(PickerFilter);
                return;
            }
            pickerPrepared = true;
            string? id = CharacterFilter();
            if (id != null && PickerFilter.Length == 0)
            {
                PickerFilter = id;
                return;
            }
        }
        OnPickerFilterChanged(PickerFilter);
    }

    private string? CharacterFilter()
    {
        IReadOnlyList<AssetNode> candidates = assets.AssetsOfKind(AssetKind.MotionList);
        IEnumerable<string> ids = CharacterId.Matches(mesh.Name).Concat(CharacterId.Matches(mesh.FullPath)).Select(m => m.Value);
        return ids.Distinct(StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault(id => candidates.Any(n => n.FullPath.Contains(id, StringComparison.OrdinalIgnoreCase)));
    }

    partial void OnPickerFilterChanged(string value)
    {
        string filter = value.Trim();
        if (pickerPrepared && filter.Length > 0) pickerTouched = true;
        if (filter.Length == 0 && bankMotlists.Count > 0)
        {
            PickerItems = bankMotlists;
            PickerStatus = $"{bankMotlists.Count} motion lists of {bankName} (search to list them all)";
            return;
        }
        IReadOnlyList<AssetNode> candidates = assets.AssetsOfKind(AssetKind.MotionList);
        IEnumerable<AssetNode> matches = filter.Length == 0
            ? candidates
            : candidates.Where(n => n.FullPath.Contains(filter, StringComparison.OrdinalIgnoreCase));
        List<AssetNode> shown = matches.Take(MaxPickerItems + 1).ToList();
        PickerStatus = shown.Count > MaxPickerItems ? $"First {MaxPickerItems} matches, refine the search" : $"{shown.Count} motion lists";
        if (shown.Count > MaxPickerItems) shown.RemoveAt(shown.Count - 1);
        PickerItems = shown;
    }

    public async Task ChooseAsync(AssetNode node)
    {
        if (game is not { } current) return;
        Motlist = node;
        OnPropertyChanged(nameof(MotlistName));
        Motions = [];
        SelectedMotion = null;
        try
        {
            string path = node.FullPath;
            string text = await Task.Run(() => current.Outline(path));
            List<OutlineNode> roots = OutlineNode.Parse(text);
            Motions = roots.SelectMany(r => r.DescendantsAndSelf()).Where(n => n.Kind == "motion")
                .Select((n, i) => new MotionItem(i, n.Name, n.Detail)).ToList();
        }
        catch (Exception e)
        {
            NativeLog.Write(LogLevel.Warning, $"{node.Name}: {e.Message}");
        }
        SelectedMotion = Motions.FirstOrDefault();
    }

    partial void OnSelectedMotionChanged(MotionItem? value)
    {
        if (value == null || Motlist == null || game == null || viewport.Handle == IntPtr.Zero) return;
        IntPtr handle = viewport.Handle;
        IntPtr gameHandle = game.Handle;
        string path = Motlist.FullPath;
        int index = value.Index;
        _ = Task.Run(() =>
        {
            if (NativeMethods.rae_viewport_play_motion(handle, gameHandle, path, index) == 0)
            {
                NativeLog.Write(LogLevel.Warning, $"Motion {index} of {path}: {NativeMethods.LastError()}");
            }
        });
        // A new motion plays with the bar's current settings.
        NativeMethods.rae_viewport_motion_set_loop(handle, Loop ? 1 : 0);
        NativeMethods.rae_viewport_motion_set_speed(handle, (float)Speed.Value);
    }

    partial void OnFollowCharacterChanged(bool value)
    {
        if (!syncingFollow && viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_set_follow(viewport.Handle, value ? 1 : 0);
    }

    partial void OnLoopChanged(bool value)
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_motion_set_loop(viewport.Handle, value ? 1 : 0);
    }

    partial void OnSpeedChanged(PlaybackSpeed value)
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_motion_set_speed(viewport.Handle, (float)value.Value);
    }

    [RelayCommand]
    private void TogglePlay()
    {
        if (viewport.Handle != IntPtr.Zero) NativeMethods.rae_viewport_motion_set_paused(viewport.Handle, IsPaused ? 0 : 1);
    }

    // An empty path and index -1 go back to the bind pose.
    [RelayCommand]
    private void Stop()
    {
        SelectedMotion = null;
        if (viewport.Handle != IntPtr.Zero && game != null)
        {
            NativeMethods.rae_viewport_play_motion(viewport.Handle, game.Handle, string.Empty, -1);
        }
    }

    // Native seek takes frames.
    [RelayCommand]
    private void Seek(double seconds)
    {
        if (viewport.Handle == IntPtr.Zero ||
            NativeMethods.rae_viewport_motion_status(viewport.Handle, out _, out _, out float rate, out _, out _) == 0)
        {
            return;
        }
        NativeMethods.rae_viewport_motion_seek(viewport.Handle, (float)(seconds * rate));
    }

    public void Tick()
    {
        if (viewport.Handle != IntPtr.Zero)
        {
            syncingFollow = true;
            FollowCharacter = NativeMethods.rae_viewport_get_follow(viewport.Handle) != 0;
            syncingFollow = false;
        }
        if (viewport.Handle == IntPtr.Zero ||
            NativeMethods.rae_viewport_motion_status(viewport.Handle, out float frame, out float count, out float rate,
                                                     out int paused, out int driven) == 0)
        {
            IsPlaybackActive = false;
            StatusText = string.Empty;
            return;
        }
        IsPlaybackActive = true;
        IsPaused = paused != 0;
        double fps = rate > 0 ? rate : 60;
        Position = frame / fps;
        Duration = count / fps;
        string text = string.Create(CultureInfo.InvariantCulture, $"frame {frame:0} / {count:0}   {fps:0} fps");
        StatusText = driven == 0 ? text + "   (no joint of this skeleton moves)" : text;
    }

    public void Release() => timer.Stop();
}
