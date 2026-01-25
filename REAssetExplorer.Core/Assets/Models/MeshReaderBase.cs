using REAssetExplorer.Core.Common;
using System.Text;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for RE Engine mesh readers with common parsing logic.
/// </summary>
public abstract class MeshReaderBase : IAssetReader<MeshData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Mesh;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<MeshData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var mesh = new MeshData
            {
                Header = new MeshHeader(),
                MeshLayout = new MeshLayout()
            };

            using var br = new BinaryReader(new MemoryStream(data.ToArray()));

            ReadHeader(br, mesh);
            ReadMeshLayout(br, mesh);
            ReadMeshBodies(br, mesh);
            ReadStrings(br, mesh);
            ReadMaterialNames(br, mesh);
            ReadBuffer(br, mesh);
            ReadVertexData(br, mesh);
            ReadSkeleton(br, mesh);
            ReadBoundingBoxes(br, mesh);
            ReadJointNames(br, mesh);

            return Result<MeshData>.Success(mesh);
        }
        catch (Exception ex)
        {
            return Result<MeshData>.Failure($"Failed to read mesh '{fileName}': {ex}");
        }
    }

    protected virtual void ReadHeader(BinaryReader br, MeshData mesh)
    {
        mesh.Header.Magic = br.ReadBytes(4);
        mesh.Header.Version = br.ReadUInt32();
        mesh.Header.FileSize = br.ReadUInt32();
        mesh.Header.LODGroupHash = br.ReadUInt32();
        mesh.Header.Flags = (MeshFlags)br.ReadByte();
        mesh.Header.SolvedOffset = br.ReadByte();
        mesh.Header.StringCount = br.ReadUInt16();
        mesh.Header.lightMapInfo = new LightMapInfo(br.ReadUInt32());
        mesh.Header.MeshOffset = br.ReadUInt64();
        mesh.Header.ShadowMeshOffset = br.ReadUInt64();
        mesh.Header.OccluderMeshOffset = br.ReadUInt64();
        mesh.Header.SkeletonOffset = br.ReadUInt64();
        mesh.Header.NormalRecalculationOffset = br.ReadUInt64();
        mesh.Header.BlendShapeOffset = br.ReadUInt64();
        mesh.Header.BoundingBoxOffset = br.ReadUInt64();
        mesh.Header.BufferOffset = br.ReadUInt64();
        mesh.Header.GroupPivotOffset = br.ReadUInt64();
        mesh.Header.MeshClusterNameIndexList = br.ReadUInt64();
        mesh.Header.JointNameIndexList = br.ReadUInt64();
        mesh.Header.ChannelNameIndexList = br.ReadUInt64();
        mesh.Header.StringTabletOffset = br.ReadUInt64();
    }

    protected virtual void ReadMeshLayout(BinaryReader br, MeshData mesh)
    {
        if (mesh.Header.MeshOffset == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.MeshOffset, SeekOrigin.Begin);

        mesh.MeshLayout.LODCount = br.ReadByte();
        mesh.MeshLayout.TotalClusterCount = br.ReadByte();
        mesh.MeshLayout.UVCount = br.ReadByte();
        mesh.MeshLayout.SkinWeightCount = br.ReadByte();
        mesh.MeshLayout.MaxDrawCallCount = br.ReadUInt16();
        mesh.MeshLayout.Has32BitIndexBuffer = br.ReadByte();
        mesh.MeshLayout.SharedLODBits = br.ReadByte();
        mesh.MeshLayout.boundingX = br.ReadSingle();
        mesh.MeshLayout.boundingY = br.ReadSingle();
        mesh.MeshLayout.boundingZ = br.ReadSingle();
        mesh.MeshLayout.boundingRadius = br.ReadSingle();
        mesh.MeshLayout.AabbMinX = br.ReadSingle();
        mesh.MeshLayout.AabbMinY = br.ReadSingle();
        mesh.MeshLayout.AabbMinZ = br.ReadSingle();
        mesh.MeshLayout.padMin = br.ReadSingle();
        mesh.MeshLayout.AabbMaxX = br.ReadSingle();
        mesh.MeshLayout.AabbMaxY = br.ReadSingle();
        mesh.MeshLayout.AabbMaxZ = br.ReadSingle();
        mesh.MeshLayout.padMax = br.ReadSingle();
        mesh.MeshLayout.MeshPointersOffset = br.ReadUInt64();

        mesh.MeshLayout.MeshOffsets = new List<ulong>();
        mesh.MeshLayout.MeshBodies = new List<MeshBody>();
        
        br.BaseStream.Seek((long)mesh.MeshLayout.MeshPointersOffset, SeekOrigin.Begin);
        for (int i = 0; i < mesh.MeshLayout.LODCount; i++)
        {
            mesh.MeshLayout.MeshOffsets.Add(br.ReadUInt64());
        }
    }

    protected virtual void ReadMeshBodies(BinaryReader br, MeshData mesh)
    {
        if (mesh.MeshLayout.MeshOffsets == null)
            return;

        for (int i = 0; i < mesh.MeshLayout.MeshOffsets.Count; i++)
        {
            br.BaseStream.Seek((long)mesh.MeshLayout.MeshOffsets[i], SeekOrigin.Begin);
            var meshBody = new MeshBody();

            meshBody.PartCount = br.ReadByte();
            meshBody.VertexFormat = br.ReadByte();
            meshBody.Reserved = br.ReadBytes(2);
            meshBody.LODFactor = br.ReadSingle();
            meshBody.PartsOffsetListOffset = br.ReadUInt64();
            
            br.BaseStream.Seek((long)meshBody.PartsOffsetListOffset, SeekOrigin.Begin);

            meshBody.PartsOffset = new List<ulong>();
            for (int j = 0; j < meshBody.PartCount; j++)
            {
                meshBody.PartsOffset.Add(br.ReadUInt64());
            }

            meshBody.Parts = new List<MeshPart>();
            for (int j = 0; j < meshBody.PartCount; j++)
            {
                br.BaseStream.Seek((long)meshBody.PartsOffset[j], SeekOrigin.Begin);
                var part = ReadMeshPart(br);
                meshBody.Parts.Add(part);
            }

            mesh.MeshLayout.MeshBodies.Add(meshBody);
        }
    }

    protected virtual MeshPart ReadMeshPart(BinaryReader br)
    {
        var part = new MeshPart();
        part.PartId = br.ReadByte();
        part.ClusterCount = br.ReadByte();
        part.Reserved = br.ReadBytes(6);
        part.VertexCount = br.ReadUInt32();
        part.IndexCount = br.ReadUInt32() / 3;

        part.Clusters = new MeshCluster[part.ClusterCount];
        for (int k = 0; k < part.ClusterCount; k++)
        {
            part.Clusters[k] = ReadMeshCluster(br);
        }

        return part;
    }

    protected virtual MeshCluster ReadMeshCluster(BinaryReader br)
    {
        var cluster = new MeshCluster();
        cluster.MaterialId = br.ReadByte();
        cluster.IsQuad = br.ReadByte();
        cluster.Reserved = br.ReadBytes(2);
        cluster.IndexCount = br.ReadUInt32() / 3;
        cluster.StartIndexLocation = br.ReadUInt32() / 3;
        cluster.BaseVertexLocation = br.ReadInt32();
        cluster.StreamingOffsetBytes = br.ReadInt32();
        cluster.StreamingPlatformSpecificOffsetBytes = br.ReadInt32();
        return cluster;
    }

    protected virtual void ReadStrings(BinaryReader br, MeshData mesh)
    {
        mesh.Strings = new Dictionary<ushort, string>();
        for (int i = 0; i < mesh.Header.StringCount; i++)
        {
            mesh.Strings.Add((ushort)i, "\0");
        }

        if (mesh.Header.StringTabletOffset == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.StringTabletOffset, SeekOrigin.Begin);
        for (int i = 0; i < mesh.Header.StringCount; i++)
        {
            ulong stringOffset = br.ReadUInt64();
            long currentPos = br.BaseStream.Position;
            br.BaseStream.Seek((long)stringOffset, SeekOrigin.Begin);
            
            StringBuilder sb = new StringBuilder();
            byte b;
            while ((b = br.ReadByte()) != 0)
            {
                sb.Append((char)b);
            }
            
            mesh.Strings[(ushort)i] = sb.ToString();
            br.BaseStream.Seek(currentPos, SeekOrigin.Begin);
        }
    }

    protected virtual void ReadMaterialNames(BinaryReader br, MeshData mesh)
    {
        if (mesh.Header.MeshClusterNameIndexList == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.MeshClusterNameIndexList, SeekOrigin.Begin);
        for (int i = 0; i < mesh.MeshLayout.TotalClusterCount; i++)
        {
            ushort nameIndex = br.ReadUInt16();
            mesh.Materials.Add(nameIndex, mesh.Strings.ContainsKey(nameIndex) ? mesh.Strings[nameIndex] : "\0");
        }
    }

    protected virtual void ReadBuffer(BinaryReader br, MeshData mesh)
    {
        br.BaseStream.Seek((long)mesh.Header.BufferOffset, SeekOrigin.Begin);

        mesh.MeshBuffer = new MeshBuffer();
        mesh.MeshBuffer.ElementListOffset = br.ReadUInt64();
        mesh.MeshBuffer.VertexBufferOffset = br.ReadUInt64();
        mesh.MeshBuffer.IndexBufferOffset = br.ReadUInt64();
        br.ReadUInt64(); // Offset to unk structure
        mesh.MeshBuffer.VertexBufferSize = br.ReadUInt32();
        mesh.MeshBuffer.IndexBufferSize = br.ReadUInt32();
        mesh.MeshBuffer.ElementCount = br.ReadUInt16();
        mesh.MeshBuffer.TotalElementCount = br.ReadUInt16();
        mesh.MeshBuffer.ShadowMeshIndexBufferOffset = br.ReadUInt32();
        mesh.MeshBuffer.OccluderMeshIndexBufferOffset = br.ReadUInt32();

        br.BaseStream.Seek((long)mesh.MeshBuffer.ElementListOffset, SeekOrigin.Begin);
        mesh.MeshBuffer.Elements = new List<BufferElement>();
        for (int i = 0; i < mesh.MeshBuffer.TotalElementCount; i++)
        {
            var element = new BufferElement();
            element.InputSlot = br.ReadUInt16();
            element.ByteStride = br.ReadUInt16();
            element.ByteOffset = br.ReadUInt32();
            mesh.MeshBuffer.Elements.Add(element);
        }

        br.BaseStream.Seek((long)mesh.MeshBuffer.VertexBufferOffset, SeekOrigin.Begin);
        mesh.MeshBuffer.VertexBuffer = br.ReadBytes((int)mesh.MeshBuffer.VertexBufferSize);

        br.BaseStream.Seek((long)mesh.MeshBuffer.IndexBufferOffset, SeekOrigin.Begin);
        var indexBuffer = br.ReadBytes((int)mesh.MeshBuffer.IndexBufferSize);
        mesh.MeshBuffer.IndexBuffer = new MeshIndex[indexBuffer.Length / 6];
        for (int i = 0; i < mesh.MeshBuffer.IndexBuffer.Length; i++)
        {
            mesh.MeshBuffer.IndexBuffer[i] = new MeshIndex
            {
                A = BitConverter.ToUInt16(indexBuffer, i * 6 + 0),
                B = BitConverter.ToUInt16(indexBuffer, i * 6 + 2),
                C = BitConverter.ToUInt16(indexBuffer, i * 6 + 4),
            };
        }
    }

    protected virtual void ReadVertexData(BinaryReader br, MeshData mesh)
    {
        int globalVertexCount = 0;
        
        if (mesh.MeshLayout.MeshBodies == null)
            return;

        for (int i = 0; i < mesh.MeshLayout.MeshBodies.Count; i++)
        {
            var body = mesh.MeshLayout.MeshBodies[i];
            for (int j = 0; j < body.Parts.Count; j++)
            {
                var part = body.Parts[j];
                for (int k = 0; k < part.Clusters.Length; k++)
                {
                    var cluster = part.Clusters[k];
                    var indices = new MeshIndex[cluster.IndexCount];

                    int vertexCount = (k + 1 < part.Clusters.Length)
                        ? part.Clusters[k + 1].BaseVertexLocation - cluster.BaseVertexLocation
                        : (int)part.VertexCount - (cluster.BaseVertexLocation - globalVertexCount);

                    for (int l = 0; l < cluster.IndexCount; l++)
                    {
                        MeshIndex index = mesh.MeshBuffer.IndexBuffer[cluster.StartIndexLocation + l];
                        indices[l] = index;
                    }
                    part.Clusters[k].Indices = indices;

                    ReadClusterVertices(mesh, cluster, vertexCount);
                }

                globalVertexCount += (int)part.VertexCount;
            }
        }
    }

    protected virtual void ReadClusterVertices(MeshData mesh, MeshCluster cluster, int vertexCount)
    {
        var vertexBuffer = mesh.MeshBuffer.VertexBuffer;
        var slotVertices = new HashSet<VertexElementSlots>();
        var positions = new Vector3[vertexCount];
        var normals = new NormalTangentPacked[vertexCount];
        var uv0 = new UV[vertexCount];
        var uv1 = new UV[vertexCount];
        var boneWeights = new BoneWeight[vertexCount];

        for (int m = 0; m < mesh.MeshBuffer.Elements.Count; m++)
        {
            var element = mesh.MeshBuffer.Elements[m];

            if (slotVertices.Contains((VertexElementSlots)element.InputSlot))
                continue;

            byte[] vertexData = new byte[element.ByteStride * vertexCount];
            for (int n = 0; n < vertexCount; n++)
            {
                Array.Copy(vertexBuffer, (cluster.BaseVertexLocation * element.ByteStride) + element.ByteOffset + element.ByteStride * n, vertexData, element.ByteStride * n, element.ByteStride);
            }

            ProcessVertexElement(element, vertexData, vertexCount, positions, normals, uv0, uv1, boneWeights);
            slotVertices.Add((VertexElementSlots)element.InputSlot);
        }

        cluster.Positions = positions;
        cluster.Normals = normals;
        cluster.UV0 = uv0;
        cluster.UV1 = uv1;
        cluster.BoneWeights = boneWeights;
    }

    protected virtual void ProcessVertexElement(BufferElement element, byte[] vertexData, int vertexCount,
        Vector3[] positions, NormalTangentPacked[] normals, UV[] uv0, UV[] uv1, BoneWeight[] boneWeights)
    {
        switch ((VertexElementSlots)element.InputSlot)
        {
            case VertexElementSlots.Position:
                for (int n = 0; n < vertexCount; n++)
                {
                    positions[n] = new Vector3(
                        BitConverter.ToSingle(vertexData, n * element.ByteStride + 0),
                        BitConverter.ToSingle(vertexData, n * element.ByteStride + 4),
                        BitConverter.ToSingle(vertexData, n * element.ByteStride + 8)
                    );
                }
                break;

            case VertexElementSlots.Normal:
                for (int n = 0; n < vertexCount; n++)
                {
                    normals[n] = new NormalTangentPacked(
                        BitConverter.ToInt64(vertexData, n * element.ByteStride + 0)
                    );
                }
                break;

            case VertexElementSlots.Uv0:
            case VertexElementSlots.Uv1:
                for (int n = 0; n < vertexCount; n++)
                {
                    if (element.InputSlot == (ushort)VertexElementSlots.Uv0)
                    {
                        uv0[n] = new UV(BitConverter.ToInt32(vertexData, n * element.ByteStride));
                    }
                    else
                    {
                        uv1[n] = new UV(BitConverter.ToInt32(vertexData, n * element.ByteStride));
                    }
                }
                break;

            case VertexElementSlots.Weights:
                for (int n = 0; n < vertexCount; n++)
                {
                    boneWeights[n] = new BoneWeight(
                        BitConverter.ToUInt64(vertexData, n * element.ByteStride),
                        BitConverter.ToUInt64(vertexData, n * element.ByteStride + 8)
                    );
                }
                break;

            default:
                Console.WriteLine($"Warning: Unhandled vertex element slot {(VertexElementSlots)element.InputSlot}");
                break;
        }
    }

    protected virtual void ReadSkeleton(BinaryReader br, MeshData mesh)
    {
        if (mesh.Header.SkeletonOffset == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.SkeletonOffset, SeekOrigin.Begin);

        mesh.SkeletonLayout = new SkeletonLayout();
        mesh.SkeletonLayout.JointCount = br.ReadUInt32();
        mesh.SkeletonLayout.JointRemapIndicesCount = br.ReadUInt32();
        br.ReadUInt64(); // 8 bytes padding
        mesh.SkeletonLayout.JointNodeOffset = br.ReadUInt64();
        mesh.SkeletonLayout.LocalMatrixOffset = br.ReadUInt64();
        mesh.SkeletonLayout.WorldMatrixOffset = br.ReadUInt64();
        mesh.SkeletonLayout.InverseBindMatrixOffset = br.ReadUInt64();
        
        mesh.SkeletonLayout.JointRemapIndices = new ushort[mesh.SkeletonLayout.JointRemapIndicesCount];
        for (int i = 0; i < mesh.SkeletonLayout.JointRemapIndicesCount; i++)
        {
            mesh.SkeletonLayout.JointRemapIndices[i] = br.ReadUInt16();
        }

        br.BaseStream.Seek((long)mesh.SkeletonLayout.JointNodeOffset, SeekOrigin.Begin);
        mesh.SkeletonLayout.JointNodes = new JointNode[mesh.SkeletonLayout.JointCount];
        for (int i = 0; i < mesh.SkeletonLayout.JointCount; i++)
        {
            var jointNode = new JointNode();
            jointNode.Index = br.ReadUInt16();
            jointNode.ParentIndex = br.ReadUInt16();
            jointNode.SibblingIndex = br.ReadUInt16();
            jointNode.ChildIndex = br.ReadUInt16();
            jointNode.SymmetryIndex = br.ReadUInt16();
            br.ReadBytes(6); // Padding
            mesh.SkeletonLayout.JointNodes[i] = jointNode;
        }

        br.BaseStream.Seek((long)mesh.SkeletonLayout.LocalMatrixOffset, SeekOrigin.Begin);
        mesh.SkeletonLayout.LocalMatrices = new Matrix4x4[mesh.SkeletonLayout.JointCount];
        for (int i = 0; i < mesh.SkeletonLayout.JointCount; i++)
        {
            mesh.SkeletonLayout.LocalMatrices[i] = br.ReadMatrix4x4();
        }

        br.BaseStream.Seek((long)mesh.SkeletonLayout.WorldMatrixOffset, SeekOrigin.Begin);
        mesh.SkeletonLayout.WorldMatrices = new Matrix4x4[mesh.SkeletonLayout.JointCount];
        for (int i = 0; i < mesh.SkeletonLayout.JointCount; i++)
        {
            mesh.SkeletonLayout.WorldMatrices[i] = br.ReadMatrix4x4();
        }

        br.BaseStream.Seek((long)mesh.SkeletonLayout.InverseBindMatrixOffset, SeekOrigin.Begin);
        mesh.SkeletonLayout.InverseBindMatrices = new Matrix4x4[mesh.SkeletonLayout.JointCount];
        for (int i = 0; i < mesh.SkeletonLayout.JointCount; i++)
        {
            mesh.SkeletonLayout.InverseBindMatrices[i] = br.ReadMatrix4x4();
        }
    }

    protected virtual void ReadBoundingBoxes(BinaryReader br, MeshData mesh)
    {
        if (mesh.Header.BoundingBoxOffset == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.BoundingBoxOffset, SeekOrigin.Begin);
        mesh.BoundingBoxLayout = new BoundingBoxLayout();
        mesh.BoundingBoxLayout.BoundingBoxCount = br.ReadUInt32();
        br.ReadUInt32(); // padding 4 bytes
        mesh.BoundingBoxLayout.BoxesOffset = br.ReadUInt64();
        mesh.BoundingBoxLayout.Boxes = new AABBData[mesh.BoundingBoxLayout.BoundingBoxCount];
        
        br.BaseStream.Seek((long)mesh.BoundingBoxLayout.BoxesOffset, SeekOrigin.Begin);
        for (int i = 0; i < mesh.BoundingBoxLayout.BoundingBoxCount; i++)
        {
            var box = new AABBData();
            box.Min = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
            box.padMin = br.ReadSingle();
            box.Max = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
            box.padMax = br.ReadSingle();
            mesh.BoundingBoxLayout.Boxes[i] = box;
        }
    }

    protected virtual void ReadJointNames(BinaryReader br, MeshData mesh)
    {
        if (mesh.Header.JointNameIndexList == 0)
            return;

        br.BaseStream.Seek((long)mesh.Header.JointNameIndexList, SeekOrigin.Begin);
        for (int i = 0; i < mesh.SkeletonLayout.JointCount; i++)
        {
            ushort nameIndex = br.ReadUInt16();
            mesh.Joints.Add(nameIndex, mesh.Strings.ContainsKey(nameIndex) ? mesh.Strings[nameIndex] : "\0");
        }
    }
}
