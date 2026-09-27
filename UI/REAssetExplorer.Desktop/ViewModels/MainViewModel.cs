using System.Globalization;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;
using REAssetExplorer.Desktop.ViewModels.Docking;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record GameOption(string Id, string Name);

public interface IShellServices
{
    Task<string?> PickSaveFileAsync(string suggestedName);
    Task<string?> PickFolderAsync(string title, string? start);
    Task CopyTextAsync(string text);
    void ShowAssetEditor(AssetEditorViewModel editor);
    void ActivateMainWindow();
}

public sealed partial class MainViewModel : ObservableObject, IAssetServices
{
    private readonly DispatcherTimer timer;
    private readonly AppSettings settings = AppSettings.Current;
    private GameOption? loadedGame;
    private bool revertingGame;
    private readonly List<AssetEditorViewModel> editors = [];
    private readonly Dictionary<AssetKind, IReadOnlyList<AssetNode>> assetsByKind = [];
    private readonly Dictionary<string, Task<IReadOnlyList<string>>> materialNames = [];
    private NativeGame? game;
    private ThumbnailService? thumbnails;
    private Task loadTask = Task.CompletedTask;
    private string? pendingOpen;
    private AssetNode? openScene;
    private bool reloadQueued;
    private bool reloading;

    public MainViewModel()
    {
        Browser.Activated += node => _ = OpenAssetAsync(node);
        Browser.Selected += node =>
        {
            Inspector.ShowAsset(node);
            OpenSelectedCommand.NotifyCanExecuteChanged();
            ExtractSelectedCommand.NotifyCanExecuteChanged();
            CopyPathCommand.NotifyCanExecuteChanged();
        };
        Hierarchy.Selected += node =>
        {
            if (node != null) Inspector.ShowOutline(node);
            Viewport.ShowSelection(node, Hierarchy.SelectedNodes);
        };
        Viewport.Picked += (kind, key) => Hierarchy.Select(Hierarchy.Find(kind, key));
        Viewport.HideRequested += Hierarchy.ToggleSelectedVisibility;
        Hierarchy.VisibilityChanged += keys => Viewport.SetHiddenObjects(keys);
        Inspector.Assets = this;
        Inspector.Overrides = SceneOverrides;
        Inspector.Parameters = SceneParameters;
        SceneOverrides.Changed += () => _ = ReloadSceneAsync();
        Viewport.LightStateChosen += state => SceneOverrides.Set(ViewportViewModel.LightStateOverride, state);
        SceneParameters.Changed += (key, values) => Viewport.SetMaterialParam(key, values);
        Viewport.SaveScenesRequested += () => SaveEditedScenesCommand.Execute(null);
        Viewport.TransformEdited += (key, values) =>
        {
            SceneParameters.Store(key, values);
            Inspector.UpdateParameter(key, values);
        };
        Docking = new DockFactory(this);
        layout = CreateLayout();
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(100), DispatcherPriority.Background, (_, _) =>
        {
            Log.Drain();
            Viewport.Tick();
        });
        timer.Start();
    }

    public IShellServices? Shell { get; set; }

    public AssetBrowserViewModel Browser { get; } = new();
    public HierarchyViewModel Hierarchy { get; } = new();
    public InspectorViewModel Inspector { get; } = new();
    public LogViewModel Log { get; } = new();
    public ViewportViewModel Viewport { get; } = new();
    public AssetOverrides SceneOverrides { get; } = new();
    public ParameterEdits SceneParameters { get; } = new();
    public DockFactory Docking { get; }

    public AssetNode? SelectedAsset => Browser.SelectedAsset;

    public IReadOnlyList<GameOption> Games { get; } =
    [
        new("re8", "Resident Evil Village"),
        new("re7", "Resident Evil 7")
    ];

    [ObservableProperty]
    private IRootDock? layout;

    [ObservableProperty]
    private GameOption? selectedGame;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(OpenSelectedCommand))]
    [NotifyCanExecuteChangedFor(nameof(ReloadGameCommand))]
    private bool isBusy;

    public void Start(IReadOnlyList<string> args)
    {
        string? gameId = settings.Game;
        string? gameDir = null;
        for (int i = 0; i + 1 < args.Count; i++)
        {
            if (args[i] == "--game") gameId = args[i + 1];
            if (args[i] == "--game-dir") gameDir = args[i + 1];
            if (args[i] == "--open") pendingOpen = args[i + 1];
        }
        if (gameDir != null && gameId != null) settings.GamePaths[gameId] = gameDir;
        if (args.Contains("--stats")) Viewport.ShowStats = true;
        SelectedGame = Games.FirstOrDefault(g => g.Id == gameId) ?? Games[0];
    }

    // Pending loads reference their viewport and the game, so shutdown waits for them.
    public Task WhenIdle() => Task.WhenAll(editors.Select(e => e.WhenIdle()).Append(loadTask));

    public Task CloseEditorsAsync()
    {
        Task idle = Task.WhenAll(editors.Select(e => e.WhenIdle()));
        foreach (AssetEditorViewModel editor in editors.ToList()) editor.RequestClose();
        return idle;
    }

    public void Shutdown()
    {
        NativeMethods.rae_audio_shutdown();
        timer.Stop();
        Viewport.Destroy();
        thumbnails?.Dispose();
        thumbnails = null;
    }

    public AssetNode? Resolve(string path) => Browser.Tree?.Resolve(path);

    public Task<Bitmap?> ThumbnailAsync(AssetNode node, string? element = null) =>
        thumbnails?.GetAsync(node, element) ?? Task.FromResult<Bitmap?>(null);

    public Task<IReadOnlyList<string>> MaterialNamesAsync(AssetNode mdf)
    {
        if (game is not { } current) return Task.FromResult<IReadOnlyList<string>>([]);
        if (!materialNames.TryGetValue(mdf.FullPath, out Task<IReadOnlyList<string>>? names))
        {
            names = Task.Run<IReadOnlyList<string>>(() =>
            {
                try
                {
                    return OutlineNode.Parse(current.Outline(mdf.FullPath))
                        .SelectMany(root => root.Children).Where(n => n.Kind == "material").Select(n => n.Name).ToList();
                }
                catch (InvalidOperationException)
                {
                    return [];
                }
            });
            materialNames[mdf.FullPath] = names;
        }
        return names;
    }

    public IReadOnlyList<AssetNode> AssetsOfKind(AssetKind kind)
    {
        if (Browser.Tree is not { } tree) return [];
        if (!assetsByKind.TryGetValue(kind, out IReadOnlyList<AssetNode>? list))
        {
            list = tree.Files.Where(f => f.Kind == kind && !AssetTree.IsStreamingCopy(f))
                       .OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
            assetsByKind[kind] = list;
        }
        return list;
    }

    public AssetNode? DefaultMaterial(AssetNode mesh)
    {
        string key = AssetTree.SourceKey(mesh.FullPath);
        string sameName = key.EndsWith(".mesh", StringComparison.Ordinal) ? key[..^".mesh".Length] + ".mdf2" : key;
        return Resolve(sameName) ?? mesh.Parent?.Children?.FirstOrDefault(c => c.Kind == AssetKind.Material);
    }

    void IAssetServices.Open(AssetNode node, string? element) => _ = OpenAssetAsync(node, element);

    public async Task<bool> PlayAudioAsync(string container, uint mediaId)
    {
        if (game is not { } current) return false;
        return await Task.Run(() => NativeMethods.rae_audio_play(current.Handle, container, mediaId) != 0);
    }

    public async Task ExportAudioAsync(string container, uint mediaId, string suggestedName)
    {
        if (game is not { } current || Shell == null) return;
        string? target = await Shell.PickSaveFileAsync(suggestedName);
        if (target == null) return;
        if (await Task.Run(() => NativeMethods.rae_audio_export_wav(current.Handle, container, mediaId, target)) != 0)
        {
            NativeLog.Write(LogLevel.Info, $"Exported {target}");
        }
    }

    public void Browse(AssetNode node)
    {
        Browser.Reveal(node);
        ShowTool("AssetBrowser");
        Shell?.ActivateMainWindow();
    }

    partial void OnSelectedGameChanged(GameOption? value)
    {
        if (value != null && !revertingGame) _ = OpenGameAsync(value);
    }

    private bool CanReloadGame() => !IsBusy && SelectedGame != null;

    [RelayCommand(CanExecute = nameof(CanReloadGame))]
    private Task ReloadGame() => SelectedGame is { } option ? OpenGameAsync(option) : Task.CompletedTask;

    [RelayCommand(CanExecute = nameof(CanReloadGame))]
    private Task ChangeGameFolder() => SelectedGame is { } option ? OpenGameAsync(option, chooseFolder: true) : Task.CompletedTask;

    private const string FirstPak = "re_chunk_000.pak";

    private static bool IsGameFolder(string? dir) => !string.IsNullOrEmpty(dir) && File.Exists(Path.Combine(dir, FirstPak));

    private async Task<string?> GameFolderAsync(GameOption option, bool choose)
    {
        settings.GamePaths.TryGetValue(option.Id, out string? saved);
        if (!choose && IsGameFolder(saved)) return saved;
        if (Shell == null) return null;
        string title = $"Where is {option.Name} installed?";
        while (true)
        {
            string? picked = await Shell.PickFolderAsync(title, saved);
            if (picked == null) return null;
            if (IsGameFolder(picked)) return picked;
            NativeLog.Write(LogLevel.Warning, $"{picked} is not a {option.Name} folder: it has no {FirstPak}");
            title = $"{option.Name}: choose the folder that holds {FirstPak}";
            saved = picked;
        }
    }

    private async Task OpenGameAsync(GameOption option, bool chooseFolder = false)
    {
        string? gameDir = await GameFolderAsync(option, chooseFolder);
        if (gameDir == null)
        {
            NativeLog.Write(LogLevel.Warning, $"{option.Name}: no game folder chosen");
            // Revert the list so choosing this game again asks again.
            if (SelectedGame != loadedGame)
            {
                revertingGame = true;
                SelectedGame = loadedGame;
                revertingGame = false;
            }
            return;
        }
        IsBusy = true;
        // Editors may still be reading from the game that is about to close.
        await CloseEditorsAsync();
        Browser.Clear();
        Hierarchy.Clear();
        Inspector.Clear();
        Viewport.Clear();
        openScene = null;
        SceneOverrides.Clear();
        SceneParameters.Clear();
        // The thumbnail worker reads from the game; stop it first.
        thumbnails?.Dispose();
        thumbnails = null;
        assetsByKind.Clear();
        materialNames.Clear();
        game?.Dispose();
        game = null;

        try
        {
            string assetsDir = Path.Combine(AppContext.BaseDirectory, "Assets");
            (NativeGame opened, AssetTree tree) = await Task.Run(() =>
            {
                NativeGame g = NativeGame.Open(option.Id, gameDir, assetsDir);
                try
                {
                    return (g, AssetTree.Build(g.ReadFileList()));
                }
                catch
                {
                    g.Dispose();
                    throw;
                }
            });
            game = opened;
            Viewport.Display.SetGame(opened);
            thumbnails = new ThumbnailService(opened);
            Browser.SetTree(tree);
            NativeLog.Write(LogLevel.Info, $"{tree.Files.Count:N0} files indexed");
            loadedGame = option;
            settings.Game = option.Id;
            settings.GamePaths[option.Id] = gameDir;
            settings.Save();
        }
        catch (Exception e)
        {
            NativeLog.Write(LogLevel.Error, $"{option.Name}: {e.Message}");
        }
        finally
        {
            IsBusy = false;
        }
        OpenPending();
    }

    private void OpenPending()
    {
        if (pendingOpen == null || game == null) return;
        string path = pendingOpen;
        pendingOpen = null;
        AssetNode? node = Browser.FindFile(path);
        if (node == null)
        {
            NativeLog.Write(LogLevel.Warning, $"--open: no file matches '{path}'");
            return;
        }
        Browser.Reveal(node);
        Dispatcher.UIThread.Post(() => _ = OpenAssetAsync(node), DispatcherPriority.Background);
    }

    private bool CanUseSelected() => Browser.SelectedAsset is { IsFolder: false };

    private bool CanOpenSelected() => !IsBusy && Browser.SelectedAsset is { CanOpen: true };

    [RelayCommand(CanExecute = nameof(CanOpenSelected))]
    private Task OpenSelected() => Browser.SelectedAsset is { } node ? OpenAssetAsync(node) : Task.CompletedTask;

    [RelayCommand(CanExecute = nameof(CanUseSelected))]
    private async Task ExtractSelected()
    {
        if (Browser.SelectedAsset is not { IsFolder: false } node || game == null || Shell == null) return;
        string fileName = node.FullPath[(node.FullPath.LastIndexOf('/') + 1)..];
        string? target = await Shell.PickSaveFileAsync(fileName);
        if (target == null) return;
        NativeGame current = game;
        try
        {
            await Task.Run(() => current.Extract(node.FullPath, target));
        }
        catch (Exception e)
        {
            NativeLog.Write(LogLevel.Error, $"Extract failed: {e.Message}");
        }
    }

    // Writes the scenes whose objects were moved, as copies under a folder chosen by the user (a mod layout).
    [RelayCommand]
    private async Task SaveEditedScenes()
    {
        if (game == null || Shell == null) return;
        List<string> lines = SceneParameters.Entries
            .Where(e => e.Key.StartsWith("param:xform:", StringComparison.Ordinal))
            .Select(e => e.Key + '\t' + string.Join(',', e.Value.Select(v => v.ToString("R", CultureInfo.InvariantCulture))))
            .ToList();
        if (lines.Count == 0)
        {
            NativeLog.Write(LogLevel.Warning, "Save edited scenes: nothing has been moved");
            return;
        }
        string? folder = await Shell.PickFolderAsync("Save edited scenes to", null);
        if (folder == null) return;
        IntPtr handle = game.Handle;
        string edits = string.Join('\n', lines);
        int written = await Task.Run(() => NativeMethods.rae_game_save_edited_scenes(handle, edits, folder));
        if (written < 0) NativeLog.Write(LogLevel.Error, $"Save edited scenes failed: {NativeMethods.LastError()}");
        else NativeLog.Write(LogLevel.Info, $"Saved {written} scene file(s) under {folder}");
    }

    [RelayCommand(CanExecute = nameof(CanUseSelected))]
    private async Task CopyPath()
    {
        if (Browser.SelectedAsset is { } node && Shell != null) await Shell.CopyTextAsync(node.FullPath);
    }

    public async Task OpenAssetAsync(AssetNode node, string? element = null)
    {
        if (IsBusy || game == null) return;
        if (!node.CanOpen)
        {
            NativeLog.Write(LogLevel.Warning, $"{node.Name}: no viewer for {node.KindLabel} files");
            return;
        }
        if (node.Kind is AssetKind.Mesh or AssetKind.Material or AssetKind.Sound or AssetKind.Effect or AssetKind.Texture or
            AssetKind.UvSequence or AssetKind.RenderTarget or AssetKind.Movie or AssetKind.UserData or AssetKind.Prefab or
            AssetKind.Collision or AssetKind.AiMap or AssetKind.Text or AssetKind.Gui or AssetKind.StateMachine or
            AssetKind.MotionBank)
        {
            OpenEditor(node, game, element);
            return;
        }

        Task outline = Hierarchy.LoadAsync(game, node);
        if (node.Kind == AssetKind.Scene)
        {
            openScene = node;
            SceneOverrides.Clear();
            SceneParameters.Clear();
            IsBusy = true;
            loadTask = Viewport.LoadAsync(node, game.Handle);
            await loadTask;
            IsBusy = false;
        }
        await outline;
    }

    // Changes made while a load runs are folded into one reload after it.
    private async Task ReloadSceneAsync()
    {
        reloadQueued = true;
        if (reloading) return;
        reloading = true;
        // Lets the rest of the same edit (a mesh and its material) join this reload.
        await Task.Yield();
        await loadTask;
        while (reloadQueued && openScene != null && game != null)
        {
            reloadQueued = false;
            IsBusy = true;
            string overrides = SceneOverrides.ToNativeText() + '\n' + SceneParameters.ToNativeText();
            loadTask = Viewport.LoadAsync(openScene, game.Handle, overrides, keepCamera: true);
            await loadTask;
            IsBusy = false;
        }
        reloadQueued = false;
        reloading = false;
        // A load starts with everything visible.
        Viewport.SetHiddenObjects(Hierarchy.HiddenKeys());
        Viewport.ShowSelection(Hierarchy.SelectedNode, Hierarchy.SelectedNodes);
    }

    private void OpenEditor(AssetNode node, NativeGame current, string? element)
    {
        if (Shell == null) return;
        AssetEditorViewModel? editor = editors.FirstOrDefault(e => e.Asset.FullPath == node.FullPath);
        if (editor != null)
        {
            Shell.ShowAssetEditor(editor);
            if (element != null && editor is MaterialEditorViewModel open) open.FocusMaterial(element);
            return;
        }
        editor = node.Kind switch
        {
            AssetKind.Material => new MaterialEditorViewModel(node, this),
            AssetKind.Sound => new SoundEditorViewModel(node, this),
            AssetKind.Effect => new EffectEditorViewModel(node, this),
            AssetKind.Texture or AssetKind.RenderTarget => new TextureEditorViewModel(node, this),
            AssetKind.Movie => new MovieEditorViewModel(node, this),
            AssetKind.UserData => new UserDataEditorViewModel(node, this),
            AssetKind.StateMachine => new StateMachineEditorViewModel(node, this),
            AssetKind.MotionBank => new MotionBankEditorViewModel(node, this),
            AssetKind.Prefab => new PrefabEditorViewModel(node, this),
            AssetKind.Collision => new CollisionEditorViewModel(node, this),
            AssetKind.AiMap => new AiMapEditorViewModel(node, this),
            AssetKind.Text => new MessageEditorViewModel(node, this),
            AssetKind.Gui => new GuiEditorViewModel(node, this),
            AssetKind.UvSequence => new UvSequenceEditorViewModel(node, this),
            _ => new MeshEditorViewModel(node, this)
        };
        editor.Closed += closed => editors.Remove(closed);
        editors.Add(editor);
        if (element != null && editor is MaterialEditorViewModel material) material.FocusMaterial(element);
        Shell.ShowAssetEditor(editor);
        _ = editor.Load(current);
    }

    private IRootDock CreateLayout()
    {
        IRootDock root = Docking.CreateLayout();
        Docking.InitLayout(root);
        return root;
    }

    [RelayCommand]
    private void ShowTool(string id)
    {
        PanelTool? tool = Docking.Tools.FirstOrDefault(t => t.Id == id);
        if (tool == null || Layout == null) return;
        if (Layout.HiddenDockables?.Contains(tool) == true) Docking.RestoreDockable(tool);
        if (tool.Owner is IDock owner)
        {
            Docking.SetActiveDockable(tool);
            Docking.SetFocusedDockable(owner, tool);
        }
    }

    [RelayCommand]
    private void ResetLayout()
    {
        CloseLayout();
        Layout = CreateLayout();
    }

    public void CloseLayout()
    {
        if (Layout is { } root && root.Close.CanExecute(null)) root.Close.Execute(null);
    }
}
