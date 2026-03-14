using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for RE Engine material readers (.mdf2) with common parsing logic.
/// </summary>
public abstract class MaterialReaderBase : IAssetReader<MaterialData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Material;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<MaterialData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var mat = new MaterialData();
            
            using var br = new BinaryReader(new MemoryStream(data.ToArray()));
            
            // ---- HEADER ----
            mat.Magic = br.ReadBytes(4);
            mat.Version = br.ReadUInt16();
            mat.MaterialCount = br.ReadUInt16();
            
            // Padding (8 bytes) to align to 16 bytes
            br.ReadBytes(8);
            
            mat.MaterialHeaders = new MaterialHeaderData[mat.MaterialCount];
            for (int i = 0; i < mat.MaterialCount; i++)
            {
                mat.MaterialHeaders[i] = new MaterialHeaderData()
                {
                    NameOffset = br.ReadUInt64(),
                    NameHash = br.ReadUInt32(),
                    PropertyDataBlockSize = br.ReadUInt32(),
                    PropertyCount = br.ReadUInt32(),
                    TextureCount = br.ReadUInt32()
                };

                br.ReadUInt64();

                mat.MaterialHeaders[i].ShaderType = (ShaderType)br.ReadUInt32();
                mat.MaterialHeaders[i].Flags = (MaterialFlags)br.ReadUInt32();
                mat.MaterialHeaders[i].PropertyHeadersOffset = br.ReadUInt64();
                mat.MaterialHeaders[i].TextureHeadersOffset = br.ReadUInt64();
                mat.MaterialHeaders[i].FirstMaterialNameOffset = br.ReadUInt64();
                mat.MaterialHeaders[i].PropertiesDataBlockOffset = br.ReadUInt64();
                mat.MaterialHeaders[i].MasterMaterialFilePathOffset = br.ReadUInt64();
                
                long currentPos = br.BaseStream.Position;
                // Read material name
                br.BaseStream.Position = (long)mat.MaterialHeaders[i].NameOffset;
                mat.MaterialHeaders[i].MaterialName = StringUtil.ReadNullTerminatedString(br);
                // Read master material file path
                br.BaseStream.Position = (long)mat.MaterialHeaders[i].MasterMaterialFilePathOffset;
                mat.MaterialHeaders[i].MasterMaterialFilePath = StringUtil.ReadNullTerminatedString(br);
                br.BaseStream.Position = currentPos;
            }
            
            // Read textures for all materials
            mat.TextureHeaders = new MaterialTextureHeader[mat.MaterialCount][];
            for (int i = 0; i < mat.MaterialCount; i++)
            {
                uint textureCount = mat.MaterialHeaders[i].TextureCount;
                mat.TextureHeaders[i] = new MaterialTextureHeader[textureCount];

                for (int j = 0; j < textureCount; j++)
                {
                    mat.TextureHeaders[i][j] = new MaterialTextureHeader()
                    {
                        TextureTypeOffset = br.ReadUInt64(),
                        Utf16TextureTypeHash = br.ReadUInt32(),
                        AsciiTextureTypeHash = br.ReadUInt32(),
                        TextureFilePathOffset = br.ReadUInt64()
                    };
                    
                    br.ReadUInt64(); // Padding (8 bytes) to align to 16 bytes
                    long currentPos = br.BaseStream.Position;
                    // Read texture type
                    br.BaseStream.Position = (long)mat.TextureHeaders[i][j].TextureTypeOffset;
                    mat.TextureHeaders[i][j].TextureType = StringUtil.ReadNullTerminatedString(br);
                    // Read texture file path
                    br.BaseStream.Position = (long)mat.TextureHeaders[i][j].TextureFilePathOffset;
                    mat.TextureHeaders[i][j].TextureFilePath = StringUtil.ReadNullTerminatedString(br);
                    br.BaseStream.Position = currentPos;
                }
            }
            
            // Read property headers for all materials
            mat.PropertyHeaders = new MaterialPropertyHeader[mat.MaterialCount][];
            for (int i = 0; i < mat.MaterialCount; i++)
            {
                uint propertyCount = mat.MaterialHeaders[i].PropertyCount;
                mat.PropertyHeaders[i] = new MaterialPropertyHeader[propertyCount];

                for (int j = 0; j < propertyCount; j++)
                {
                    mat.PropertyHeaders[i][j] = new MaterialPropertyHeader()
                    {
                        NameOffset = br.ReadUInt64(),
                        NameUtf16Hash = br.ReadUInt32(),
                        NameAsciiHash = br.ReadUInt32(),
                        DataOffset = br.ReadUInt32(),
                        ParameterCount = br.ReadUInt32()
                    };
                    long currentPos = br.BaseStream.Position;
                    // Read property name
                    br.BaseStream.Position = (long)mat.PropertyHeaders[i][j].NameOffset;
                    mat.PropertyHeaders[i][j].Name = StringUtil.ReadNullTerminatedString(br);
                    
                    // Read parameters
                    mat.PropertyHeaders[i][j].Parameters = new float[mat.PropertyHeaders[i][j].ParameterCount];
                    br.BaseStream.Position = (long)(mat.MaterialHeaders[i].PropertiesDataBlockOffset + mat.PropertyHeaders[i][j].DataOffset);
                    for (int k = 0; k < mat.PropertyHeaders[i][j].ParameterCount; k++)
                    {
                        mat.PropertyHeaders[i][j].Parameters[k] = br.ReadSingle();
                    }
                    
                    br.BaseStream.Position = currentPos;
                }
            }
            
            // Read property data blocks for all materials
            

            return Result<MaterialData>.Success(mat);
        }
        catch (Exception ex)
        {
            return Result<MaterialData>.Failure($"Failed to read material '{fileName}': {ex.Message}");
        }
    }
    
    public bool ResolveDependencies(MaterialData asset) => true;
}
