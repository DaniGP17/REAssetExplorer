using System;
using System.Collections.Generic;
using System.Linq;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Rendering.Pipeline;
using REAssetExplorer.Rendering.Shaders;
using SharpDX.Direct3D11;
using Buffer = SharpDX.Direct3D11.Buffer;
using SysVector2 = System.Numerics.Vector2;
using SysVector3 = System.Numerics.Vector3;
using SysVector4 = System.Numerics.Vector4;

namespace REAssetExplorer.Rendering.Handlers;

public class SubMesh
{
    public int StartIndex { get; set; }
    public int IndexCount { get; set; }
    public int BaseVertex { get; set; }
    public string MaterialName { get; set; } = string.Empty;
    public byte MaterialID { get; set; }
    public int PartId { get; set; }
    public int LodIndex { get; set; }
}

public class Mesh : IDisposable
{
    public Buffer? VertexBuffer { get; private set; }
    public Buffer? IndexBuffer { get; private set; }
    public int IndexCount { get; private set; }
    public int VertexCount { get; private set; }
    public int VertexStride { get; private set; } // Stride total (para compatibilidad)
    public List<SubMesh> SubMeshes { get; private set; } = new List<SubMesh>();
    public Dictionary<byte, MaterialInstance> Materials { get; private set; } = new Dictionary<byte, MaterialInstance>();
    public MeshRenderState RenderState { get; } = new MeshRenderState();
    private readonly IAssetLoader? _assetLoader;

    public Mesh(D3D11Device device, MeshData meshData, IAssetLoader? assetLoader = null)
    {
        _assetLoader = assetLoader;
        
        if (meshData == null)
        {
            throw new ArgumentNullException(nameof(meshData));
        }
        
        if (meshData.MeshLayout.MeshBodies == null)
        {
            throw new InvalidOperationException("MeshData.MeshLayout.MeshBodies is null");
        }
        
        if (meshData.MeshLayout.MeshBodies.Count == 0)
        {
            throw new InvalidOperationException("MeshData.MeshLayout is missing MeshBodies");
        }
        
        ProcessAllLODs(device, meshData);
        
        LoadMaterials(device, meshData);
    }
    
    private void ProcessAllLODs(D3D11Device device, MeshData meshData)
    {
        var allVertices = new List<VertexPosition>();
        var allIndices = new List<int>();
        
        for (int lodIndex = 0; lodIndex < meshData.MeshLayout.MeshBodies.Count; lodIndex++)
        {
            var meshBody = meshData.MeshLayout.MeshBodies[lodIndex];
            
            if (meshBody.Parts == null)
            {
                continue;
            }
            
            foreach (var meshPart in meshBody.Parts)
            {
                ProcessMeshPart(meshPart, lodIndex, meshData, allVertices, allIndices);
            }
        }
        
        SetVertices(device, allVertices.ToArray(), allIndices.ToArray());
    }
    
    private void ProcessMeshPart(MeshPart meshPart, int lodIndex, MeshData meshData, List<VertexPosition> allVertices, List<int> allIndices)
    {
        if (meshPart.Clusters == null || meshPart.Clusters.Length == 0)
        {
            return;
        }
            
        foreach (var cluster in meshPart.Clusters)
        {
            if (cluster.Positions == null || cluster.Positions.Length == 0)
            {
                continue;
            }
            
            int baseVertexIndex = allVertices.Count;
            int startIndex = allIndices.Count;
            int vertexCount = cluster.Positions.Length;
            
            for (int i = 0; i < vertexCount; i++)
            {
                var position = cluster.Positions[i];
                
                var sysPosition = new SysVector3(position.X, position.Y, position.Z);
                
                SysVector3 normal = SysVector3.UnitY;
                SysVector3 tangent = SysVector3.UnitX;
                if (cluster.Normals != null && i < cluster.Normals.Length)
                {
                    var normalPacked = cluster.Normals[i];
                    normal = new SysVector3(
                        Math.Max(normalPacked.Nx / 127.0f, -1.0f),
                        Math.Max(normalPacked.Ny / 127.0f, -1.0f),
                        Math.Max(normalPacked.Nz / 127.0f, -1.0f)
                    );
                    tangent = new SysVector3(
                        Math.Max(normalPacked.Tx / 127.0f, -1.0f),
                        Math.Max(normalPacked.Ty / 127.0f, -1.0f),
                        Math.Max(normalPacked.Tz / 127.0f, -1.0f)
                    );
                }
                
                SysVector2 texCoord = SysVector2.Zero;
                SysVector2 texCoord2 = SysVector2.Zero;
                
                if (cluster.UV0 != null && i < cluster.UV0.Length)
                {
                    var uv = cluster.UV0[i];
                    texCoord = new SysVector2(uv.U, uv.V);
                }
                
                if (cluster.UV1 != null && i < cluster.UV1.Length && cluster.UV1[0] != null)
                {
                    var uv = cluster.UV1[i];
                    texCoord2 = new SysVector2(uv.U, uv.V);
                }
                
                allVertices.Add(
                    new VertexPosition(
                        sysPosition,
                        new SysVector4(normal.X, normal.Y, normal.Z, 0),
                        new SysVector4(tangent.X, tangent.Y, tangent.Z, 0),
                        texCoord,
                        texCoord2
                    )
                );
            }

            if (cluster.Indices != null)
            {
                foreach (var meshIndex in cluster.Indices)
                {
                    allIndices.Add(baseVertexIndex + meshIndex.A);
                    allIndices.Add(baseVertexIndex + meshIndex.B);
                    allIndices.Add(baseVertexIndex + meshIndex.C);
                }
                
                // Get material name safely
                string materialName = "UnknownMaterial";
                if (meshData.Materials != null && cluster.MaterialId < meshData.Materials.Count)
                {
                    materialName = meshData.Materials[cluster.MaterialId];
                }
                
                SubMeshes.Add(new SubMesh
                {
                    StartIndex = startIndex,
                    IndexCount = cluster.Indices.Length * 3,
                    BaseVertex = 0,
                    MaterialName = materialName,
                    MaterialID = cluster.MaterialId,
                    PartId = meshPart.PartId,
                    LodIndex = lodIndex
                });
            }
        }
    }
    
    public void SetVertices(D3D11Device device, VertexPosition[] vertices, int[] indices)
    {
        VertexCount = vertices.Length;
        IndexCount = indices.Length;
        VertexStride = VertexPosition.SizeInBytes;

        // LOG: First 3 vertices to check bounds
        if (vertices.Length > 0)
        {
            float minX = float.MaxValue, maxX = float.MinValue;
            float minY = float.MaxValue, maxY = float.MinValue;
            float minZ = float.MaxValue, maxZ = float.MinValue;
            
            for (int i = 0; i < Math.Min(vertices.Length, 3); i++)
            {
                var pos = vertices[i].Position;
                minX = Math.Min(minX, pos.X); maxX = Math.Max(maxX, pos.X);
                minY = Math.Min(minY, pos.Y); maxY = Math.Max(maxY, pos.Y);
                minZ = Math.Min(minZ, pos.Z); maxZ = Math.Max(maxZ, pos.Z);
                Console.WriteLine($"[Mesh.SetVertices] Vertex[{i}]: Position=({pos.X:F2}, {pos.Y:F2}, {pos.Z:F2})");
            }
            
            // Calculate full bounds
            for (int i = 3; i < vertices.Length; i++)
            {
                var pos = vertices[i].Position;
                minX = Math.Min(minX, pos.X); maxX = Math.Max(maxX, pos.X);
                minY = Math.Min(minY, pos.Y); maxY = Math.Max(maxY, pos.Y);
                minZ = Math.Min(minZ, pos.Z); maxZ = Math.Max(maxZ, pos.Z);
            }
            
            Console.WriteLine($"[Mesh.SetVertices] Bounds: X=[{minX:F2}, {maxX:F2}], Y=[{minY:F2}, {maxY:F2}], Z=[{minZ:F2}, {maxZ:F2}]");
            Console.WriteLine($"[Mesh.SetVertices] Total: {vertices.Length} vertices, {indices.Length} indices ({indices.Length/3} triangles)");
        }

        VertexBuffer = Buffer.Create(device.Device, BindFlags.VertexBuffer, vertices);
        IndexBuffer = Buffer.Create(device.Device, BindFlags.IndexBuffer, indices);
    }
    
    public void Dispose()
    {
        IndexBuffer?.Dispose();
    }

    public void LoadMaterials(D3D11Device device, MeshData meshData)
    {
        if (meshData.Materials == null || meshData.Materials.Count == 0)
        {
            Console.WriteLine("LoadMaterials: No materials to load, returning");
            return;
        }
        
        var sw = System.Diagnostics.Stopwatch.StartNew();
        
        // Obtener las preferencias de shaders del game provider si está disponible
        IShaderPreferences? shaderPreferences = null;
        if (_assetLoader != null)
        {
            var gameProvider = _assetLoader.GameProvider;
            if (gameProvider?.ShaderPreferences is IShaderPreferences prefs)
            {
                shaderPreferences = prefs;
            }
        }
        
        var materialDataIndex = new Dictionary<string, (MaterialData data, int index, string masterPath)>(StringComparer.OrdinalIgnoreCase);
        
        foreach (var resolvedDep in meshData.ResolvedDependencies.Values)
        {
            if (resolvedDep is MaterialData matData)
            {
                for (int idx = 0; idx < matData.MaterialHeaders.Length; idx++)
                {
                    var matName = matData.MaterialHeaders[idx].MaterialName;
                    var masterPath = matData.MaterialHeaders[idx].MasterMaterialFilePath;
                    materialDataIndex[matName] = (matData, idx, masterPath);
                }
            }
        }
        
        var uniqueSdfPaths = new HashSet<string>();
        for (int i = 0; i < meshData.Materials.Count; i++)
        {
            var materialName = meshData.Materials[(byte)i];
            if (materialDataIndex.TryGetValue(materialName, out var matInfo))
            {
                uniqueSdfPaths.Add(matInfo.masterPath);
            }
        }
        
        var shaderCache = new Dictionary<string, SdfData>();
        
        foreach (var sdfPath in uniqueSdfPaths)
        {
            try
            {
                if (_assetLoader == null)
                {
                    Console.WriteLine($"[LoadMaterials] ✗ AssetLoader not available, cannot load SDF: {sdfPath}");
                    continue;
                }
                
                var sdfAsset = _assetLoader.LoadAsset<SdfData>(sdfPath, loadDependencies: false);
                if (!sdfAsset.IsSuccess || sdfAsset.Value == null)
                {
                    Console.WriteLine($"[LoadMaterials] ✗ Failed to load SDF: {sdfPath}");
                    continue;
                }
                
                var sdfData = sdfAsset.Value;
                shaderCache[sdfPath] = sdfData;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[LoadMaterials] ✗ Error loading SDF {sdfPath}: {ex.Message}");
                Console.WriteLine($"    {ex.StackTrace}");
            }
        }
        
        if (shaderCache.Count == 0)
        {
            Console.WriteLine("[LoadMaterials] ✗ No shaders could be loaded from SDFs");
            return;
        }
        
        var globalTextureIndex = new Dictionary<string, TextureData>(StringComparer.OrdinalIgnoreCase);
        foreach (var resolvedDep in meshData.ResolvedDependencies.Values)
        {
            if (resolvedDep is MaterialData matData)
            {
                foreach (var textureDep in matData.ResolvedDependencies.Values)
                {
                    if (textureDep is TextureData textureData)
                    {
                        var key = System.IO.Path.GetFileNameWithoutExtension(textureData.Name).ToLowerInvariant();
                        if (!globalTextureIndex.ContainsKey(key))
                        {
                            globalTextureIndex[key] = textureData;
                        }
                    }
                }
            }
        }
        
        var materialPrepData = new System.Collections.Concurrent.ConcurrentDictionary<byte, (MaterialData data, int idx, SdfData sdfData, List<(int slot, TextureData? textureData)> textures)>();
        
        System.Threading.Tasks.Parallel.For(0, meshData.Materials.Count, i =>
        {
            var materialName = meshData.Materials[(byte)i];
            
            if (!materialDataIndex.TryGetValue(materialName, out var matInfo))
            {
                Console.WriteLine($"Material not found: {materialName}");
                return;
            }
            
            var (materialData, matIdx, masterMaterialPath) = matInfo;
            
            if (!shaderCache.TryGetValue(masterMaterialPath, out var shaderInfo))
            {
                Console.WriteLine($"Shader not found in cache for: {masterMaterialPath}");
                return;
            }
            
            var sdfData = shaderInfo;
            
            var textureHeaders = materialData.TextureHeaders[matIdx];
            var texturesToLoad = new List<(int slot, TextureData? textureData)>();
            
            for (int j = 0; j < textureHeaders.Length; j++)
            {
                var textureFileName = textureHeaders[j].TextureFilePath.Split('/').Last();
                var textureNameWithoutExt = System.IO.Path.GetFileNameWithoutExtension(textureFileName).ToLowerInvariant();
                
                if (globalTextureIndex.TryGetValue(textureNameWithoutExt, out var textureData))
                {
                    Console.WriteLine("Found texture for material '{0}[{1}]': {2}", materialName, j, textureFileName);
                    texturesToLoad.Add((j, textureData));
                }
                else
                {
                    Console.WriteLine($"Texture not found for material '{materialName}': {textureFileName}");
                    texturesToLoad.Add((j, null));
                }
            }
            
            materialPrepData.TryAdd((byte)i, (materialData, matIdx, sdfData, texturesToLoad));
        });
        
        foreach (var kvp in materialPrepData.OrderBy(x => x.Key))
        {
            var materialId = kvp.Key;
            var (materialData, matIdx, sdfData, texturesToLoad) = kvp.Value;
            var materialName = meshData.Materials[materialId];
            
            var textures = new List<Texture?>();
            
            foreach (var (slot, textureData) in texturesToLoad)
            {
                if (textureData != null)
                {
                    try
                    {
                        var texture = new Texture();
                        texture.LoadFromTextureData(device, textureData);
                        textures.Add(texture);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Failed to load texture {textureData.Name}: {ex.Message}");
                        textures.Add(null);
                    }
                }
                else
                {
                    textures.Add(null);
                }
            }
            
            var materialInstance = new MaterialInstance
            {
                MaterialName = materialName,
                MaterialData = materialData,
                SdfData = sdfData,
                RenderPipeline = new PipelineBuilder(device, shaderPreferences).BuildPipeline(materialData, materialId, sdfData, meshData),
                Textures = textures
            };
            
            // Apply shader-specific properties from MaterialData
            //MaterialPropertyMapper.ApplyProperties(materialInstance, materialData, matIdx, shader.ProfileName);
            
            Materials.Add(materialId, materialInstance);
        }
        
        sw.Stop();
        Console.WriteLine($"Loaded {Materials.Count} materials in {sw.ElapsedMilliseconds}ms");
    }
    
    public List<string> GetMaterialNames()
    {
        var materialNames = new List<string>();
        foreach (var subMesh in SubMeshes)
        {
            if (!materialNames.Contains(subMesh.MaterialName))
            {
                materialNames.Add(subMesh.MaterialName);
            }
        }
        return materialNames;
    }
}