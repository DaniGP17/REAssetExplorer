namespace REAssetExplorer.Core.Assets.Models;

public class SdfData : AssetData
{
    public SdfHeader Header { get; set; }
    public List<ShaderProgramData> ShaderPrograms { get; set; } = new();
}

public struct SdfHeader
{
    public UInt16 VariantCount;
    public UInt16 ProgramCount;
    public UInt64 FileSize;
}

public struct ShaderProgramData
{
    public string name;
    public ulong isPtr;
    public ulong vsPtr;
    public ulong hsPtr;
    public ulong dsPtr;
    public ulong gsPtr;
    public ulong psPtr;
    public ulong csPtr;

    public ulong ILPtr;
    public ulong RSPtr;
    public ulong BSPtr;
    public ulong DSSPtr;

    public ulong inputAttributePtr;

    public ulong constantSlots;
    public ulong constantInfos;

    public ulong samplerSlots;
    public ulong samplerInfos;

    public ulong srvSlots;
    public ulong srvInfos;

    public ulong uavSlots;
    public ulong uavInfos;

    public ulong dummySlots;
    public ulong dummyInfos;

    public uint inputSignatureSize;

    public uint[] shaderSize;
    public uint resourceNum;

    public byte[] constantResourceNum;
    public byte[] samplerResourceNum;
    public byte[] srvResourceNum;
    public byte[] uavResourceNum;
    public byte[] dummyResourceNum;

    public byte constantCount;
    public byte samplerCount;
    public uint inputAttributeCount;

    public byte srvCount;
    public byte uavCount;
    public byte dummyCount;
    public byte optimizeHint;

    public ulong unknown_0x100;
    
    public byte[] VertexShaderData;
    public byte[] HullShaderData;
    public byte[] DomainShaderData;
    public byte[] GeometryShaderData;
    public byte[] PixelShaderData;
    public byte[] ComputeShaderData;
    public List<(ShaderConstantBufferInfo, ShaderSlotHandle)> ConstantBindings;
    public List<(ShaderResourceInfo, ShaderSlotHandle)> SrvBindings;
    public List<(ShaderResourceInfo, ShaderSlotHandle)> SamplerBindings;
    public InputLayoutDesc InputLayout;
}

public struct ShaderConstantBufferInfo
{
    public string Name;
    public uint NameHash;
    public uint ShaderID;
    public bool SceneDependent;
    public uint Size;
    public uint VariableCount;
    public List<ShaderConstantVariableHandle> Variables;
}

public struct ShaderConstantVariableHandle
{
    public string Name;
    public uint NameHash;
    public byte[] Data;
}

public struct ShaderResourceInfo
{
    public string Name;
    public uint NameHash;
    public uint ShaderID;
    public bool SceneDependent;
}

public sealed class ShaderSlotHandle
{
    public byte[] Raw = new byte[8]; // los 8 bytes del handle
    
    public byte StageBit => Raw[6];
    public byte ResourceType => Raw[7];

    public bool IsStageActive(int stage) => (StageBit & (1 << stage)) != 0;
    
    public ushort Slot
    {
        get
        {
            return (ushort)(Raw[4] | (Raw[5] << 8));
        }
    }

    public (int Stage, int Slot, int Space)[] GetActiveBindings()
    {
        var list = new List<(int, int, int)>();

        for (int stage = 0; stage < 6; stage++)
        {
            byte b = Raw[stage];

            int space = b & 0x3;          // bits 0-1
            int slot  = (b >> 2) & 0x3F;  // bits 2-7

            if (slot != 0 || space != 0)
                list.Add((stage, slot, space));
        }

        return list.ToArray();
    }
}

public struct InputLayoutDesc
{
    public uint ElementCount;
    public List<InputElement> Elements;
}

public struct InputElement
{
    public SemanticType SemanticType;
    public byte Format;
    public byte InputSlot;
    public byte SemanticIndex;
    public uint Offset;
    public bool IsInstanceData;
}

[Flags]
public enum ShaderStage : byte
{
    None            = 0x00,
    Vertex          = 0x01,
    Hull            = 0x02,
    Domain          = 0x04,
    Geometry        = 0x08,
    Pixel           = 0x10,
    Compute         = 0x20
}

public enum ShaderResourceType : byte
{
    Sampler             = 0x00,
    Texture2D           = 0x02,
    Texture2DArray      = 0x06,
    StructuredBuffer    = 0x80
}

public enum SemanticType : byte
{
    Position = 0x0,
    Normal = 0x1,
    Binormal = 0x2,
    Tangent = 0x3,
    Texcoord = 0x4,
    Index = 0x5,
    Weight = 0x6,
    Color = 0x7,
    VertexId = 0x8,
    Generic = 0x9,
    InstanceId = 0xA,
    UniqueUv = 0xB,
    TessParam = 0xC,
    GroupId = 0xD
}