using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.Interop.Structures;

namespace REAssetExplorer.RenderTest2.Core;

public class ResourceManager : IDisposable
{
    private readonly AssetLoader _assetLoader;
    private readonly IGameProvider _gameProvider;

    private readonly ConcurrentQueue<LoadRequest> _loadQueue = new();
    private int _loadQueueCount = 0;

    private readonly ConcurrentQueue<Resource> _deleteQueue = new();
    
    // Deferred release queue for GPU resource safety
    private readonly ConcurrentQueue<(Resource resource, ulong releaseFrame)> _releaseQueue = new();

    private readonly ConcurrentDictionary<ulong, Resource> _resourceDict = new();

    private long _totalLoadedResources = 0;
    private long _totalFailedLoads = 0;

    private class LoadRequest
    {
        public string FilePath { get; set; } = string.Empty;
        public ResourceType Type { get; set; }
        public bool LoadDependencies { get; set; }
        public ulong RequestId { get; set; }
    }

    public int LoadQueueCount => Volatile.Read(ref _loadQueueCount);
    public long TotalLoadedResources => Volatile.Read(ref _totalLoadedResources);
    public long TotalFailedLoads => Volatile.Read(ref _totalFailedLoads);

    public ResourceManager(Dictionary<string, PakFile> pakFiles, IGameProvider gameProvider, MaterialsCache? materialsCache = null)
    {
        _gameProvider = gameProvider ?? throw new ArgumentNullException(nameof(gameProvider));
        _assetLoader = new AssetLoader(gameProvider, pakFiles, materialsCache);
    }
    
    /// <summary>
    /// Gets the underlying AssetLoader for advanced operations
    /// </summary>
    public AssetLoader AssetLoader => _assetLoader;

    #region Queue Management

    public ulong EnqueueLoad(string filePath, ResourceType type, bool loadDependencies = true)
    {
        var requestId = HashPath(filePath);
        
        if (_resourceDict.ContainsKey(requestId))
            return requestId;

        var request = new LoadRequest
        {
            FilePath = filePath.ToLowerInvariant(),
            Type = type,
            LoadDependencies = loadDependencies,
            RequestId = requestId
        };

        _loadQueue.Enqueue(request);
        Interlocked.Increment(ref _loadQueueCount);
        
        return requestId;
    }
    
    public void ProcessLoadQueue(int maxItemsPerFrame = Int32.MaxValue)
    {
        int processedCount = 0;
        
        while (processedCount < maxItemsPerFrame && _loadQueue.TryDequeue(out var request))
        {
            Interlocked.Decrement(ref _loadQueueCount);
            
            if (_resourceDict.ContainsKey(request.RequestId))
            {
                processedCount++;
                continue;
            }

            Resource? resource = request.Type switch
            {
                ResourceType.Texture => LoadTexture(request.FilePath),
                ResourceType.Mesh => LoadMesh(request.FilePath),
                //ResourceType.Material => LoadMaterial(request.FilePath),
                ResourceType.Shader => LoadShader(request.FilePath),
                _ => null
            };

            if (resource != null)
            {
                Interlocked.Increment(ref _totalLoadedResources);
            }
            else
            {
                Interlocked.Increment(ref _totalFailedLoads);
            }

            processedCount++;
        }
    }

    public void EnqueueDelete(ulong id)
    {
        if (_resourceDict.TryRemove(id, out var resource))
        {
            _deleteQueue.Enqueue(resource);
        }
    }

    public void EnqueueDelete(Resource resource)
    {
        _deleteQueue.Enqueue(resource);
        RemoveResource(resource.Id);
    }

    public void ProcessDeletes()
    {
        while (_deleteQueue.TryDequeue(out var resource))
        {
            try
            {
                resource.Dispose();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error disposing resource {resource.FilePath}: {ex.Message}");
            }
        }
    }
    
    /// <summary>
    /// Prepares a resource for deferred release (will be released after 2 frames to ensure GPU is done with it)
    /// </summary>
    public void PrepareResourceRelease(Resource resource)
    {
        var currentFrame = Renderer.Instance?.GetRenderFrame() ?? 0;
        var releaseFrame = currentFrame + 2; // Wait 2 frames before releasing
        _releaseQueue.Enqueue((resource, releaseFrame));
    }
    
    /// <summary>
    /// Executes deferred resource releases for resources that are safe to release
    /// Should be called once per frame
    /// </summary>
    public void ResourceReleaseLastExecute()
    {
        var currentFrame = Renderer.Instance?.GetRenderFrame() ?? 0;
        var itemsToRequeue = new List<(Resource resource, ulong releaseFrame)>();
        
        // Process all items in the queue
        while (_releaseQueue.TryDequeue(out var item))
        {
            if (item.releaseFrame <= currentFrame)
            {
                // Safe to release now
                try
                {
                    RemoveResource(item.resource.Id);
                    item.resource.Dispose();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error releasing resource {item.resource.FilePath}: {ex.Message}");
                }
            }
            else
            {
                // Not ready yet, requeue
                itemsToRequeue.Add(item);
            }
        }
        
        // Requeue items that aren't ready
        foreach (var item in itemsToRequeue)
        {
            _releaseQueue.Enqueue(item);
        }
    }

    #endregion

    #region Resource Loading

    private TResource? LoadAsset<TAssetData, TResource>(
        string filePath, 
        string assetTypeName,
        Func<TAssetData, TResource> createResource,
        bool loadDependencies = true)
        where TAssetData : AssetData
        where TResource : Resource
    {
        var stopwatch = Stopwatch.StartNew();
        
        try
        {
            var result = _assetLoader.LoadAsset<TAssetData>(filePath, loadDependencies, progress =>
            {
                
            });

            if (result.IsFailure || result.Value == null)
            {
                Console.WriteLine($"Failed to load {assetTypeName} {filePath}: {result.Error}");
                return null;
            }

            var resource = createResource(result.Value);
            var normalizedPath = filePath.ToLowerInvariant();
            resource.Id = HashPath(normalizedPath);
            resource.FilePath = normalizedPath;
            resource.Name = System.IO.Path.GetFileNameWithoutExtension(filePath);
            resource.State = ResourceState.Loaded;
            resource.LoadTimeMs = stopwatch.ElapsedMilliseconds;

            _resourceDict[resource.Id] = resource;
            
            return resource;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error loading {assetTypeName} {filePath}: {ex.Message}");
            Console.WriteLine(ex.StackTrace);
            return null;
        }
        finally
        {
            stopwatch.Stop();
        }
    }

    private RenderTexture? LoadTexture(string filePath)
    {
        return LoadAsset<TextureData, RenderTexture>(filePath, "Texture", textureData =>
        {
            return TextureMapper.MapToRenderTexture(filePath, textureData);
        }, loadDependencies: false); // Las texturas normalmente no tienen dependencias
    }

    private RenderMesh? LoadMesh(string filePath)
    {
        return LoadAsset<MeshData, RenderMesh>(filePath, "Mesh", meshData =>
        {
            return MeshMapper.MapToRenderMesh(filePath, meshData);
        }, loadDependencies: true); // Los meshes pueden tener materiales/texturas como dependencias
    }

    /*private MaterialResource? LoadMaterial(string filePath)
    {
        return LoadAsset<MaterialData, MaterialResource>(filePath, "Material", materialData =>
        {
            var resource = new MaterialResource { MaterialData = materialData };

            if (materialData.Dependencies != null)
            {
                var textureDeps = materialData.Dependencies
                    .Where(d => d.FilePath?.EndsWith(".tex", StringComparison.OrdinalIgnoreCase) == true);

                foreach (var dep in textureDeps)
                {
                    if (!string.IsNullOrEmpty(dep.FilePath))
                    {
                        var texId = HashPath(dep.FilePath);
                        resource.TextureIds.Add(texId);
                        resource.Dependencies.Add(texId);
                    }
                }
            }

            return resource;
        });
    }*/

    private ShaderResource? LoadShader(string filePath)
    {
        // TODO: Implementar carga de shaders cuando definas el formato
        Console.WriteLine($"Shader loading not yet implemented: {filePath}");
        return null;
    }
    #endregion

    #region Resource Retrieval

    public bool TryGetResource(ulong id, out Resource? resource)
    {
        return _resourceDict.TryGetValue(id, out resource);
    }
    
    /// <summary>
    /// Get all loaded resources of a specific type
    /// </summary>
    public IEnumerable<T> GetAllResources<T>() where T : Resource
    {
        return _resourceDict.Values.OfType<T>();
    }

    public bool TryGetResource(string filePath, out Resource? resource)
    {
        return TryGetResource(HashPath(filePath.ToLowerInvariant()), out resource);
    }

    public T? GetResource<T>(ulong id) where T : Resource
    {
        if (_resourceDict.TryGetValue(id, out var resource) && resource is T typedResource)
        {
            return typedResource;
        }
        return null;
    }

    public T? GetResource<T>(string filePath) where T : Resource
    {
        return GetResource<T>(HashPath(filePath));
    }

    public IEnumerable<T> GetResourcesOfType<T>() where T : Resource
    {
        return _resourceDict.Values.OfType<T>();
    }

    public bool IsResourceLoaded(ulong id)
    {
        return _resourceDict.ContainsKey(id);
    }

    public bool IsResourceLoaded(string filePath)
    {
        return IsResourceLoaded(HashPath(filePath));
    }

    #endregion

    #region Resource Management

    public bool RegisterResource(ulong id, Resource resource)
    {
        return _resourceDict.TryAdd(id, resource);
    }

    public bool RemoveResource(ulong id)
    {
        return _resourceDict.TryRemove(id, out _);
    }

    public void ClearAll()
    {
        foreach (var resource in _resourceDict.Values)
        {
            EnqueueDelete(resource);
        }
        ProcessDeletes();
        
        _resourceDict.Clear();
        
        while (_loadQueue.TryDequeue(out _))
        {
            Interlocked.Decrement(ref _loadQueueCount);
        }
    }

    public string GetStats()
    {
        return $"Resources Loaded: {_resourceDict.Count}, " +
               $"Load Queue: {LoadQueueCount}, " +
               $"Total Loaded: {TotalLoadedResources}, " +
               $"Total Failed: {TotalFailedLoads}";
    }

    #endregion

    #region Helper Methods

    private static ulong HashPath(string path)
    {
        var normalized = path.ToLowerInvariant();
        ulong hash = 0xcbf29ce484222325;
        
        foreach (char c in normalized)
        {
            hash ^= c;
            hash *= 0x100000001b3;
        }
        
        return hash;
    }
    
    // FindAssetEntry ya no es necesario, AssetLoader lo maneja internamente
    
    public static ResourceType GetResourceTypeFromExtension(string filePath)
    {
        var ext = System.IO.Path.GetExtension(filePath).ToLowerInvariant();
        return ext switch
        {
            ".tex" => ResourceType.Texture,
            ".mesh" => ResourceType.Mesh,
            ".mdf2" => ResourceType.Material,
            ".mmtr" => ResourceType.Shader,
            _ => throw new NotSupportedException($"Unsupported file extension: {ext}")
        };
    }

    #endregion

    public void Dispose()
    {
        ClearAll();
    }
}