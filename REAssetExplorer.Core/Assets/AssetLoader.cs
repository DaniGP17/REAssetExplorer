using System.Collections.Concurrent;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Manages loading assets and their dependencies from PAK files.
/// </summary>
public class AssetLoader : IAssetLoader
{
    private readonly IGameProvider _gameProvider;
    private readonly Dictionary<string, PakFile> _pakFiles;
    private readonly DependencyResolverRegistry _resolverRegistry;
    private readonly Dictionary<string, PakEntryReference> _entryByNormalizedPath = new(StringComparer.Ordinal);

    // Thread-safe asset cache
    private readonly ConcurrentDictionary<string, AssetData> _loadedAssets = new(StringComparer.Ordinal);

    // Per-path signals: guarantees each asset is loaded exactly once across threads.
    // A thread that loses the race waits on the winner's signal rather than loading a duplicate.
    private readonly ConcurrentDictionary<string, ManualResetEventSlim> _loadSignals = new(StringComparer.Ordinal);

    // Guards against processing the same asset's dependency graph more than once.
    private readonly ConcurrentDictionary<string, byte> _dependenciesLoaded = new(StringComparer.Ordinal);

    // Cached once; passed to every DependencyLoadContext to avoid repeated allocations.
    private readonly DependencyLoadContext _sharedLoadContext;

    private readonly struct PakEntryReference
    {
        public PakEntryReference(PakFile pakFile, int entryIndex)
        {
            PakFile = pakFile;
            EntryIndex = entryIndex;
        }

        public PakFile PakFile { get; }
        public int EntryIndex { get; }
        public PakEntry Entry => PakFile.Entries[EntryIndex];
    }

    public AssetLoader(
        IGameProvider gameProvider,
        Dictionary<string, PakFile> pakFiles,
        DependencyResolverRegistry? resolverRegistry = null)
    {
        _gameProvider = gameProvider ?? throw new ArgumentNullException(nameof(gameProvider));
        _pakFiles = pakFiles ?? throw new ArgumentNullException(nameof(pakFiles));
        _resolverRegistry = resolverRegistry ?? CreateDefaultResolverRegistry();

        _sharedLoadContext = new DependencyLoadContext(this)
        {
            PakFiles = _pakFiles.Values.ToList(),
            GameProvider = _gameProvider
        };

        BuildLookupIndex();
    }

    private static DependencyResolverRegistry CreateDefaultResolverRegistry()
    {
        var registry = new DependencyResolverRegistry();
        registry.RegisterResolvers(
            new Resolvers.MeshDependencyResolver(),
            new Resolvers.MaterialDependencyResolver(),
            new Resolvers.TextureDependencyResolver(),
            new Resolvers.SdfDependencyResolver(),
            new Resolvers.SceneDependencyResolver()
        );
        return registry;
    }

    public DependencyResolverRegistry ResolverRegistry => _resolverRegistry;

    /// <inheritdoc/>
    public IGameProvider GameProvider => _gameProvider;

    /// <summary>
    /// Loads an asset and optionally its dependencies.
    /// Thread-safe: concurrent calls for the same path will coalesce — only one load happens,
    /// other callers wait for it to finish.
    /// </summary>
    public Result<T> LoadAsset<T>(
        string filePath,
        bool loadDependencies = true,
        Action<string>? onProgress = null) where T : AssetData
    {
        filePath = _gameProvider.FilesPath + filePath;
        var normalizedPath = NormalizePath(filePath);
        onProgress?.Invoke($"Loading {normalizedPath}...");

        // Fast path: already fully loaded.
        if (_loadedAssets.TryGetValue(normalizedPath, out var cached) && cached is T cachedTyped)
        {
            onProgress?.Invoke($"Using cached {normalizedPath}");
            return Result<T>.Success(cachedTyped);
        }

        // Try to become the loading thread for this path.
        var ourSignal = new ManualResetEventSlim(false);
        var signal = _loadSignals.GetOrAdd(normalizedPath, ourSignal);
        bool isLeader = ReferenceEquals(signal, ourSignal);

        if (!isLeader)
        {
            // Another thread owns the load — wait for it to finish.
            signal.Wait();
            if (_loadedAssets.TryGetValue(normalizedPath, out cached) && cached is T waited)
                return Result<T>.Success(waited);
            return Result<T>.Failure($"Asset failed to load (concurrent): {normalizedPath}");
        }

        // We are the loading thread.
        try
        {
            if (!_entryByNormalizedPath.TryGetValue(normalizedPath, out var entryRef))
            {
                Console.WriteLine("Asset not found: " + normalizedPath);
                onProgress?.Invoke($"Asset not found in PAK: {normalizedPath}");
                return Result<T>.Failure($"Asset not found: {normalizedPath}");
            }

            var data = _gameProvider.PakReader.ExtractFile(entryRef.PakFile, entryRef.Entry);
            var reader = _gameProvider.AssetReaders.GetReader<T>(normalizedPath);

            if (reader == null)
                return Result<T>.Failure($"No reader found for: {normalizedPath}");

            var result = reader.Read(data, normalizedPath);
            if (result.IsFailure)
                return Result<T>.Failure(result.Error!);

            var asset = result.Value!;

            if (string.IsNullOrEmpty(asset.Name))
                asset.Name = System.IO.Path.GetFileNameWithoutExtension(normalizedPath);
            if (string.IsNullOrEmpty(asset.FilePath))
                asset.FilePath = normalizedPath;
            if (string.IsNullOrEmpty(asset.Extension))
                asset.Extension = System.IO.Path.GetExtension(normalizedPath);

            var assetType = GetAssetType<T>();
            var resolver = _resolverRegistry.GetResolver(assetType);
            if (resolver != null)
            {
                resolver.ResolveDependencies(asset, new DependencyResolutionContext());

                if (asset.Dependencies.Count > 0)
                    onProgress?.Invoke($"Resolved {asset.Dependencies.Count} dependencies for {normalizedPath}");
            }

            // Store before loading deps so cycle-detection in LoadDependencies can find it.
            _loadedAssets[normalizedPath] = asset;

            if (loadDependencies && asset.Dependencies.Count > 0)
            {
                onProgress?.Invoke($"Loading {asset.Dependencies.Count} dependencies for {normalizedPath}...");
                LoadDependencies(asset, onProgress);
            }

            if (loadDependencies && asset is SceneData sceneToMerge)
                sceneToMerge.MergeExternalHierarchies();

            return Result<T>.Success(asset);
        }
        catch (Exception ex)
        {
            // Remove signal so a future call can retry.
            _loadSignals.TryRemove(normalizedPath, out _);
            return Result<T>.Failure($"Error loading {normalizedPath}: {ex.Message}");
        }
        finally
        {
            // Always wake up waiters whether we succeeded or failed.
            ourSignal.Set();
        }
    }

    /// <summary>
    /// Loads all dependencies of an asset in parallel.
    /// </summary>
    public void LoadDependencies(AssetData asset, Action<string>? onProgress = null)
    {
        var assetKey = NormalizePath(asset.FilePath ?? "");
        if (!string.IsNullOrEmpty(assetKey) && !_dependenciesLoaded.TryAdd(assetKey, 0))
            return;

        var assetType = GetAssetTypeFromData(asset);
        var resolver = _resolverRegistry.GetResolver(assetType);
        if (resolver == null)
        {
            Console.WriteLine($"No resolver found for asset type: {assetType}");
            return;
        }

        foreach (var dep in asset.Dependencies)
        {
            dep.FilePath = NormalizePath(dep.FilePath);

            // Here we could add a option to disable loading streaming textures
            if (dep.AssetType == AssetType.Texture)
            {
                var streamingDepPath = "streaming/" + dep.FilePath;
                var fullStreamingKey = NormalizePath(_gameProvider.FilesPath + streamingDepPath);
                if (_entryByNormalizedPath.ContainsKey(fullStreamingKey))
                    dep.FilePath = streamingDepPath;
            }
        }

        Parallel.ForEach(asset.Dependencies, dependency =>
        {
            if (string.IsNullOrEmpty(dependency.FilePath))
            {
                onProgress?.Invoke($"Skipping dependency '{dependency.Name}': path not resolved");
                return;
            }

            if (_loadedAssets.TryGetValue(dependency.FilePath, out var cached))
            {
                StoreResolvedDependency(asset, dependency.FilePath, cached);
                if (cached.Dependencies.Count > 0)
                    LoadDependencies(cached, onProgress);
                return;
            }

            onProgress?.Invoke($"Loading dependency: {dependency.FilePath} ({dependency.Purpose})");

            try
            {
                var loaded = resolver.LoadDependency(dependency, _sharedLoadContext, onProgress);
                if (loaded == null)
                {
                    if (dependency.IsRequired)
                        Console.WriteLine($"Failed to load REQUIRED dependency: {dependency.FilePath}");
                }
                else
                {
                    StoreResolvedDependency(asset, dependency.FilePath, loaded);
                    if (loaded.Dependencies.Count > 0)
                        LoadDependencies(loaded, onProgress);
                }
            }
            catch (Exception ex)
            {
                if (dependency.IsRequired)
                    Console.WriteLine($"Failed to load required dependency {dependency.FilePath}: {ex.Message}");
                else
                    Console.WriteLine($"Failed to load optional dependency {dependency.FilePath}: {ex.Message}");
            }
        });
    }

    /// <summary>
    /// Returns true if the given exact normalized path exists in the PAK index.
    /// </summary>
    public bool EntryExists(string filePath) =>
        _entryByNormalizedPath.ContainsKey(NormalizePath(filePath));

    private static AssetType GetAssetType<T>() where T : AssetData
    {
        return typeof(T).Name switch
        {
            nameof(MeshData) => AssetType.Mesh,
            nameof(MaterialData) => AssetType.Material,
            nameof(TextureData) => AssetType.Texture,
            nameof(SceneData) => AssetType.Scene,
            _ => AssetType.Unknown
        };
    }

    private static AssetType GetAssetTypeFromData(AssetData asset)
    {
        return asset switch
        {
            MeshData => AssetType.Mesh,
            MaterialData => AssetType.Material,
            TextureData => AssetType.Texture,
            SceneData => AssetType.Scene,
            _ => AssetType.Unknown
        };
    }

    public T? GetLoadedAsset<T>(string filePath) where T : AssetData
    {
        var normalizedPath = NormalizePath(filePath);
        if (_loadedAssets.TryGetValue(normalizedPath, out var asset) && asset is T typedAsset)
            return typedAsset;
        return null;
    }

    public bool IsAssetLoaded(string filePath) =>
        _loadedAssets.ContainsKey(NormalizePath(filePath));

    public void ClearCache()
    {
        _loadedAssets.Clear();
        _dependenciesLoaded.Clear();
        _loadSignals.Clear();
    }

    public IEnumerable<T> GetLoadedAssets<T>() where T : AssetData =>
        _loadedAssets.Values.OfType<T>();

    private static void StoreResolvedDependency(AssetData asset, string filePath, AssetData loaded)
    {
        asset.ResolvedDependencies[filePath] = loaded;
        const string streamingPrefix = "streaming/";
        if (filePath.StartsWith(streamingPrefix))
            asset.ResolvedDependencies[filePath[streamingPrefix.Length..]] = loaded;
    }

    private void BuildLookupIndex()
    {
        foreach (var pakFile in _pakFiles.Values)
        {
            for (int i = 0; i < pakFile.Entries.Count; i++)
            {
                var entry = pakFile.Entries[i];
                if (string.IsNullOrWhiteSpace(entry.FilePath))
                    continue;

                var normalizedPath = NormalizePath(entry.FilePath);
                if (!_entryByNormalizedPath.ContainsKey(normalizedPath))
                    _entryByNormalizedPath[normalizedPath] = new PakEntryReference(pakFile, i);
            }
        }
    }

    private static string NormalizePath(string? filePath)
    {
        if (string.IsNullOrWhiteSpace(filePath))
            return string.Empty;

        string normalized = filePath.Replace('\\', '/').ToLowerInvariant();

        int lastDot = normalized.LastIndexOf('.');
        if (lastDot == -1)
            return normalized;

        for (int i = lastDot + 1; i < normalized.Length; i++)
        {
            char c = normalized[i];
            if (c < '0' || c > '9')
                return normalized;
        }

        int secondLastDot = normalized.LastIndexOf('.', lastDot - 1);
        if (secondLastDot == -1)
            return normalized;

        return normalized.Substring(0, lastDot);
    }
}
