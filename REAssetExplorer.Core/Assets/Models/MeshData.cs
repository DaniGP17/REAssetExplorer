

using System.Collections;
using System.Runtime.InteropServices;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

public class MeshData : AssetData
{
    public MeshHeader Header;
    public MeshLayout MeshLayout;
    public Dictionary<UInt16, String> Strings = new Dictionary<UInt16, String>();
    public Dictionary<UInt16, String> Materials = new Dictionary<UInt16, String>();
    public Dictionary<UInt16, String> Joints = new Dictionary<UInt16, String>();
    public MeshBuffer MeshBuffer;
    public SkeletonLayout SkeletonLayout;
    public BoundingBoxLayout BoundingBoxLayout;
}

public struct MeshHeader
{
    public MeshHeader() { }

    public byte[] Magic { get; set; } = new byte[4];
    public uint Version { get; set; }
    public uint FileSize { get; set; }
    public uint LODGroupHash { get; set; }
    public MeshFlags Flags { get; set; }
    public byte SolvedOffset { get; set; }
    public ushort StringCount { get; set; }
    public LightMapInfo lightMapInfo { get; set; }
    public ulong MeshOffset { get; set; }
    public ulong ShadowMeshOffset { get; set; }
    public ulong OccluderMeshOffset { get; set; }
    public ulong SkeletonOffset { get; set; }
    public ulong NormalRecalculationOffset { get; set; }
    public ulong BlendShapeOffset { get; set; }
    public ulong BoundingBoxOffset { get; set; }
    public ulong BufferOffset { get; set; }
    public ulong GroupPivotOffset { get; set; }
    public ulong MeshClusterNameIndexList { get; set; }
    public ulong JointNameIndexList { get; set; }
    public ulong ChannelNameIndexList { get; set; }
    public ulong StringTabletOffset { get; set; }
}

public struct MeshLayout
{
    public byte LODCount { get; set; }
    public byte TotalClusterCount { get; set; }
    public byte UVCount { get; set; }
    public byte SkinWeightCount { get; set; }
    public UInt16 MaxDrawCallCount { get; set; }
    public byte Has32BitIndexBuffer { get; set; }
    public byte SharedLODBits { get; set; }
    public float boundingX { get; set; }
    public float boundingY { get; set; }
    public float boundingZ { get; set; }
    public float boundingRadius { get; set; }
    public float AabbMinX { get; set; }
    public float AabbMinY { get; set; }
    public float AabbMinZ { get; set; }
    public float padMin { get; set; }
    public float AabbMaxX { get; set; }
    public float AabbMaxY { get; set; }
    public float AabbMaxZ { get; set; }
    public float padMax { get; set; }
    public ulong MeshPointersOffset { get; set; } // The offset to the list of offsets for each mesh(lods)
    public List<ulong> MeshOffsets { get; set; }
    public List<MeshBody> MeshBodies { get; set; }
}

public struct MeshBody
{
    public byte PartCount { get; set; }
    public byte VertexFormat { get; set; }
    public byte[] Reserved { get; set; } // 2 bytes
    public float LODFactor { get; set; }
    public ulong PartsOffsetListOffset { get; set; }
    public List<ulong> PartsOffset { get; set; }
    public List<MeshPart> Parts { get; set; }
}

public struct MeshPart
{
    public MeshPart() { }
    public byte PartId { get; set; }
    public byte ClusterCount { get; set; }
    public byte[] Reserved { get; set; } // 6 bytes
    public uint VertexCount { get; set; }
    public uint IndexCount { get; set; }
    public MeshCluster[] Clusters { get; set; }
}

public struct MeshCluster
{
    public byte MaterialId { get; set; }
    public byte IsQuad { get; set; }
    public byte[] Reserved { get; set; } // 2 bytes
    public uint IndexCount { get; set; }
    public uint StartIndexLocation { get; set; }
    public int BaseVertexLocation { get; set; }
    public int StreamingOffsetBytes { get; set; }
    public int StreamingPlatformSpecificOffsetBytes { get; set; }

    //public Dictionary<VertexElementSlots, VertexData> Vertices;
    public Vector3[] Positions;
    public NormalTangentPacked[] Normals;
    public UV[] UV0;
    public UV[] UV1;
    public BoneWeight[] BoneWeights;
    public MeshIndex[] Indices; // Faces
}

public struct VertexData
{
    public UInt16 ByteStride { get; set; }
    public byte[] Data { get; set; }
}

public struct MeshBuffer
{
    public ulong ElementListOffset { get; set; }
    public ulong VertexBufferOffset { get; set; }
    public ulong IndexBufferOffset { get; set; }
    public uint VertexBufferSize { get; set; }
    public uint IndexBufferSize { get; set; }
    public UInt16 ElementCount { get; set; }
    public UInt16 TotalElementCount { get; set; }
    public uint ShadowMeshIndexBufferOffset { get; set; }
    public uint OccluderMeshIndexBufferOffset { get; set; }
    public uint BlendShapeVertexBufferOffset { get; set; }

    public List<BufferElement> Elements;
    public byte[] VertexBuffer;
    public MeshIndex[] IndexBuffer;
}

public struct SkeletonLayout
{
    public uint JointCount { get; set; }
    public uint JointRemapIndicesCount { get; set; }
    // 8 bytes padding
    public ulong JointNodeOffset { get; set; }
    public ulong LocalMatrixOffset { get; set; }
    public ulong WorldMatrixOffset { get; set; }
    public ulong InverseBindMatrixOffset { get; set; }
    public UInt16[] JointRemapIndices;
    public JointNode[] JointNodes;
    public Matrix4x4[] LocalMatrices;
    public Matrix4x4[] WorldMatrices;
    public Matrix4x4[] InverseBindMatrices;
}

public struct JointNode
{
    public UInt16 Index { get; set; }
    public UInt16 ParentIndex { get; set; }
    public UInt16 SibblingIndex { get; set; }
    public UInt16 ChildIndex { get; set; }
    public UInt16 SymmetryIndex { get; set; }
}

public struct BoundingBoxLayout
{
    public uint BoundingBoxCount { get; set; }
    // padding 4 bytes
    public ulong BoxesOffset { get; set; }
    public AABBData[] Boxes;
}

public struct AABBData
{
    public Vector3 Min;
    public float padMin;
    public Vector3 Max;
    public float padMax;
}

public struct MeshIndex
{
    public UInt16 A;
    public UInt16 B;
    public UInt16 C;
}

public struct VertexBuffer
{
    public byte[] Positions;
    public byte[] Normals;
    public byte[] UV0;
    public byte[] UV1;
    public byte[] Weights;
    public byte[] Colors;
}

public struct BufferElement
{
    public UInt16 InputSlot;
    public UInt16 ByteStride;
    public uint ByteOffset;
}

public enum VertexElementSlots
{
    Position = 0,
    Normal = 1,
    Uv0 = 2,
    Uv1 = 3,
    Weights = 4,
    Color = 5,
}

[Flags]
public enum MeshFlags : byte
{
    None = 0x0,
    IsSkinning = 0x01,
    HasJoint = 0x02,
    HasBlendShape = 0x04,
    HasVertexGroup = 0x08,
    QuadEnable = 0x10,
    StreamingBVH = 0x20,
    HasTertiaryUV = 0x40
}

public readonly struct LightMapInfo
{
    public readonly uint Raw;

    public LightMapInfo(uint raw)
    {
        Raw = raw;
    }

    public bool Reserved => (Raw & 0x1) != 0;
    public bool Used => (Raw & 0x2) != 0;

    public int Width => (int)((Raw >> 2) & 0x7FFF);
    public int Height => (int)((Raw >> 17) & 0x7FFF);
}
