using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.TestUI;

public sealed class SceneLoadedEventArgs : EventArgs
{
    public SceneData Scene { get; }
    public SceneLoadedEventArgs(SceneData scene) => Scene = scene;
}

public sealed class MeshLoadedEventArgs : EventArgs
{
    public MeshData Mesh { get; }
    /// <summary>Mesh path relative to FilesPath (the same form the renderer expects).</summary>
    public string MeshPath { get; }
    /// <summary>Resolved mdf2 path relative to FilesPath, or empty if no material was found.</summary>
    public string MdfPath { get; }

    public MeshLoadedEventArgs(MeshData mesh, string meshPath, string mdfPath)
    {
        Mesh = mesh;
        MeshPath = meshPath;
        MdfPath = mdfPath;
    }
}

/// <summary>
/// Owns the AssetLoader for the current game session and coordinates cross-panel asset events.
/// Created once after PAK files are loaded; persists for the lifetime of the MainWindow.
/// </summary>
public sealed class AssetSession
{
    public static AssetSession? Current { get; private set; }

    private readonly AssetLoader _loader;

    public AssetLoader Loader => _loader;
    public SceneData? CurrentScene { get; private set; }
    public MeshData?  CurrentMesh  { get; private set; }

    /// <summary>Raised on the calling thread when a scene finishes loading (success only).</summary>
    public event EventHandler<SceneLoadedEventArgs>? SceneLoaded;

    /// <summary>Raised on the calling thread when a single mesh finishes loading (success only).</summary>
    public event EventHandler<MeshLoadedEventArgs>?  MeshLoaded;

    public static void Initialize(IGameProvider provider, IReadOnlyDictionary<string, PakFile> pakFiles)
    {
        Current = new AssetSession(provider, new Dictionary<string, PakFile>(pakFiles));
    }

    private AssetSession(IGameProvider provider, Dictionary<string, PakFile> pakFiles)
    {
        _loader = new AssetLoader(provider, pakFiles);
    }

    /// <summary>
    /// Loads a scene by path relative to the game's FilesPath (e.g. "scene/master.scn.20").
    /// Runs on a background thread; raises SceneLoaded on completion.
    /// </summary>
    public async Task<Result<SceneData>> LoadSceneAsync(string filePath, Action<string>? onProgress = null)
    {
        var result = await Task.Run(() =>
            _loader.LoadAsset<SceneData>(filePath, loadDependencies: true, onProgress));

        if (result.IsSuccess)
        {
            CurrentScene = result.Value;
            SceneLoaded?.Invoke(this, new SceneLoadedEventArgs(result.Value!));
        }
        return result;
    }

    /// <summary>
    /// Loads a single mesh by path relative to the game's FilesPath
    /// (e.g. "character/em3200/em3200.mesh.220128762"). The companion .mdf2 file
    /// is auto-discovered by base name and passed alongside if it exists in the PAK.
    /// Runs on a background thread; raises MeshLoaded on completion.
    /// </summary>
    public async Task<Result<MeshData>> LoadMeshAsync(string filePath, Action<string>? onProgress = null)
    {
        var result = await Task.Run(() =>
            _loader.LoadAsset<MeshData>(filePath, loadDependencies: false, onProgress));

        if (result.IsSuccess)
        {
            CurrentMesh = result.Value;
            var mdfPath = TryResolveCompanionMdfPath(filePath);
            MeshLoaded?.Invoke(this, new MeshLoadedEventArgs(result.Value!, filePath, mdfPath));
        }
        return result;
    }

    /// <summary>
    /// Given a mesh path like "character/em3200/em3200.mesh.220128762", returns the
    /// expected .mdf2 sibling (without version suffix — AssetLoader normalizes that away)
    /// if a PAK entry for it exists; otherwise the empty string.
    /// </summary>
    private string TryResolveCompanionMdfPath(string meshFilePath)
    {
        var fileName = Path.GetFileName(meshFilePath);
        int dot = fileName.IndexOf(".mesh", StringComparison.OrdinalIgnoreCase);
        if (dot < 0) return string.Empty;

        var baseName = fileName[..dot];
        var dir = Path.GetDirectoryName(meshFilePath)?.Replace('\\', '/') ?? string.Empty;
        var mdfRelative = string.IsNullOrEmpty(dir) ? $"{baseName}.mdf2" : $"{dir}/{baseName}.mdf2";

        // EntryExists takes the full path (FilesPath + relative). NormalizePath strips trailing version.
        var fullPath = _loader.GameProvider.FilesPath + mdfRelative;
        return _loader.EntryExists(fullPath) ? mdfRelative : string.Empty;
    }
}
