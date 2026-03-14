using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using Matrix4x4 = System.Numerics.Matrix4x4;
using Vector2 = System.Numerics.Vector2;
using Vector3 = System.Numerics.Vector3;
using Vector4 = System.Numerics.Vector4;

namespace REAssetExplorer.RenderTest2.Assets;

public class RenderMesh : Resource
{
    public override ResourceType Type => ResourceType.Mesh;

    public List<RenderSubMesh> SubMeshes = new();
    public BoundingBox? Bounds;
    public RenderSkeleton? Skeleton;
    public RenderVertex[] Vertices = Array.Empty<RenderVertex>();
    public uint[] Indices = Array.Empty<uint>();

    // Metadata
    public uint VertexStride;
    public bool Use32BitIndices;
    
    // Helpers
    public bool IsSkinned => Skeleton != null;
    public int VertexCount => Vertices.Length;
    public int IndexCount => Indices.Length;
}

public class RenderSubMesh
{
    public uint IndexCount;
    public uint StartIndex;
    public int BaseVertex;
    public RenderMaterial? Material;
}

public class BoundingBox
{
    public Vector3 Min;
    public Vector3 Max;
}

public class RenderSkeleton
{
    public RenderBone[] Bones = Array.Empty<RenderBone>();
    public Matrix4x4[] InverseBindMatrices = Array.Empty<Matrix4x4>();
}

public class RenderBone
{
    public int ParentIndex;
    public Matrix4x4 LocalBindPose;
}

public struct RenderBoneWeight
{
    public Vector4 Indices;
    public Vector4 Weights;
}

[StructLayout(LayoutKind.Sequential)]
public struct RenderVertex
{
    public Vector3 Position;
    public Vector3 Normal;
    public Vector4 Tangent;
    public Vector2 UV0;
    public Vector2 UV1;
    public Vector4 BoneWeights;
    public Vector4 BoneIndices;
}

public class MeshMapper
{
    public static RenderMesh MapToRenderMesh(string name, MeshData meshData)
    {
        var renderMesh = new RenderMesh();

        var vertices = new List<RenderVertex>();
        var indices = new List<uint>();
        var subMeshes = new List<RenderSubMesh>();

        var lod0 = meshData.MeshLayout.MeshBodies[0];
        var materials = meshData.ResolvedDependencies.Values.OfType<MaterialData>().ToArray();
        var materialLookup = BuildMaterialLookup(materials);

        foreach (var part in lod0.Parts)
        {
            foreach (var cluster in part.Clusters)
            {
                uint startIndex = (uint)indices.Count;
                int baseVertex = vertices.Count;

                // ---- VERTICES ----
                int vertexCount = cluster.Positions.Length;

                for (int i = 0; i < vertexCount; i++)
                {
                    RenderVertex v = new RenderVertex
                    {
                        Position = cluster.Positions[i].ToNumerics(),
                        Normal = cluster.Normals != null
                            ? cluster.Normals[i].GetNormal()
                            : Vector3.UnitY,

                        Tangent = cluster.Normals != null
                            ? cluster.Normals[i].GetTangent()
                            : Vector4.UnitX,

                        UV0 = cluster.UV0 != null
                            ? cluster.UV0[i].ToNumerics()
                            : Vector2.Zero,

                        UV1 = cluster.UV1 != null
                            ? cluster.UV1[i].ToNumerics()
                            : Vector2.Zero
                    };

                    if (meshData.IsSkinning && cluster.BoneWeights != null)
                    {
                        var bw = ConvertBoneWeight(cluster.BoneWeights[i]);
                        v.BoneIndices = bw.Indices;
                        v.BoneWeights = bw.Weights;
                    }

                    vertices.Add(v);
                }

                // ---- INDICES ----
                foreach (var tri in cluster.Indices)
                {
                    indices.Add((uint)(tri.A + baseVertex));
                    indices.Add((uint)(tri.B + baseVertex));
                    indices.Add((uint)(tri.C + baseVertex));
                }

                uint indexCount = (uint)(indices.Count - startIndex);

                // ---- SUBMESH ----
                var materialName = meshData.Materials[cluster.MaterialId];
                materialLookup.TryGetValue(materialName, out var materialRef);
                subMeshes.Add(new RenderSubMesh
                {
                    IndexCount = indexCount,
                    StartIndex = startIndex,
                    BaseVertex = 0,
                    Material = MaterialMapper.MapToRenderMaterial(materialName, materialRef.MaterialData, materialRef.MaterialIndex)
                });
            }
        }

        // ---- ASSIGN ----
        renderMesh.SubMeshes = subMeshes;
        renderMesh.Bounds = CreateBounding(meshData);
        renderMesh.Vertices = vertices.ToArray();
        renderMesh.Indices = indices.ToArray();

        renderMesh.VertexStride = (uint)Marshal.SizeOf<RenderVertex>();
        renderMesh.Use32BitIndices = true;

        if (meshData.IsSkinning)
            renderMesh.Skeleton = ConvertSkeleton(meshData);

        return renderMesh;
    }

    private static Dictionary<string, (MaterialData? MaterialData, int MaterialIndex)> BuildMaterialLookup(MaterialData[] materials)
    {
        var lookup = new Dictionary<string, (MaterialData? MaterialData, int MaterialIndex)>(StringComparer.OrdinalIgnoreCase);

        foreach (var materialData in materials)
        {
            for (int i = 0; i < materialData.MaterialHeaders.Length; i++)
            {
                var header = materialData.MaterialHeaders[i];
                lookup[header.MaterialName] = (materialData, i);
            }
        }

        return lookup;
    }
    
    private static BoundingBox CreateBounding(MeshData meshData)
    {
        return new BoundingBox
        {
            Min = new Vector3(
                meshData.MeshLayout.AabbMinX,
                meshData.MeshLayout.AabbMinY,
                meshData.MeshLayout.AabbMinZ),

            Max = new Vector3(
                meshData.MeshLayout.AabbMaxX,
                meshData.MeshLayout.AabbMaxY,
                meshData.MeshLayout.AabbMaxZ)
        };
    }
    
    private static RenderSkeleton ConvertSkeleton(MeshData meshData)
    {
        var skel = new RenderSkeleton();

        int count = (int)meshData.SkeletonLayout.JointCount;

        skel.Bones = new RenderBone[count];
        skel.InverseBindMatrices = 
            meshData.SkeletonLayout.InverseBindMatrices
                .Select(m => m.ToNumerics())
                .ToArray();

        for (int i = 0; i < count; i++)
        {
            skel.Bones[i] = new RenderBone
            {
                ParentIndex = meshData.SkeletonLayout.JointNodes[i].ParentIndex,
                LocalBindPose = meshData.SkeletonLayout.LocalMatrices[i].ToNumerics()
            };
        }

        return skel;
    }
    
    private static RenderBoneWeight ConvertBoneWeight(BoneWeight bw)
    {
        var pairs = new List<(uint index, float weight)>();

        for (int i = 0; i < 8; i++)
        {
            if (bw.boneWeights[i] > 0.0001f)
                pairs.Add((bw.boneIndices[i], bw.boneWeights[i]));
        }

        pairs = pairs
            .OrderByDescending(p => p.weight)
            .Take(4)
            .ToList();

        while (pairs.Count < 4)
            pairs.Add((0, 0));

        float total = pairs.Sum(p => p.weight);
        if (total > 0)
        {
            for (int i = 0; i < pairs.Count; i++)
                pairs[i] = (pairs[i].index, pairs[i].weight / total);
        }

        return new RenderBoneWeight
        {
            Indices = new Vector4(
                pairs[0].index,
                pairs[1].index,
                pairs[2].index,
                pairs[3].index),

            Weights = new Vector4(
                pairs[0].weight,
                pairs[1].weight,
                pairs[2].weight,
                pairs[3].weight)
        };
    }
}