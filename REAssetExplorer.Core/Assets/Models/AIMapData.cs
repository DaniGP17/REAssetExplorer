using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

public class AIMapData : AssetData
{
    public int NameLength { get; set; }
    public string Name { get; set; } = string.Empty;
    public MapType MapType { get; set;  }
    public SectionType SectionType { get; set;  }
    public UInt16 SectionID { get; set;  }
    public ulong AttributeDataOffset { get; set;  }
    public ulong UserDataOffset { get; set;  }
    public ulong SectionBridgeOffset { get; set;  }
    public ulong[] SegmentDataOffset { get; set; } = new ulong[2];
    public ulong[] SegmentRelationOffset { get; set; } = new ulong[2];
    public MapStructure Structure { get; set;  }
    public Guid CreationID { get; set;  }
    public Guid ManagerID { get; set;  }
    public MapSegment[] Segments { get; set; } = new MapSegment[2];
    public List<MapBridgeData> Bridges { get; set; } = new();
}

public struct NodeContentGroupBase
{
    public string TypeName;
    public int Count;
}

public struct MapSegment
{
    public MapSegment() { }
    public Vector4[] Vertices;
    public float MaxNodeDistance;
    public float VerticalDetectionAngle;
    public Node[] Nodes;
    public Link[] Links;
    public uint NodeCount;
    public uint NodeMaxID;
    public uint LinkCount;
    public AABB Boundary;
    public uint[] DivisionTreeLayerNodeCounts = new uint[4];
    public byte[] DivisionTreeLayerNodeBuffer;
    public List<SectionConnectInfo> SectionConnectInfos = new();
    public List<NodeContentGroupBase> ContentGroups = new();
}

public struct SectionConnectInfo
{
    public uint SectionID;
    public List<Link> Links;
}

public struct MapBridgeData
{
    public MapBridgeData() { }
    public Guid ConnectMapID;
    public List<uint> BaseMapDividedNodes;
    public List<uint> ConnectMapDividedNodes;
    public List<Vector3>[] Vertices = new List<Vector3>[2];
    public List<Node>[] Nodes = new List<Node>[2];
    public List<Link>[] BridgeLinkPools = new List<Link>[2];
    public List<Link>[] BaseMapToBridgeLinkPool = new List<Link>[2];
    public List<Link>[] ConnectMapToBridgeLinkPool = new List<Link>[2];
    public AABB[] Boundary = new AABB[2];
}

public class Node
{
    public uint NodeID;
    public uint LocalID;
    public uint ContentGroupIndex;
    public uint ContentID;
    
    public EnumBitSet<NodeSystemAttributes, uint> SystemAttribute;
    public ulong Attribute;
    public uint UserDataIndex;
    public uint LinkCount;
    public Node Parent;
    public List<Node> Children = new();
}

public struct Link
{
    public uint FromNodeID;
    public uint ToNodeID;
    public uint ID;
    public uint PortalID;
    public ulong Attribute;
    public bool IsExtra;
    public bool IsDummy;
}

public enum MapType : byte
{
    NavMesh = 0,
    WayPoint,
    VolumeSpace,
    NoMap
}

public enum SectionType
{
    NoSection = 0,
    Owner,
    Section,
    ConnectManager,
    IndividualSection,
    Invalid = -1
}

public enum MapStructure : byte
{
    Unclassified = 0,
    GroundBase,
    AllSurface
}

public enum NodeSystemAttributes : uint
{
    ModifiedChildren = 0,
    Junction,
    DisableHybridTrace,
    MultiSharedEdge,
    HasExtraLink,
    ConnectExtraLinkBoundary,
    Wall,
    ConnectedWall,
    Num = 32
}