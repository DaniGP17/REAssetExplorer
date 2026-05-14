using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.Interop.Structures;

namespace REAssetExplorer.RenderTest2.Core;

public class ResourceManager : IDisposable
{
    private readonly AssetLoader _assetLoader;
    private readonly ConcurrentDictionary<ulong, Resource> _resources = new();
    private readonly ConcurrentQueue<(Resource resource, ulong releaseFrame)> _releaseQueue = new();

    public AssetLoader AssetLoader => _assetLoader;

    public ResourceManager(Dictionary<string, PakFile> pakFiles, IGameProvider gameProvider)
    {
        _assetLoader = new AssetLoader(
            gameProvider ?? throw new ArgumentNullException(nameof(gameProvider)),
            pakFiles ?? throw new ArgumentNullException(nameof(pakFiles)));
    }

    /// <summary>
    /// Loads and registers a GPU resource. Returns the cached resource if already loaded.
    /// </summary>
    public Resource? Load(string filePath, ResourceType type, bool loadDependencies = true)
    {
        var id = HashPath(filePath);
        if (_resources.TryGetValue(id, out var cached))
            return cached;

        return type switch
        {
            ResourceType.Texture => LoadTexture(filePath),
            ResourceType.Mesh    => LoadMesh(filePath, loadDependencies),
            _                    => null
        };
    }

    private RenderTexture? LoadTexture(string filePath)
    {
        var result = _assetLoader.LoadAsset<TextureData>(filePath, loadDependencies: false);
        if (result.IsFailure || result.Value == null)
        {
            Console.WriteLine($"Failed to load texture {filePath}: {result.Error}");
            return null;
        }
        return Register(filePath, TextureMapper.MapToRenderTexture(filePath, result.Value));
    }

    private RenderMesh? LoadMesh(string filePath, bool loadDependencies)
    {
        var result = _assetLoader.LoadAsset<MeshData>(filePath, loadDependencies);
        if (result.IsFailure || result.Value == null)
        {
            Console.WriteLine($"Failed to load mesh {filePath}: {result.Error}");
            return null;
        }
        return Register(filePath, MeshMapper.MapToRenderMesh(filePath, result.Value));
    }

    /// <summary>
    /// Loads a mesh and injects a specific material (with its texture dependencies) into it.
    /// Returns cached RenderMesh if the path was already loaded.
    /// </summary>
    public RenderMesh? LoadMeshWithMaterial(string meshPath, string mdfPath)
    {
        var id = HashPath(meshPath);
        if (_resources.TryGetValue(id, out var cached))
            return cached as RenderMesh;

        var meshResult = _assetLoader.LoadAsset<MeshData>(meshPath, loadDependencies: false);
        if (meshResult.IsFailure || meshResult.Value == null)
        {
            Console.WriteLine($"Failed to load mesh {meshPath}: {meshResult.Error}");
            return null;
        }

        var meshData = meshResult.Value;

        if (!string.IsNullOrEmpty(mdfPath))
        {
            var matResult = _assetLoader.LoadAsset<MaterialData>(mdfPath, loadDependencies: true);
            if (matResult.IsSuccess && matResult.Value != null)
                meshData.ResolvedDependencies[mdfPath] = matResult.Value;
        }

        return Register(meshPath, MeshMapper.MapToRenderMesh(meshPath, meshData));
    }

    private T Register<T>(string filePath, T resource) where T : Resource
    {
        var normalized = filePath.ToLowerInvariant();
        resource.Id = HashPath(normalized);
        resource.FilePath = normalized;
        resource.Name = Path.GetFileNameWithoutExtension(filePath);
        resource.State = ResourceState.Loaded;
        _resources[resource.Id] = resource;
        return resource;
    }

    public T? GetResource<T>(ulong id) where T : Resource =>
        _resources.TryGetValue(id, out var r) && r is T t ? t : null;

    public T? GetResource<T>(string filePath) where T : Resource =>
        GetResource<T>(HashPath(filePath));

    public bool TryGetResource(ulong id, out Resource? resource) =>
        _resources.TryGetValue(id, out resource);

    public IEnumerable<T> GetAllResources<T>() where T : Resource =>
        _resources.Values.OfType<T>();

    /// <summary>
    /// Schedules a resource for release after 2 frames to ensure the GPU has finished using it.
    /// </summary>
    public void PrepareResourceRelease(Resource resource)
    {
        var frame = Renderer.Instance?.GetRenderFrame() ?? 0;
        _resources.TryRemove(resource.Id, out _);
        _releaseQueue.Enqueue((resource, frame + 2));
    }

    /// <summary>
    /// Releases resources whose frame delay has elapsed. Call once per frame.
    /// </summary>
    public void ResourceReleaseLastExecute()
    {
        var frame = Renderer.Instance?.GetRenderFrame() ?? 0;
        var deferred = new List<(Resource, ulong)>();

        while (_releaseQueue.TryDequeue(out var item))
        {
            if (item.releaseFrame <= frame)
                item.resource.Dispose();
            else
                deferred.Add(item);
        }

        foreach (var item in deferred)
            _releaseQueue.Enqueue(item);
    }

    public static ResourceType GetResourceTypeFromExtension(string filePath)
    {
        var ext = Path.GetExtension(filePath).ToLowerInvariant();
        return ext switch
        {
            ".tex"  => ResourceType.Texture,
            ".mesh" => ResourceType.Mesh,
            ".mdf2" => ResourceType.Material,
            ".mmtr" => ResourceType.Shader,
            _       => throw new NotSupportedException($"Unsupported file extension: {ext}")
        };
    }

    private static ulong HashPath(string path)
    {
        ulong hash = 0xcbf29ce484222325;
        foreach (char c in path.ToLowerInvariant())
        {
            hash ^= c;
            hash *= 0x100000001b3;
        }
        return hash;
    }

    public void Dispose()
    {
        foreach (var r in _resources.Values)
            r.Dispose();
        _resources.Clear();

        while (_releaseQueue.TryDequeue(out var item))
            item.resource.Dispose();
    }
}
