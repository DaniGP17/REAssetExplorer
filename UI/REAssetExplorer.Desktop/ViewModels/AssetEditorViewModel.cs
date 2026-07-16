using Avalonia.Media;
using Avalonia.Threading;
using Dock.Model.Controls;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.ViewModels;

public abstract class AssetEditorViewModel
{
    private readonly DispatcherTimer timer;
    private Task loadTask = Task.CompletedTask;
    private NativeGame? game;
    private bool reloadQueued;
    private bool reloading;

    protected AssetEditorViewModel(AssetNode asset, IAssetServices assets, bool withViewport = true)
    {
        Asset = asset;
        Viewport = withViewport ? new ViewportViewModel() : null;
        Inspector.Assets = assets;
        Inspector.Overrides = Overrides;
        Inspector.Parameters = Parameters;
        Overrides.Changed += () => _ = ReloadAsync();
        if (Viewport != null) Viewport.LightStateChosen += state => Overrides.Set(ViewportViewModel.LightStateOverride, state);
        Parameters.Changed += (key, values) => Viewport?.SetMaterialParam(key, values);
        Hierarchy.Selected += node =>
        {
            if (node != null) Inspector.ShowOutline(node);
            Viewport?.ShowSelection(node, Hierarchy.SelectedNodes);
        };
        if (Viewport != null)
        {
            Viewport.Picked += (kind, key) => Hierarchy.Select(Hierarchy.Find(kind, key));
            Viewport.HideRequested += Hierarchy.ToggleSelectedVisibility;
            Viewport.TransformEdited += (key, values) =>
            {
                Parameters.Store(key, values);
                Inspector.UpdateParameter(key, values);
            };
        }
        Docking = new AssetEditorDockFactory(this);
        Layout = Docking.CreateLayout();
        Docking.InitLayout(Layout);
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(100), DispatcherPriority.Background, (_, _) =>
        {
            Viewport?.Tick();
            Player?.Tick();
            Playback?.Tick();
            ImageTools?.Tick();
        });
        timer.Start();
    }

    public AssetNode Asset { get; }
    public string Title => Asset.Name;
    public abstract string EditorName { get; }
    public Geometry? Icon => Asset.Icon;
    public IBrush KindBrush => Asset.KindBrush;
    public HierarchyViewModel Hierarchy { get; } = new();
    public InspectorViewModel Inspector { get; } = new();
    public ViewportViewModel? Viewport { get; }
    public virtual AudioPlayerViewModel? Player => null;
    public virtual EffectPlaybackViewModel? Playback => null;
    public virtual ImageToolbarViewModel? ImageTools => null;
    public virtual MoviePlayerViewModel? MovieTools => null;
    public virtual AnimationToolbarViewModel? AnimationTools => null;
    public virtual MessageTableViewModel? MessageTable => null;
    public virtual GuiPreviewViewModel? GuiPreview => null;
    public virtual FsmGraphViewModel? FsmGraph => null;
    protected NativeGame? Game => game;
    public AssetOverrides Overrides { get; } = new();
    public ParameterEdits Parameters { get; } = new();
    public AssetEditorDockFactory Docking { get; }
    public IRootDock Layout { get; }

    // The window must wait for a load in flight before closing.
    public event Action? CloseRequested;
    public event Action<AssetEditorViewModel>? Closed;

    public Task Load(NativeGame source)
    {
        game = source;
        Viewport?.Display.SetGame(source);
        loadTask = LoadAllAsync(source);
        return loadTask;
    }

    public Task WhenIdle() => loadTask;

    public void RequestClose() => CloseRequested?.Invoke();

    public void OnClosed()
    {
        timer.Stop();
        if (Layout.Close.CanExecute(null)) Layout.Close.Execute(null);
        Viewport?.Destroy();
        Player?.Release();
        MovieTools?.Release();
        AnimationTools?.Release();
        Closed?.Invoke(this);
    }

    protected virtual void OnLoaded()
    {
    }

    protected virtual Task LoadContentAsync(NativeGame source) => Task.CompletedTask;

    private async Task LoadAllAsync(NativeGame source)
    {
        await Task.WhenAll(Hierarchy.LoadAsync(source, Asset),
                           Viewport?.LoadAsync(Asset, source.Handle) ?? Task.CompletedTask,
                           LoadContentAsync(source));
        OnLoaded();
    }

    protected Task ReloadViewportAsync() => ReloadAsync();

    // Changes made while a load runs are folded into one reload after it.
    private async Task ReloadAsync()
    {
        reloadQueued = true;
        if (reloading || game == null || Viewport == null) return;
        reloading = true;
        await Task.Yield();
        await loadTask;
        while (reloadQueued)
        {
            reloadQueued = false;
            string overrides = Overrides.ToNativeText() + '\n' + Parameters.ToNativeText();
            loadTask = Viewport.LoadAsync(Asset, game.Handle, overrides, keepCamera: true);
            await loadTask;
        }
        reloading = false;
        Viewport.ShowSelection(Hierarchy.SelectedNode, Hierarchy.SelectedNodes);
    }
}

public sealed class SoundEditorViewModel : AssetEditorViewModel
{
    private readonly AudioPlayerViewModel player;

    public SoundEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets, withViewport: false)
    {
        player = new AudioPlayerViewModel(assets, asset.FullPath);
        Hierarchy.Selected += player.Select;
        Hierarchy.Activated += player.Toggle;
    }

    public override string EditorName => "Sound Bank";
    public override AudioPlayerViewModel Player => player;
}

public sealed class EffectEditorViewModel : AssetEditorViewModel
{
    private readonly EffectPlaybackViewModel playback;

    public EffectEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        playback = new EffectPlaybackViewModel(Viewport!);
        Hierarchy.VisibilityChanged += keys => Viewport!.SetHiddenObjects(keys);
    }

    public override string EditorName => "Effect Editor";
    public override EffectPlaybackViewModel Playback => playback;

    // The preview only draws Billboard3D emitters.
    protected override void OnLoaded()
    {
        OutlineNode? emitter = Hierarchy.Nodes.FirstOrDefault(n => n.Kind == "emitter" && n.Detail.StartsWith("Billboard3D"))
                               ?? Hierarchy.Nodes.FirstOrDefault(n => n.Kind == "emitter");
        if (emitter != null) Hierarchy.Select(emitter);
    }
}

public sealed class TextureEditorViewModel : AssetEditorViewModel
{
    private readonly ImageToolbarViewModel tools;

    public TextureEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        tools = new ImageToolbarViewModel(Viewport!, flipbook: false);
    }

    public override string EditorName => "Texture Editor";
    public override ImageToolbarViewModel ImageTools => tools;
}

public sealed class PrefabEditorViewModel : AssetEditorViewModel
{
    public PrefabEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        Hierarchy.VisibilityChanged += keys => Viewport!.SetHiddenObjects(keys);
    }

    public override string EditorName => "Prefab Editor";
}

public sealed class CollisionEditorViewModel : AssetEditorViewModel
{
    public CollisionEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        Hierarchy.VisibilityChanged += keys => Viewport!.SetHiddenObjects(keys);
    }

    public override string EditorName => "Collision Mesh";
}

public sealed class AiMapEditorViewModel : AssetEditorViewModel
{
    public AiMapEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        Hierarchy.VisibilityChanged += keys => Viewport!.SetHiddenObjects(keys);
    }

    public override string EditorName => "AI Map";
}

public sealed class StateMachineEditorViewModel : AssetEditorViewModel
{
    private readonly FsmGraphViewModel graph = new();
    private bool fromGraph;

    public StateMachineEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets, withViewport: false)
    {
        Hierarchy.Selected += node =>
        {
            if (node != null && !fromGraph) graph.Show(node.Key);
        };
        graph.Picked += node =>
        {
            fromGraph = true;
            try
            {
                Hierarchy.Select(Hierarchy.Nodes.FirstOrDefault(n => n.Key == node.Key));
            }
            finally
            {
                fromGraph = false;
            }
        };
    }

    public override string EditorName => "State Machine";
    public override FsmGraphViewModel FsmGraph => graph;

    protected override async Task LoadContentAsync(NativeGame source)
    {
        string path = Asset.FullPath;
        string? text = await Task.Run(() => source.FsmGraph(path));
        if (text == null)
        {
            NativeLog.Write(LogLevel.Warning, $"{Asset.Name}: graph not read: {NativeMethods.LastError()}");
            return;
        }
        graph.Load(Models.FsmGraph.Parse(text));
    }

    protected override void OnLoaded()
    {
        if (Hierarchy.Nodes.FirstOrDefault() is { } root) Hierarchy.Select(root);
    }
}

public sealed class MotionBankEditorViewModel(AssetNode asset, IAssetServices assets)
    : AssetEditorViewModel(asset, assets, withViewport: false)
{
    public override string EditorName => "Motion Bank";

    protected override void OnLoaded()
    {
        if (Hierarchy.Nodes.FirstOrDefault() is { } root) Hierarchy.Select(root);
    }
}

public sealed class GuiEditorViewModel : AssetEditorViewModel
{
    private readonly GuiPreviewViewModel preview = new();

    public GuiEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets, withViewport: false)
    {
        Hierarchy.Selected += node =>
        {
            preview.SelectedKey = node?.Kind == "guielement" ? node.Key : null;
            if (node?.Kind != "guielement") return;
            preview.SelectOwnerFor(node.Key);
            ShowElement(node);
        };
        Closed += _ => preview.Stop();
        Hierarchy.VisibilityChanged += keys => preview.SetHidden(keys);
        preview.Picked += key => Hierarchy.Select(Hierarchy.Find("guielement", key));
    }

    public override string EditorName => "GUI Editor";
    public override GuiPreviewViewModel GuiPreview => preview;

    private void ShowElement(OutlineNode node)
    {
        List<OutlineProperty> clipValues = preview.ClipValues(node.Key).ToList();
        if (clipValues.Count == 0) return;
        var shown = new OutlineNode(node.Kind, node.Name, node.Detail, node.Key, node.Depth, null);
        shown.Properties.AddRange(clipValues);
        shown.Properties.AddRange(node.Properties);
        Inspector.ShowOutline(shown);
    }

    protected override async void OnLoaded()
    {
        if (Game == null) return;
        NativeGame game = Game;
        string clips = await Task.Run(() => game.GuiClips(Asset.FullPath) ?? string.Empty);
        preview.Load(game, Hierarchy.Nodes.FirstOrDefault(), clips);
    }
}

public sealed class MessageEditorViewModel : AssetEditorViewModel
{
    private readonly MessageTableViewModel messages = new();

    public MessageEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets, withViewport: false)
    {
        messages.Selected += item =>
        {
            if (item == null) ShowFile();
            else Inspector.ShowOutline(EntryNode(item));
        };
    }

    public override string EditorName => "Message Table";
    public override MessageTableViewModel MessageTable => messages;

    protected override Task LoadContentAsync(NativeGame source) => messages.LoadAsync(source, Asset);

    protected override void OnLoaded() => ShowFile();

    private void ShowFile()
    {
        if (Hierarchy.Nodes.FirstOrDefault() is { } root) Inspector.ShowOutline(root);
    }

    private OutlineNode EntryNode(MessageItem item)
    {
        var node = new OutlineNode("message", item.Key, string.Empty, string.Empty, 0, null);
        void Add(string section, string key, string value) =>
            node.Properties.Add(new OutlineProperty(section, key, value, string.Empty, string.Empty));
        Add("Message", "Key", item.Key);
        Add("Message", "GUID", item.Guid);
        Add("Message", "Hash", item.Hash);
        MessageTable table = messages.Table;
        for (int i = 0; i < item.Attributes.Length && i < table.Attributes.Count; i++)
        {
            string name = table.Attributes[i].Name;
            Add("Attributes", name.Length > 0 ? name : $"Attribute {i}", item.Attributes[i]);
        }
        foreach (MessageLanguage language in table.Languages)
        {
            Add("Text", language.Name, language.Index < item.Texts.Length ? item.Texts[language.Index] : string.Empty);
        }
        return node;
    }
}

public sealed class UserDataEditorViewModel(AssetNode asset, IAssetServices assets)
    : AssetEditorViewModel(asset, assets, withViewport: false)
{
    public override string EditorName => "User Data";

    protected override void OnLoaded()
    {
        OutlineNode? root = Hierarchy.Nodes.FirstOrDefault(n => n.Kind == "userdata");
        if (root != null) Hierarchy.Select(root);
    }
}

public sealed class MovieEditorViewModel : AssetEditorViewModel
{
    private readonly MoviePlayerViewModel player;

    public MovieEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        player = new MoviePlayerViewModel(Viewport!);
    }

    public override string EditorName => "Movie Player";
    public override MoviePlayerViewModel MovieTools => player;
}

public sealed class UvSequenceEditorViewModel : AssetEditorViewModel
{
    private readonly ImageToolbarViewModel tools;

    public UvSequenceEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        tools = new ImageToolbarViewModel(Viewport!, flipbook: true);
    }

    public override string EditorName => "UV Sequence Editor";
    public override ImageToolbarViewModel ImageTools => tools;

    protected override void OnLoaded()
    {
        OutlineNode? sequence = Hierarchy.Nodes.FirstOrDefault(n => n.Kind == "sequence");
        if (sequence != null) Hierarchy.Select(sequence);
    }
}

public sealed class MeshEditorViewModel : AssetEditorViewModel
{
    private readonly AnimationToolbarViewModel animation;

    public MeshEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        animation = new AnimationToolbarViewModel(Viewport!, assets, asset);
        animation.CharacterChanged += assembly => _ = ShowCharacterAsync(assembly);
        Hierarchy.VisibilityChanged += keys => Viewport!.SetHiddenObjects(keys);
    }

    public override string EditorName => "Mesh Editor";
    public override AnimationToolbarViewModel AnimationTools => animation;

    protected override void OnLoaded()
    {
        if (Game != null) animation.Attach(Game);
        animation.IsAvailable = Hierarchy.Nodes.Any(n => n.Kind == "joint");
        if (animation.IsAvailable) _ = animation.LoadCharactersAsync();
    }

    private async Task ShowCharacterAsync(CharacterAssembly? assembly)
    {
        int edited = assembly?.IndexOf(Asset.FullPath) ?? -1;
        Viewport!.CharacterParts = assembly?.PartsText;
        Viewport.EditedPart = Math.Max(edited, 0);
        Hierarchy.SetExtraRoots(assembly == null ? [] : [CharacterNode(assembly, edited)]);
        await ReloadViewportAsync();
        Viewport.SetHiddenObjects(Hierarchy.HiddenKeys());
        animation.Replay();
    }

    private static OutlineNode CharacterNode(CharacterAssembly assembly, int edited)
    {
        var root = new OutlineNode("character", assembly.Name, $"{assembly.Parts.Count} meshes", string.Empty, 0, null);
        void Add(OutlineNode node, string section, string key, string value)
        {
            if (value.Length > 0) node.Properties.Add(new OutlineProperty(section, key, value, string.Empty, string.Empty));
        }
        Add(root, "Character", "Name", assembly.Name);
        Add(root, "Character", "Found in", assembly.Source);
        Add(root, "Character", "Motion bank", assembly.Motbank);
        Add(root, "Character", "Meshes", assembly.Parts.Count.ToString());
        for (int i = 0; i < assembly.Parts.Count; i++)
        {
            CharacterPart part = assembly.Parts[i];
            string detail = i == edited ? "this mesh" : Path.GetFileName(part.Mesh);
            var node = new OutlineNode("charpart", part.Name, detail, $"part:{i}", 1, root);
            Add(node, "Part", "Mesh", part.Mesh);
            Add(node, "Part", "Material", part.Material);
            if (part.Parent >= 0 && part.Parent < assembly.Parts.Count)
            {
                string parent = assembly.Parts[part.Parent].Name;
                Add(node, "Part", part.ParentJoint.Length > 0 ? "Rides joint" : "Follows joints of",
                    part.ParentJoint.Length > 0 ? $"{part.ParentJoint} of {parent}" : parent);
            }
            Add(node, "Part", "Own motion bank", part.Motbank);
            root.Children.Add(node);
        }
        return root;
    }
}

public sealed class MaterialEditorViewModel : AssetEditorViewModel
{
    private bool loaded;
    private string? pendingFocus;

    public MaterialEditorViewModel(AssetNode asset, IAssetServices assets) : base(asset, assets)
    {
        // The sphere is centered on the grid plane, which would cut through it.
        Viewport!.ShowGrid = false;
        Viewport.Shading = ViewportShading.Lit;
    }

    public override string EditorName => "Material Editor";

    public void FocusMaterial(string name)
    {
        if (!loaded)
        {
            pendingFocus = name;
            return;
        }
        OutlineNode? material = Hierarchy.Nodes.FirstOrDefault(n => n.Kind == "material" && n.Name == name);
        if (material != null) Hierarchy.Select(material);
    }

    // mdf2 files often begin with a placeholder material on systems/rendering/Null*.
    protected override void OnLoaded()
    {
        loaded = true;
        if (pendingFocus != null)
        {
            FocusMaterial(pendingFocus);
            pendingFocus = null;
            return;
        }
        List<OutlineNode> materials = Hierarchy.Nodes.Where(n => n.Kind == "material").ToList();
        OutlineNode? first = materials.FirstOrDefault(HasRealTextures) ?? materials.FirstOrDefault();
        if (first != null) Hierarchy.Select(first);
    }

    private static bool HasRealTextures(OutlineNode material) =>
        material.Properties.Any(p => p.Section == "Textures" && p.Value.Length > 0 &&
                                     !p.Value.StartsWith("systems/rendering/", StringComparison.OrdinalIgnoreCase));
}
