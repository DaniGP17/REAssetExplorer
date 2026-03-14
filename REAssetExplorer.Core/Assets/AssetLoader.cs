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
    private readonly MaterialsCache? _materialsCache;
    private readonly Dictionary<string, AssetData> _loadedAssets = new(StringComparer.Ordinal);
    private readonly DependencyResolverRegistry _resolverRegistry;
    private readonly Dictionary<string, PakEntryReference> _entryByNormalizedPath = new(StringComparer.Ordinal);
    private readonly Dictionary<string, PakEntryReference> _entryByTrimmedPath = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<PakEntryReference>> _entriesByTrimmedFileName = new(StringComparer.Ordinal);
    private readonly Dictionary<string, PakEntryReference?> _resolvedEntryCache = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _bestVariantPathCache = new(StringComparer.Ordinal);

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
        MaterialsCache? materialsCache = null,
        DependencyResolverRegistry? resolverRegistry = null)
    {
        _gameProvider = gameProvider ?? throw new ArgumentNullException(nameof(gameProvider));
        _pakFiles = pakFiles ?? throw new ArgumentNullException(nameof(pakFiles));
        _materialsCache = materialsCache;
        _resolverRegistry = resolverRegistry ?? CreateDefaultResolverRegistry();
        BuildLookupIndexes();
    }
    
    /// <summary>
    /// Creates a default resolver registry with standard resolvers.
    /// </summary>
    private static DependencyResolverRegistry CreateDefaultResolverRegistry()
    {
        var registry = new DependencyResolverRegistry();
        registry.RegisterResolvers(
            new Resolvers.MeshDependencyResolver(),
            new Resolvers.MaterialDependencyResolver(),
            new Resolvers.TextureDependencyResolver(),
            new Resolvers.SdfDependencyResolver()
        );
        return registry;
    }
    
    /// <summary>
    /// Gets the resolver registry to allow registering custom resolvers.
    /// </summary>
    public DependencyResolverRegistry ResolverRegistry => _resolverRegistry;
    
    /// <inheritdoc/>
    public IGameProvider GameProvider => _gameProvider;
    
    /// <summary>
    /// Loads an asset and optionally its dependencies.
    /// </summary>
    /// <typeparam name="T">The type of asset to load.</typeparam>
    /// <param name="filePath">The path to the asset within the PAK files.</param>
    /// <param name="loadDependencies">Whether to also load dependencies.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    /// <returns>Result containing the loaded asset or an error.</returns>
    public Result<T> LoadAsset<T>(
        string filePath, 
        bool loadDependencies = true,
        Action<string>? onProgress = null) where T : AssetData
    {
        var normalizedPath = NormalizePath(filePath);
        onProgress?.Invoke($"Loading {normalizedPath}...");
        
        // Check if already loaded
        if (_loadedAssets.TryGetValue(normalizedPath, out var cached) && cached is T cachedTyped)
        {
            onProgress?.Invoke($"Using cached {normalizedPath}");
            return Result<T>.Success(cachedTyped);
        }
        
        // Find the asset in PAK files
        var (pakFile, entry) = FindAssetEntry(normalizedPath);
        if (pakFile == null || entry == null)
        {
            onProgress?.Invoke($"Asset not found in PAK: {normalizedPath}");
            return Result<T>.Failure($"Asset not found: {normalizedPath}");
        }

        var resolvedPath = NormalizePath(entry.Value.FilePath);
        if (_loadedAssets.TryGetValue(resolvedPath, out var resolvedCached) && resolvedCached is T resolvedTyped)
        {
            _loadedAssets[normalizedPath] = resolvedTyped;
            onProgress?.Invoke($"Using cached {resolvedPath}");
            return Result<T>.Success(resolvedTyped);
        }
        
        // Extract and read the asset
        try
        {
            var data = _gameProvider.PakReader.ExtractFile(pakFile, entry.Value);
            var reader = _gameProvider.AssetReaders.GetReader<T>(resolvedPath);
            
            if (reader == null)
            {
                return Result<T>.Failure($"No reader found for: {resolvedPath}");
            }
            
            var result = reader.Read(data, resolvedPath);
            
            if (result.IsFailure)
            {
                return Result<T>.Failure(result.Error!);
            }
            
            var asset = result.Value!;
            
            // Set basic properties if not already set
            if (string.IsNullOrEmpty(asset.Name))
            {
                asset.Name = System.IO.Path.GetFileNameWithoutExtension(resolvedPath);
            }
            if (string.IsNullOrEmpty(asset.FilePath))
            {
                asset.FilePath = resolvedPath;
            }
            if (string.IsNullOrEmpty(asset.Extension))
            {
                asset.Extension = System.IO.Path.GetExtension(resolvedPath);
            }
            
            // Resolve dependencies using the appropriate resolver
            var assetType = GetAssetType<T>();
            var resolver = _resolverRegistry.GetResolver(assetType);
            
            if (resolver != null)
            {
                var resolutionContext = new DependencyResolutionContext
                {
                    MaterialsCache = _materialsCache
                };
                
                resolver.ResolveDependencies(asset, resolutionContext);
                
                if (asset.Dependencies.Count > 0)
                {
                    onProgress?.Invoke($"Resolved {asset.Dependencies.Count} dependencies for {resolvedPath}");
                }
            }
            
            // Cache the asset
            _loadedAssets[normalizedPath] = asset;
            _loadedAssets[resolvedPath] = asset;
            
            // Load dependencies if requested
            if (loadDependencies && asset.Dependencies.Count > 0)
            {
                onProgress?.Invoke($"Loading {asset.Dependencies.Count} dependencies for {resolvedPath}...");
                LoadDependencies(asset, onProgress);
            }
            
            return Result<T>.Success(asset);
        }
        catch (Exception ex)
        {
            return Result<T>.Failure($"Error loading {resolvedPath}: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Loads all dependencies of an asset.
    /// </summary>
    /// <param name="asset">The asset whose dependencies to load.</param>
    /// <param name="onProgress">Optional callback for progress updates.</param>
    public void LoadDependencies(AssetData asset, Action<string>? onProgress = null)
    {
        var assetType = GetAssetTypeFromData(asset);
        var resolver = _resolverRegistry.GetResolver(assetType);
        
        if (resolver == null)
        {
            Console.WriteLine($"No resolver found for asset type: {assetType}");
            return;
        }
        
        var loadContext = new DependencyLoadContext(this)
        {
            MaterialsCache = _materialsCache,
            PakFiles = _pakFiles.Values.ToList(),
            GameProvider = _gameProvider
        };
        
        foreach (var dependency in asset.Dependencies)
        {
            dependency.FilePath = NormalizePath(dependency.FilePath);
            if (string.IsNullOrEmpty(dependency.FilePath))
            {
                onProgress?.Invoke($"Skipping dependency '{dependency.Name}': path not resolved");
                continue;
            }
            
            // Skip if already loaded
            if (_loadedAssets.ContainsKey(dependency.FilePath))
            {
                asset.ResolvedDependencies[dependency.FilePath] = _loadedAssets[dependency.FilePath];
                continue;
            }
            
            onProgress?.Invoke($"Loading dependency: {dependency.FilePath} ({dependency.Purpose})");
            
            try
            {
                var loadedDependency = resolver.LoadDependency(dependency, loadContext, onProgress);
                
                if (loadedDependency == null)
                {
                    if (dependency.IsRequired)
                    {
                        Console.WriteLine($"Failed to load REQUIRED dependency: {dependency.FilePath}");
                    }
                }
                else
                {
                    // Store the resolved dependency in the parent asset
                    asset.ResolvedDependencies[dependency.FilePath] = loadedDependency;
                }
            }
            catch (Exception ex)
            {
                if (dependency.IsRequired)
                {
                    Console.WriteLine($"Failed to load required dependency {dependency.FilePath}: {ex.Message}");
                }
                else
                {
                    Console.WriteLine($"Failed to load optional dependency {dependency.FilePath}: {ex.Message}");
                }
            }
        }
    }
    
    /// <summary>
    /// Gets the asset type for a generic type parameter.
    /// </summary>
    private static AssetType GetAssetType<T>() where T : AssetData
    {
        return typeof(T).Name switch
        {
            nameof(MeshData) => AssetType.Mesh,
            nameof(MaterialData) => AssetType.Material,
            nameof(TextureData) => AssetType.Texture,
            _ => AssetType.Unknown
        };
    }
    
    /// <summary>
    /// Gets the asset type from an asset data instance.
    /// </summary>
    private static AssetType GetAssetTypeFromData(AssetData asset)
    {
        return asset switch
        {
            MeshData => AssetType.Mesh,
            MaterialData => AssetType.Material,
            TextureData => AssetType.Texture,
            _ => AssetType.Unknown
        };
    }
    
    /// <summary>
    /// Gets a loaded asset from the cache.
    /// </summary>
    /// <typeparam name="T">The type of asset to get.</typeparam>
    /// <param name="filePath">The path to the asset.</param>
    /// <returns>The cached asset, or null if not found or wrong type.</returns>
    public T? GetLoadedAsset<T>(string filePath) where T : AssetData
    {
        var normalizedPath = NormalizePath(filePath);
        if (_loadedAssets.TryGetValue(normalizedPath, out var asset) && asset is T typedAsset)
        {
            return typedAsset;
        }
        return null;
    }
    
    /// <summary>
    /// Checks if an asset is already loaded.
    /// </summary>
    /// <param name="filePath">The path to the asset.</param>
    /// <returns>True if the asset is loaded, false otherwise.</returns>
    public bool IsAssetLoaded(string filePath)
    {
        return _loadedAssets.ContainsKey(NormalizePath(filePath));
    }
    
    /// <summary>
    /// Clears all loaded assets from the cache.
    /// </summary>
    public void ClearCache()
    {
        _loadedAssets.Clear();
    }
    
    /// <summary>
    /// Gets all loaded assets of a specific type.
    /// </summary>
    /// <typeparam name="T">The type of assets to get.</typeparam>
    /// <returns>Collection of loaded assets of the specified type.</returns>
    public IEnumerable<T> GetLoadedAssets<T>() where T : AssetData
    {
        return _loadedAssets.Values.OfType<T>();
    }

    /// <summary>
    /// Finds the best available asset variant for a given logical path.
    /// For textures this prefers streaming/high-resolution variants without rescanning all PAK entries.
    /// </summary>
    public string FindBestVariantPath(string filePath)
    {
        var normalizedPath = NormalizePath(filePath);
        if (string.IsNullOrEmpty(normalizedPath))
            return normalizedPath;

        if (_bestVariantPathCache.TryGetValue(normalizedPath, out var cachedPath))
            return cachedPath;

        var trimmedPath = TrimNumericExtension(normalizedPath);
        var trimmedFileName = Path.GetFileName(trimmedPath);
        if (string.IsNullOrEmpty(trimmedFileName) ||
            !_entriesByTrimmedFileName.TryGetValue(trimmedFileName, out var candidates) ||
            candidates.Count == 0)
        {
            _bestVariantPathCache[normalizedPath] = normalizedPath;
            return normalizedPath;
        }

        var canonicalQueryPath = CanonicalizeVariantPath(trimmedPath);
        PakEntryReference? bestCandidate = null;
        int bestScore = int.MinValue;
        long bestSize = long.MinValue;

        foreach (var candidate in candidates)
        {
            var candidatePath = NormalizePath(candidate.Entry.FilePath);
            var candidateTrimmedPath = TrimNumericExtension(candidatePath);
            var candidateCanonicalPath = CanonicalizeVariantPath(candidateTrimmedPath);
            int score = ScoreVariantCandidate(canonicalQueryPath, candidateCanonicalPath);
            if (score == int.MinValue)
                continue;

            long size = candidate.Entry.UncompressedSize;
            if (score > bestScore || (score == bestScore && size > bestSize))
            {
                bestCandidate = candidate;
                bestScore = score;
                bestSize = size;
            }
        }

        var bestPath = bestCandidate.HasValue
            ? NormalizePath(bestCandidate.Value.Entry.FilePath)
            : normalizedPath;

        _bestVariantPathCache[normalizedPath] = bestPath;
        return bestPath;
    }
    
    /// <summary>
    /// Finds an asset entry in the PAK files.
    /// Uses contains matching to handle paths with different prefixes and version suffixes.
    /// </summary>
    /// <param name="filePath">The path to search for.</param>
    /// <returns>A tuple with the PAK file and entry, or (null, null) if not found.</returns>
    private (PakFile? pakFile, PakEntry? entry) FindAssetEntry(string filePath)
    {
        var normalizedPath = NormalizePath(filePath);
        if (string.IsNullOrEmpty(normalizedPath))
            return (null, null);

        if (_resolvedEntryCache.TryGetValue(normalizedPath, out var cachedResult))
        {
            return cachedResult.HasValue
                ? (cachedResult.Value.PakFile, cachedResult.Value.Entry)
                : (null, null);
        }

        if (_entryByNormalizedPath.TryGetValue(normalizedPath, out var exactMatch))
        {
            _resolvedEntryCache[normalizedPath] = exactMatch;
            return (exactMatch.PakFile, exactMatch.Entry);
        }

        var trimmedPath = TrimNumericExtension(normalizedPath);
        if (_entryByTrimmedPath.TryGetValue(trimmedPath, out var trimmedMatch))
        {
            _resolvedEntryCache[normalizedPath] = trimmedMatch;
            return (trimmedMatch.PakFile, trimmedMatch.Entry);
        }

        var trimmedFileName = Path.GetFileName(trimmedPath);
        if (!string.IsNullOrEmpty(trimmedFileName) &&
            _entriesByTrimmedFileName.TryGetValue(trimmedFileName, out var candidates))
        {
            PakEntryReference? bestCandidate = null;
            int bestScore = int.MinValue;
            long bestSize = long.MinValue;

            foreach (var candidate in candidates)
            {
                var candidatePath = NormalizePath(candidate.Entry.FilePath);
                var candidateTrimmedPath = TrimNumericExtension(candidatePath);
                int score = ScoreLookupCandidate(normalizedPath, trimmedPath, candidatePath, candidateTrimmedPath);
                if (score == int.MinValue)
                    continue;

                long size = candidate.Entry.UncompressedSize;
                if (score > bestScore || (score == bestScore && size > bestSize))
                {
                    bestCandidate = candidate;
                    bestScore = score;
                    bestSize = size;
                }
            }

            if (bestCandidate.HasValue)
            {
                _resolvedEntryCache[normalizedPath] = bestCandidate;
                return (bestCandidate.Value.PakFile, bestCandidate.Value.Entry);
            }
        }

        _resolvedEntryCache[normalizedPath] = null;
        return (null, null);
    }

    private void BuildLookupIndexes()
    {
        foreach (var pakFile in _pakFiles.Values)
        {
            for (int i = 0; i < pakFile.Entries.Count; i++)
            {
                var entry = pakFile.Entries[i];
                if (string.IsNullOrWhiteSpace(entry.FilePath))
                    continue;

                var entryRef = new PakEntryReference(pakFile, i);
                var normalizedPath = NormalizePath(entry.FilePath);
                var trimmedPath = TrimNumericExtension(normalizedPath);

                AddOrReplaceBestEntry(_entryByNormalizedPath, normalizedPath, entryRef);
                AddOrReplaceBestEntry(_entryByTrimmedPath, trimmedPath, entryRef);

                var trimmedFileName = Path.GetFileName(trimmedPath);
                if (!string.IsNullOrEmpty(trimmedFileName))
                {
                    if (!_entriesByTrimmedFileName.TryGetValue(trimmedFileName, out var entries))
                    {
                        entries = new List<PakEntryReference>();
                        _entriesByTrimmedFileName[trimmedFileName] = entries;
                    }

                    entries.Add(entryRef);
                }
            }
        }

        foreach (var entries in _entriesByTrimmedFileName.Values)
        {
            entries.Sort((left, right) => right.Entry.UncompressedSize.CompareTo(left.Entry.UncompressedSize));
        }
    }

    private static void AddOrReplaceBestEntry(
        Dictionary<string, PakEntryReference> index,
        string key,
        PakEntryReference candidate)
    {
        if (string.IsNullOrEmpty(key))
            return;

        if (index.TryGetValue(key, out var current) && current.Entry.UncompressedSize >= candidate.Entry.UncompressedSize)
            return;

        index[key] = candidate;
    }

    private static string NormalizePath(string? filePath)
    {
        if (string.IsNullOrWhiteSpace(filePath))
            return string.Empty;

        return filePath.Replace('\\', '/').ToLowerInvariant();
    }

    private static string TrimNumericExtension(string path)
    {
        if (string.IsNullOrEmpty(path))
            return path;

        int slashIndex = path.LastIndexOf('/');
        int dotIndex = path.LastIndexOf('.');
        if (dotIndex <= slashIndex || dotIndex < 0)
            return path;

        for (int i = dotIndex + 1; i < path.Length; i++)
        {
            if (!char.IsDigit(path[i]))
                return path;
        }

        return path[..dotIndex];
    }

    private static string CanonicalizeVariantPath(string path)
    {
        if (string.IsNullOrEmpty(path))
            return path;

        return path.Replace("/streaming/", "/", StringComparison.Ordinal);
    }

    private static int ScoreLookupCandidate(
        string normalizedQueryPath,
        string trimmedQueryPath,
        string candidatePath,
        string candidateTrimmedPath)
    {
        if (candidatePath.Equals(normalizedQueryPath, StringComparison.Ordinal))
            return 5000;

        if (candidateTrimmedPath.Equals(trimmedQueryPath, StringComparison.Ordinal))
            return 4500;

        if (candidateTrimmedPath.EndsWith(trimmedQueryPath, StringComparison.Ordinal))
            return 4000 + CountSharedPathSuffixSegments(trimmedQueryPath, candidateTrimmedPath);

        if (candidatePath.EndsWith(normalizedQueryPath, StringComparison.Ordinal))
            return 3500 + CountSharedPathSuffixSegments(normalizedQueryPath, candidatePath);

        if (candidatePath.Contains(normalizedQueryPath, StringComparison.Ordinal))
            return 3000 + CountSharedPathSuffixSegments(trimmedQueryPath, candidateTrimmedPath);

        var queryFileName = Path.GetFileName(trimmedQueryPath);
        var candidateFileName = Path.GetFileName(candidateTrimmedPath);
        if (!candidateFileName.Equals(queryFileName, StringComparison.Ordinal))
            return int.MinValue;

        return 2000 + CountSharedPathSuffixSegments(
            Path.GetDirectoryName(trimmedQueryPath) ?? string.Empty,
            Path.GetDirectoryName(candidateTrimmedPath) ?? string.Empty);
    }

    private static int ScoreVariantCandidate(string canonicalQueryPath, string candidateCanonicalPath)
    {
        if (candidateCanonicalPath.Equals(canonicalQueryPath, StringComparison.Ordinal))
            return 4000;

        if (candidateCanonicalPath.EndsWith(canonicalQueryPath, StringComparison.Ordinal))
            return 3000 + CountSharedPathSuffixSegments(canonicalQueryPath, candidateCanonicalPath);

        var queryFileName = Path.GetFileName(canonicalQueryPath);
        var candidateFileName = Path.GetFileName(candidateCanonicalPath);
        if (!candidateFileName.Equals(queryFileName, StringComparison.Ordinal))
            return int.MinValue;

        return 2000 + CountSharedPathSuffixSegments(
            Path.GetDirectoryName(canonicalQueryPath) ?? string.Empty,
            Path.GetDirectoryName(candidateCanonicalPath) ?? string.Empty);
    }

    private static int CountSharedPathSuffixSegments(string left, string right)
    {
        if (string.IsNullOrEmpty(left) || string.IsNullOrEmpty(right))
            return 0;

        var leftSegments = left.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var rightSegments = right.Split('/', StringSplitOptions.RemoveEmptyEntries);

        int count = 0;
        int leftIndex = leftSegments.Length - 1;
        int rightIndex = rightSegments.Length - 1;

        while (leftIndex >= 0 && rightIndex >= 0)
        {
            if (!leftSegments[leftIndex].Equals(rightSegments[rightIndex], StringComparison.Ordinal))
                break;

            count++;
            leftIndex--;
            rightIndex--;
        }

        return count;
    }
}
