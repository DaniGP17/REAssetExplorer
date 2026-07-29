using System.Diagnostics;
using System.Globalization;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record GuiClipOwner(GuiElement Element, string Label)
{
    public override string ToString() => Label;
}

public sealed partial class GuiPreviewViewModel : ObservableObject
{
    private readonly DispatcherTimer timer;
    private readonly Stopwatch clock = new();
    private HashSet<string> hiddenKeys = [];
    // Set while the timer writes ClipFrame, so the change is not taken as a seek.
    private bool updatingFrame;

    public GuiPreviewViewModel()
    {
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(15), DispatcherPriority.Render, (_, _) => Tick());
    }

    public GuiScene? Scene { get; private set; }
    public GuiImageCache? Images { get; private set; }
    public GameFonts? Fonts { get; private set; }
    public GuiAnimator? Animator { get; private set; }

    [ObservableProperty]
    private bool animate = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PlayTip))]
    private bool isPlaying;

    [ObservableProperty]
    private bool loopClips;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasClips))]
    private IReadOnlyList<GuiClipOwner> clipOwners = [];

    [ObservableProperty]
    private GuiClipOwner? selectedOwner;

    [ObservableProperty]
    private IReadOnlyList<string> ownerClips = [];

    [ObservableProperty]
    private string? selectedClip;

    [ObservableProperty]
    private double clipFrame;

    [ObservableProperty]
    private double clipLength = 1;

    [ObservableProperty]
    private string clipStatus = string.Empty;

    public bool HasClips => ClipOwners.Count > 0;
    public string PlayTip => IsPlaying ? "Pause all clips" : "Resume all clips";
    // Back to front.
    public List<GuiDrawItem> Items { get; private set; } = [];

    [ObservableProperty]
    private bool showHidden;

    [ObservableProperty]
    private bool showBounds;

    [ObservableProperty]
    private bool showHitAreas;

    [ObservableProperty]
    private bool checkerBackground = true;

    [ObservableProperty]
    private string? selectedKey;

    [ObservableProperty]
    private string zoomText = string.Empty;

    [ObservableProperty]
    private string statusText = string.Empty;

    public event Action? Invalidated;
    public event Action? FitRequested;
    public event Action<string>? Picked;

    public void Load(NativeGame game, OutlineNode? fileNode, string clips)
    {
        Images = new GuiImageCache(game);
        Images.Changed += () => Invalidated?.Invoke();
        Fonts = GameFonts.For(game);
        if (!Fonts.IsLoaded)
        {
            Fonts.Loaded += () => Invalidated?.Invoke();
            _ = Fonts.LoadAsync();
        }
        Scene = fileNode?.Kind == "gui" ? new GuiScene(fileNode) : null;
        Animator = Scene != null ? new GuiAnimator(Scene, GuiClipTable.Parse(clips)) { Loop = LoopClips } : null;
        ClipOwners = Animator?.Owners.Select(e => new GuiClipOwner(e, PathOf(e))).ToList() ?? [];
        SelectedOwner = ClipOwners.FirstOrDefault();
        if (!Animate) ClearAnimation();
        Relayout();
        IsPlaying = Animator != null;
        StatusText = Scene == null
            ? "Not loaded"
            : $"{Scene.ScreenSize.Width:0}x{Scene.ScreenSize.Height:0}  {Scene.Elements.Count() - 1} elements";
        FitRequested?.Invoke();
    }

    public void SetHidden(IEnumerable<string> keys)
    {
        hiddenKeys = [.. keys];
        Relayout();
    }

    public void SelectOwnerFor(string? key)
    {
        GuiElement? element = Scene?.Elements.FirstOrDefault(e => e.Key == key);
        for (GuiElement? e = element; e != null; e = e.Parent)
        {
            if (ClipOwners.FirstOrDefault(o => o.Element == e) is { } owner)
            {
                SelectedOwner = owner;
                return;
            }
        }
    }

    public IEnumerable<OutlineProperty> ClipValues(string? key)
    {
        GuiElement? element = Scene?.Elements.FirstOrDefault(e => e.Key == key);
        if (element == null || !Animate) yield break;
        foreach ((string attribute, string value, string source) in element.AnimatedValues().OrderBy(v => v.Attribute))
        {
            yield return new OutlineProperty("Clip Values", $"{attribute}  ({source})", value, string.Empty, string.Empty);
        }
    }

    public void Stop()
    {
        timer.Stop();
        IsPlaying = false;
    }

    public void Pick(string? key)
    {
        SelectedKey = key;
        Picked?.Invoke(key ?? string.Empty);
    }

    [RelayCommand]
    private void Fit() => FitRequested?.Invoke();

    [RelayCommand]
    private void PlayClip()
    {
        if (Animator == null || SelectedOwner == null || SelectedClip == null) return;
        Animate = true;
        Animator.Play(SelectedOwner.Element, SelectedClip);
        IsPlaying = true;
        Relayout();
    }

    [RelayCommand]
    private void Restart()
    {
        if (Animator == null) return;
        Animator.Reset();
        IsPlaying = true;
        Relayout();
    }

    partial void OnIsPlayingChanged(bool value)
    {
        if (value && Animator != null)
        {
            clock.Restart();
            timer.Start();
        }
        else
        {
            timer.Stop();
        }
    }

    partial void OnLoopClipsChanged(bool value)
    {
        if (Animator != null) Animator.Loop = value;
    }

    partial void OnAnimateChanged(bool value)
    {
        if (Animator == null) return;
        if (value) Animator.Apply();
        else ClearAnimation();
        Relayout();
    }

    partial void OnSelectedOwnerChanged(GuiClipOwner? value)
    {
        OwnerClips = value != null && Animator != null ? Animator.ClipsOf(value.Element).Select(c => c.Name).Distinct().ToList() : [];
        SelectedClip = value != null ? Animator?.CurrentClip(value.Element)?.Name ?? OwnerClips.FirstOrDefault() : null;
        UpdateClipStatus();
    }

    partial void OnClipFrameChanged(double value)
    {
        if (updatingFrame || Animator == null || SelectedOwner == null) return;
        IsPlaying = false;
        Animate = true;
        Animator.Seek(SelectedOwner.Element, value);
        UpdateClipStatus();
        Relayout();
    }

    private void Tick()
    {
        if (Animator == null) return;
        double seconds = Math.Min(clock.Elapsed.TotalSeconds, 0.1);
        clock.Restart();
        if (Animate)
        {
            Animator.Advance(seconds);
            Relayout();
        }
        UpdateClipStatus();
        if (!Animator.IsAnimating) IsPlaying = false;
    }

    private void UpdateClipStatus()
    {
        GuiClip? clip = SelectedOwner != null ? Animator?.CurrentClip(SelectedOwner.Element) : null;
        if (clip == null || SelectedOwner == null)
        {
            ClipStatus = string.Empty;
            return;
        }
        double frame = Animator!.CurrentFrame(SelectedOwner.Element);
        updatingFrame = true;
        ClipLength = Math.Max(clip.Frames, 1);
        ClipFrame = Math.Min(frame, ClipLength);
        updatingFrame = false;
        ClipStatus = clip.IsStatic
            ? $"{clip.Name}  static"
            : string.Create(CultureInfo.InvariantCulture, $"{clip.Name}  {frame:0}/{clip.Frames:0}");
    }

    private void ClearAnimation()
    {
        if (Scene == null) return;
        foreach (GuiElement element in Scene.Elements) element.ClearAnimated();
    }

    private static string PathOf(GuiElement element)
    {
        var names = new List<string>();
        for (GuiElement? e = element; e != null; e = e.Parent) names.Add(e.Node.Name);
        names.Reverse();
        return string.Join("/", names);
    }

    partial void OnShowHiddenChanged(bool value) => Relayout();

    partial void OnShowBoundsChanged(bool value) => Invalidated?.Invoke();

    partial void OnShowHitAreasChanged(bool value) => Invalidated?.Invoke();

    partial void OnCheckerBackgroundChanged(bool value) => Invalidated?.Invoke();

    partial void OnSelectedKeyChanged(string? value) => Invalidated?.Invoke();

    private void Relayout()
    {
        Items = Scene?.Layout(hiddenKeys, ShowHidden) ?? [];
        Invalidated?.Invoke();
    }
}
