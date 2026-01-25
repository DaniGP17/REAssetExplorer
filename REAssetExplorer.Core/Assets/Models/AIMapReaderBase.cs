using REAssetExplorer.Core.Common;
using System.Text;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for RE Engine AI map readers with common parsing logic.
/// </summary>
public abstract class AIMapReaderBase : IAssetReader<AIMapData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Material;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<AIMapData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var aiMap = new AIMapData();
            using var br = new BinaryReader(new MemoryStream(data.ToArray()));

            ReadHeader(br, aiMap);
            ReadSectionBridge(br, aiMap);
            ReadSegments(br, aiMap);

            return Result<AIMapData>.Success(aiMap);
        }
        catch (Exception ex)
        {
            return Result<AIMapData>.Failure($"Failed to read AI map '{fileName}': {ex}");
        }
    }

    protected virtual void ReadHeader(BinaryReader br, AIMapData aiMap)
    {
        br.BaseStream.Seek(0x4, SeekOrigin.Begin);

        aiMap.NameLength = br.ReadInt32();
        aiMap.Name = br.ReadFixedString(aiMap.NameLength * 2, Encoding.Unicode);
        br.Align(4);

        aiMap.MapType = (MapType)br.ReadByte();
        aiMap.SectionType = (SectionType)br.ReadByte();
        aiMap.SectionID = br.ReadUInt16();

        aiMap.CreationID = new Guid(br.ReadBytes(16));

        aiMap.Structure = (MapStructure)br.ReadByte();
        br.ReadBytes(3); // padding

        aiMap.AttributeDataOffset = br.ReadUInt64();
        aiMap.UserDataOffset = br.ReadUInt64();
        aiMap.SectionBridgeOffset = br.ReadUInt64();

        aiMap.SegmentDataOffset[0] = br.ReadUInt64();
        aiMap.SegmentRelationOffset[0] = br.ReadUInt64();
        aiMap.SegmentDataOffset[1] = br.ReadUInt64();
        aiMap.SegmentRelationOffset[1] = br.ReadUInt64();
    }

    protected virtual void ReadSectionBridge(BinaryReader br, AIMapData aiMap)
    {
        if (aiMap.SectionType != SectionType.IndividualSection)
            return;

        br.BaseStream.Seek((long)aiMap.SectionBridgeOffset, SeekOrigin.Begin);
        aiMap.ManagerID = new Guid(br.ReadBytes(16));
    }

    protected virtual void ReadSegments(BinaryReader br, AIMapData aiMap)
    {
        for (int i = 0; i < 2; i++)
        {
            if (aiMap.SegmentDataOffset[i] == 0)
                continue;

            if (aiMap.SectionType == SectionType.IndividualSection)
            {
                throw new NotImplementedException("Reading individual section AI maps is not implemented yet.");
            }

            br.BaseStream.Seek((long)aiMap.SegmentDataOffset[i], SeekOrigin.Begin);
            aiMap.Segments[i] = ReadMapSegment(br, aiMap, i);
        }
    }

    protected virtual MapSegment ReadMapSegment(BinaryReader br, AIMapData aiMap, int segmentIdx)
    {
        var segment = new MapSegment();

        ReadMapSegmentBody(br, ref segment);

        segment.SectionConnectInfos = new List<SectionConnectInfo>();
        int connectSectionNum = br.ReadInt32();
        for (int i = 0; i < connectSectionNum; i++)
        {
            var connectInfo = new SectionConnectInfo();
            connectInfo.SectionID = br.ReadUInt32();
            int linkNum = br.ReadInt32();
            connectInfo.Links = new List<Link>();
            segment.SectionConnectInfos.Add(connectInfo);
        }

        for (int j = 0; j < segment.SectionConnectInfos.Count; j++)
        {
            var link = new Link();
            link.PortalID = br.ReadUInt32();
            link.FromNodeID = br.ReadUInt32();
            link.ToNodeID = br.ReadUInt32();
            link.ID = br.ReadUInt32();
            link.Attribute = br.ReadUInt64();

            uint extraInfo = br.ReadUInt32();
            link.IsExtra = (extraInfo & 0x8000_0000) != 0;
            link.IsDummy = (extraInfo & 0x4000_0000) != 0;

            segment.SectionConnectInfos[j].Links.Add(link);
        }

        ReadSectionInformation(br, ref segment);

        return segment;
    }

    protected virtual void ReadMapSegmentBody(BinaryReader br, ref MapSegment seg)
    {
        uint contentGroupCount = br.ReadUInt32();

        for (int i = 0; i < contentGroupCount; i++)
        {
            uint nameLen = br.ReadUInt32();
            string className = br.ReadFixedString((int)nameLen * 2, Encoding.Unicode);
            br.Align(4);

            uint contentCount = br.ReadUInt32();
            SkipContentGroup(br, className, (int)contentCount);

            NodeContentGroupBase group = new NodeContentGroupBase();
            group.TypeName = className;
            group.Count = (int)contentCount;

            seg.ContentGroups.Add(group);
        }

        long afterGroups = br.BaseStream.Position;

        uint portalShapeGroupCount = br.ReadUInt32();
        uint vertexCount = br.ReadUInt32();

        seg.Vertices = new Vector4[vertexCount];
        for (int i = 0; i < vertexCount; i++)
        {
            seg.Vertices[i] = new Vector4(
                br.ReadSingle(),
                br.ReadSingle(),
                br.ReadSingle(),
                br.ReadSingle()
            );
        }

        uint nodeCount = br.ReadUInt32();
        seg.NodeMaxID = br.ReadUInt32();

        seg.NodeCount = nodeCount;
        if (seg.NodeCount == 0)
            seg.NodeMaxID = 0;
        seg.Nodes = new Node[nodeCount];

        for (uint i = 0; i < nodeCount; i++)
        {
            var node = new Node();

            node.LocalID = br.ReadUInt32();

            uint contentGroup = br.ReadUInt32();
            uint contentID = br.ReadUInt32();

            node.ContentGroupIndex = contentGroup;
            node.ContentID = contentID;

            node.SystemAttribute = new EnumBitSet<NodeSystemAttributes, uint>(32, br.ReadUInt32());
            node.Attribute = br.ReadUInt64();
            node.UserDataIndex = br.ReadUInt32();
            node.LinkCount = br.ReadUInt32();

            seg.Nodes[i] = node;
        }

        uint linkCount = br.ReadUInt32();
        seg.LinkCount = linkCount;
        seg.Links = new Link[linkCount];

        for (uint i = 0; i < linkCount; i++)
        {
            var link = new Link();

            link.ID = br.ReadUInt32();
            link.FromNodeID = br.ReadUInt32();
            link.ToNodeID = br.ReadUInt32();
            link.PortalID = br.ReadUInt32();
            link.Attribute = br.ReadUInt64();

            uint extraInfo = br.ReadUInt32();
            link.IsExtra = (extraInfo & 0x8000_0000) != 0;
            link.IsDummy = (extraInfo & 0x4000_0000) != 0;

            seg.Links[i] = link;
        }
    }

    protected virtual void ReadSectionInformation(BinaryReader br, ref MapSegment seg)
    {
        seg.MaxNodeDistance = br.ReadSingle();
        seg.VerticalDetectionAngle = br.ReadSingle();

        Vector3 boundaryMin = new Vector3(br);
        br.ReadBytes(4);
        Vector3 boundaryMax = new Vector3(br);
        br.ReadBytes(4);
        seg.Boundary = new AABB(boundaryMin, boundaryMax);

        int treeLayerCount = 0;
        for (int layer = 0; layer < 4; layer++)
        {
            seg.DivisionTreeLayerNodeCounts[layer] = br.ReadUInt32();
            treeLayerCount += (int)seg.DivisionTreeLayerNodeCounts[layer];
        }

        if (treeLayerCount > 0)
        {
            seg.DivisionTreeLayerNodeBuffer = br.ReadBytes(treeLayerCount * 4);
        }
    }

    protected virtual void SkipContentGroup(BinaryReader br, string typeName, int count)
    {
        if (typeName.EndsWith("ContentGroupTriangle"))
        {
            br.BaseStream.Seek(count * 28L, SeekOrigin.Current);
            return;
        }

        if (typeName.EndsWith("ContentGroupPolygon"))
        {
            for (int i = 0; i < count; i++)
            {
                uint vertexNum = br.ReadUInt32();
                br.BaseStream.Seek(4L * vertexNum, SeekOrigin.Current);
                uint dwords = (vertexNum + 3u) >> 2;
                br.BaseStream.Seek(4L * dwords, SeekOrigin.Current);
                br.BaseStream.Seek(4L * vertexNum, SeekOrigin.Current);
                br.BaseStream.Seek(12 + 12, SeekOrigin.Current);
            }
            return;
        }

        if (typeName.EndsWith("ContentGroupMapPoint"))
        {
            for (int i = 0; i < count; i++)
            {
                br.ReadBytes(24);
            }
            return;
        }

        if (typeName.EndsWith("ContentGroupMapAABB"))
        {
            for (int i = 0; i < count; i++)
            {
                br.ReadBytes(8);
                br.ReadBytes(8 * 3);
                br.ReadBytes(8 * 3);
                br.ReadBytes(4);
            }
            return;
        }

        throw new NotSupportedException($"Unknown ContentGroup '{typeName}', cannot skip safely.");
    }
}
