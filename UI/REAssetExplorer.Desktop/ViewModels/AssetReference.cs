using Avalonia.Media;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.ViewModels;

public interface IAssetServices
{
    AssetNode? Resolve(string path);
    // element: the name of one material inside a material file.
    Task<Bitmap?> ThumbnailAsync(AssetNode node, string? element = null);
    // In material slot order.
    Task<IReadOnlyList<string>> MaterialNamesAsync(AssetNode mdf);
    IReadOnlyList<AssetNode> AssetsOfKind(AssetKind kind);
    AssetNode? SelectedAsset { get; }
    AssetNode? DefaultMaterial(AssetNode mesh);
    void Open(AssetNode node, string? element = null);
    void Browse(AssetNode node);
    Task<bool> PlayAudioAsync(string container, uint mediaId);
    Task ExportAudioAsync(string container, uint mediaId, string suggestedName);
}

public sealed partial class AssetReference : ObservableObject
{
    private const int MaxPickerItems = 500;

    private readonly IAssetServices assets;
    private readonly AssetOverrides? overrides;
    private readonly string originalPath;
    private Bitmap? thumbnail;
    private bool thumbnailRequested;

    public AssetReference(string path, string overrideKey, IAssetServices assets, AssetOverrides? overrides,
                          string? element = null)
    {
        this.assets = assets;
        this.overrides = overrides;
        originalPath = path;
        OverrideKey = overrideKey;
        Element = element;
        Kind = AssetKinds.FromExtension(Path.GetExtension(AssetTree.SourceKey(path)).TrimStart('.'));
        string current = CanChange && overrides!.TryGet(overrideKey, out string replaced) ? replaced : path;
        SetCurrent(current);
    }

    public string OverrideKey { get; }
    public string? Element { get; }
    public AssetKind Kind { get; }
    public bool CanChange => Element == null && overrides != null && OverrideKey.Length > 0;
    public bool IsOverridden => CanChange && overrides!.Contains(OverrideKey);
    public Geometry? Icon => Icons.Get(AssetKinds.IconKey(Kind));
    public IBrush KindBrush => AssetKinds.Brush(Kind);

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Name))]
    [NotifyPropertyChangedFor(nameof(IsMissing))]
    [NotifyCanExecuteChangedFor(nameof(OpenCommand))]
    [NotifyCanExecuteChangedFor(nameof(BrowseCommand))]
    private AssetNode? node;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Name))]
    private string currentPath = string.Empty;

    public string Name => Element ?? Node?.Name ?? FileName(CurrentPath);
    public bool IsMissing => Node == null;

    // Requested the first time a view binds it, so only rows on screen render previews.
    public Bitmap? Thumbnail
    {
        get
        {
            // A material file holds several materials; only its elements get previews.
            bool preview = Node != null && Native.ThumbnailService.CanPreview(Node.Kind) &&
                           (Node.Kind != AssetKind.Material || Element != null);
            if (!thumbnailRequested && preview)
            {
                thumbnailRequested = true;
                _ = LoadThumbnailAsync(Node!);
            }
            return thumbnail;
        }
    }

    public bool HasThumbnail => thumbnail != null;

    [ObservableProperty]
    private string pickerFilter = string.Empty;

    [ObservableProperty]
    private IReadOnlyList<AssetNode> pickerItems = [];

    [ObservableProperty]
    private string pickerStatus = string.Empty;

    public event Action<AssetNode>? Chosen;
    public event Action? WasReset;

    public void Choose(AssetNode chosen)
    {
        if (!CanChange || chosen.Kind != Kind) return;
        overrides!.Set(OverrideKey, chosen.FullPath);
        SetCurrent(chosen.FullPath);
        Chosen?.Invoke(chosen);
    }

    // Filled only when the popup opens: the candidate list can be tens of thousands long.
    public void RefreshPicker() => OnPickerFilterChanged(PickerFilter);

    [RelayCommand(CanExecute = nameof(HasNode))]
    private void Open() => assets.Open(Node!, Element);

    [RelayCommand(CanExecute = nameof(HasNode))]
    private void Browse() => assets.Browse(Node!);

    [RelayCommand]
    private void UseSelected()
    {
        if (assets.SelectedAsset is { } selected) Choose(selected);
    }

    [RelayCommand]
    private void Reset()
    {
        if (!CanChange) return;
        overrides!.Remove(OverrideKey);
        SetCurrent(originalPath);
        WasReset?.Invoke();
    }

    private bool HasNode() => Node != null;

    private static string FileName(string path)
    {
        string key = AssetTree.SourceKey(path);
        return key[(key.LastIndexOf('/') + 1)..];
    }

    private void SetCurrent(string path)
    {
        CurrentPath = path;
        Node = assets.Resolve(path);
        thumbnail = null;
        thumbnailRequested = false;
        OnPropertyChanged(nameof(Thumbnail));
        OnPropertyChanged(nameof(HasThumbnail));
        OnPropertyChanged(nameof(IsOverridden));
    }

    private async Task LoadThumbnailAsync(AssetNode target)
    {
        Bitmap? bitmap = await assets.ThumbnailAsync(target, Element);
        if (Node != target) return;
        thumbnail = bitmap;
        OnPropertyChanged(nameof(Thumbnail));
        OnPropertyChanged(nameof(HasThumbnail));
    }

    partial void OnPickerFilterChanged(string value)
    {
        string filter = value.Trim();
        IReadOnlyList<AssetNode> candidates = assets.AssetsOfKind(Kind);
        IEnumerable<AssetNode> matches = filter.Length == 0
            ? candidates
            : candidates.Where(n => n.FullPath.Contains(filter, StringComparison.OrdinalIgnoreCase));
        List<AssetNode> shown = matches.Take(MaxPickerItems + 1).ToList();
        PickerStatus = shown.Count > MaxPickerItems ? $"First {MaxPickerItems} matches, refine the search" : $"{shown.Count} assets";
        if (shown.Count > MaxPickerItems) shown.RemoveAt(shown.Count - 1);
        PickerItems = shown;
    }
}
