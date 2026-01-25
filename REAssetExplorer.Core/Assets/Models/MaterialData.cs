namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Represents material definition data.
/// </summary>
/// <param name="Name">The name of the material.</param>
/// <param name="ShaderName">The shader used by this material.</param>
/// <param name="TextureReferences">Texture file paths referenced by this material.</param>
/// <param name="Properties">Material properties (key-value pairs).</param>
public class MaterialData : AssetData
{
    public byte[] Magic { get; set; } = new byte[4];
    public uint Version { get; set; }
    public uint MaterialCount { get; set; }
    // Padding (8 bytes) to align to 16 bytes
    public MaterialHeaderData[] MaterialHeaders { get; set; } = Array.Empty<MaterialHeaderData>();
    
    // Textures: Array of arrays, one per material
    // TextureHeaders[materialIndex][textureIndex]
    public MaterialTextureHeader[][] TextureHeaders { get; set; } = Array.Empty<MaterialTextureHeader[]>();
    
    // Properties: Array of arrays, one per material
    // PropertyHeaders[materialIndex][propertyIndex]
    public MaterialPropertyHeader[][] PropertyHeaders { get; set; } = Array.Empty<MaterialPropertyHeader[]>();
}

public struct MaterialTextureHeader
{
    public ulong TextureTypeOffset;
    public uint Utf16TextureTypeHash;
    public uint AsciiTextureTypeHash;
    public ulong TextureFilePathOffset;
    // Padding (8 bytes) to align to 16 bytes
    public string TextureType;
    public string TextureFilePath;
}

public struct MaterialPropertyHeader
{
    public ulong NameOffset;
    public uint NameUtf16Hash;
    public uint NameAsciiHash;
    public uint DataOffset;
    public uint ParameterCount;
    public string Name;
    public float[] Parameters;
}

public struct MaterialHeaderData
{
    public ulong NameOffset;
    public uint NameHash;
    public uint PropertyDataBlockSize;
    public uint PropertyCount;
    public uint TextureCount;
    public ShaderType ShaderType;
    public MaterialFlags Flags;
    public ulong PropertyHeadersOffset;
    public ulong TextureHeadersOffset;
    public ulong FirstMaterialNameOffset;
    public ulong PropertiesDataBlockOffset;
    public ulong MasterMaterialFilePathOffset;
    public string MaterialName;
    public string MasterMaterialFilePath;
}

public enum ShaderType : uint
{
    Standard = 0x0,
    Decal = 0x1,
    DecalWithMetallic = 0x2,
    DecalNrmr = 0x3,
    Transparent = 0x4,
    Distortion = 0x5,
    PrimitiveMesh = 0x6,
    PrimitiveSolidMesh = 0x7,
    Water = 0x8,
    SpeedTree = 0x9,
    Gui = 0xA,
    GuiMesh = 0xB,
    GuiMeshTransparent = 0xC,
    ExpensiveTransparent = 0xD,
    Forward = 0xE,
    RenderTarget = 0xF,
    PostProcess = 0x10,
    PrimitiveMaterial = 0x11,
    PrimitiveSolidMaterial = 0x12,
    SpineMaterial = 0x13,
    Max = 0x14
}

[Flags]
public enum MaterialFlags : uint
{
    BaseTwoSideEnable        = 1u << 0,   // bit 0
    BaseAlphaTestEnable      = 1u << 1,   // bit 1
    ShadowCastDisable        = 1u << 2,   // bit 2
    VertexShaderUsed         = 1u << 3,   // bit 3
    EmissiveUsed             = 1u << 4,   // bit 4
    TessellationEnable       = 1u << 5,   // bit 5
    EnableIgnoreDepth        = 1u << 6,   // bit 6
    AlphaMaskUsed            = 1u << 7,   // bit 7

    ForcedTwoSideEnable      = 1u << 8,   // bit 8
    TwoSideEnable            = 1u << 9,   // bit 9

    RoughTransparentEnable   = 1u << 24, // byte3 bit0
    ForcedAlphaTestEnable    = 1u << 25,
    AlphaTestEnable          = 1u << 26,
    SssProfileUsed           = 1u << 27,
    EnableStencilPriority    = 1u << 28,
    RequireDualQuaternion    = 1u << 29,
    PixelDepthOffsetUsed     = 1u << 30,
    NoRayTracing             = 1u << 31
}