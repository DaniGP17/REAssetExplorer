using System.IO;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.UI.Services;

public class CacheService
{
    private readonly Dictionary<string, CacheFile?> _cache = new();
    private readonly string _cacheDirectory;

    public CacheService(string? cacheDirectory = null)
    {
        _cacheDirectory = cacheDirectory ?? "Cache";
        EnsureCacheDirectoryExists();
    }

    private void EnsureCacheDirectoryExists()
    {
        if (!Directory.Exists(_cacheDirectory))
        {
            Directory.CreateDirectory(_cacheDirectory);
        }
    }

    private string GetCachePath(string cacheName) => Path.Combine(_cacheDirectory, $"{cacheName}.bin");
    
    /// <summary>
    /// Gets the full path to a cache file.
    /// </summary>
    /// <param name="cacheName">The name of the cache.</param>
    /// <returns>The full path to the cache file.</returns>
    public string GetCacheFilePath(string cacheName) => GetCachePath(cacheName);

    public void LoadCache<T>(string cacheName) where T : CacheFile, new()
    {
        var cachePath = GetCachePath(cacheName);
        
        if (!File.Exists(cachePath))
        {
            return;
        }
        
        var cacheFile = CacheFile.LoadFromFile<T>(cachePath);
        _cache[cacheName] = cacheFile;
    }
    
    public void GetCache<T>(string cacheName, out T? cacheFile) where T : CacheFile
    {
        if (_cache.TryGetValue(cacheName, out var cached) && cached is T typedCache)
        {
            cacheFile = typedCache;
            return;
        }

        cacheFile = null;
    }

    public T? GetOrLoadCache<T>(string cacheName) where T : CacheFile, new()
    {
        if (!_cache.ContainsKey(cacheName))
        {
            LoadCache<T>(cacheName);
        }

        GetCache<T>(cacheName, out var cacheFile);
        return cacheFile;
    }

    public void SaveCache<T>(string cacheName, T cacheFile) where T : CacheFile
    {
        var cachePath = GetCachePath(cacheName);
        
        using var stream = File.Create(cachePath);
        cacheFile.WriteBytes(stream);
        
        _cache[cacheName] = cacheFile;
    }

    public bool HasCache(string cacheName)
    {
        return _cache.ContainsKey(cacheName) || File.Exists(GetCachePath(cacheName));
    }

    public void InvalidateCache(string cacheName)
    {
        if (_cache.ContainsKey(cacheName))
        {
            _cache.Remove(cacheName);
        }

        var cachePath = GetCachePath(cacheName);
        if (File.Exists(cachePath))
        {
            File.Delete(cachePath);
        }
    }

    public void ClearAllCache()
    {
        _cache.Clear();

        if (Directory.Exists(_cacheDirectory))
        {
            foreach (var file in Directory.GetFiles(_cacheDirectory, "*.bin"))
            {
                File.Delete(file);
            }
        }
    }

    public void UnloadCache(string cacheName)
    {
        if (_cache.ContainsKey(cacheName))
        {
            _cache.Remove(cacheName);
        }
    }

    public IReadOnlyCollection<string> GetLoadedCacheNames()
    {
        return _cache.Keys.ToList().AsReadOnly();
    }

    public IEnumerable<string> GetAvailableCacheNames()
    {
        if (!Directory.Exists(_cacheDirectory))
        {
            return Enumerable.Empty<string>();
        }

        return Directory.GetFiles(_cacheDirectory, "*.bin")
            .Select(Path.GetFileNameWithoutExtension)
            .Where(name => !string.IsNullOrEmpty(name))
            .Select(name => name!);
    }
}