using System.Runtime.InteropServices;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>
/// Base class for RE Engine shader definition file readers (.sdf/.mmtr) with common parsing logic.
/// </summary>
public abstract class SdfReaderBase : IAssetReader<SdfData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Material;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<SdfData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var sdf = new SdfData();
            
            using var br = new BinaryReader(new MemoryStream(data.ToArray()));
            
            br.ReadBytes(4);
            
            sdf.Header = new SdfHeader
            {
                VariantCount = br.ReadUInt16(),
                ProgramCount = br.ReadUInt16(),
                FileSize = br.ReadUInt64()
            };
            
            const int EntrySize = 0x108;
            const long TableOffset = 0x10;

            int selectedVariant = 1;

            // This may be different in each game
            /*if (sdf.Header.VariantCount > 1)
            {
                selectedVariant = 1; // Force Gen2(DX11)
            }*/

            long variantOffset = TableOffset + (long)selectedVariant * sdf.Header.ProgramCount * EntrySize;
            br.BaseStream.Seek(variantOffset, SeekOrigin.Begin);

            sdf.ShaderPrograms = new List<ShaderProgramData>(sdf.Header.ProgramCount);

            for (int i = 0; i < sdf.Header.ProgramCount; i++)
            {
                var program = ReadShaderProgramData(br);
                sdf.ShaderPrograms.Add(program);
            }


            return Result<SdfData>.Success(sdf);
        }
        catch (Exception ex)
        {
            return Result<SdfData>.Failure($"Failed to read sdf '{fileName}': {ex}");
        }
    }
    
    private static ShaderProgramData ReadShaderProgramData(BinaryReader br)
    {
        ShaderProgramData p = new ShaderProgramData();

        p.name = br.ReadNullTerminatedString(br.ReadInt64());
        p.isPtr   = br.ReadUInt64();
        p.vsPtr   = br.ReadUInt64();
        p.hsPtr   = br.ReadUInt64();
        p.dsPtr   = br.ReadUInt64();
        p.gsPtr   = br.ReadUInt64();
        p.psPtr   = br.ReadUInt64();
        p.csPtr   = br.ReadUInt64();

        p.ILPtr   = br.ReadUInt64();
        p.RSPtr   = br.ReadUInt64();
        p.BSPtr   = br.ReadUInt64();
        p.DSSPtr  = br.ReadUInt64();

        p.inputAttributePtr = br.ReadUInt64();

        p.constantSlots = br.ReadUInt64();
        p.constantInfos = br.ReadUInt64();

        p.samplerSlots  = br.ReadUInt64();
        p.samplerInfos  = br.ReadUInt64();

        p.srvSlots   = br.ReadUInt64();
        p.srvInfos   = br.ReadUInt64();

        p.uavSlots   = br.ReadUInt64();
        p.uavInfos   = br.ReadUInt64();

        p.dummySlots = br.ReadUInt64();
        p.dummyInfos = br.ReadUInt64();

        p.inputSignatureSize = br.ReadUInt32();

        p.shaderSize = new uint[6];
        for (int i = 0; i < 6; i++)
            p.shaderSize[i] = br.ReadUInt32();

        p.resourceNum = br.ReadUInt32();

        p.constantResourceNum = br.ReadBytes(6);
        p.samplerResourceNum  = br.ReadBytes(6);
        p.srvResourceNum      = br.ReadBytes(6);
        p.uavResourceNum      = br.ReadBytes(6);
        p.dummyResourceNum    = br.ReadBytes(6);

        p.constantCount = br.ReadByte();
        p.samplerCount  = br.ReadByte();
        p.inputAttributeCount = br.ReadUInt32();

        p.srvCount   = br.ReadByte();
        p.uavCount   = br.ReadByte();
        p.dummyCount = br.ReadByte();
        p.optimizeHint = br.ReadByte();

        p.unknown_0x100 = br.ReadUInt64();
        
        
        p.VertexShaderData = ReadShaderData(br, p.vsPtr, p.shaderSize[0]);
        p.HullShaderData   = ReadShaderData(br, p.hsPtr, p.shaderSize[1]);
        p.DomainShaderData = ReadShaderData(br, p.dsPtr, p.shaderSize[2]);
        p.GeometryShaderData = ReadShaderData(br, p.gsPtr, p.shaderSize[3]);
        p.PixelShaderData  = ReadShaderData(br, p.psPtr, p.shaderSize[4]);
        p.ComputeShaderData = ReadShaderData(br, p.csPtr, p.shaderSize[5]);
        
        void SaveStage(string name, byte[] data)
        {
            if (data != null && data.Length > 0)
            {
                string basePath = @"C:\Users\daniel\Downloads\RE8Lighting";
                string safeName = string.Concat(p.name.Split(Path.GetInvalidFileNameChars()));
                string shaderDir = Path.Combine(basePath, safeName);
                Directory.CreateDirectory(shaderDir);
                File.WriteAllBytes(
                    Path.Combine(shaderDir, $"{name}.cso"),
                    data
                );
            }
        }

        SaveStage("VS", p.VertexShaderData);
        SaveStage("HS", p.HullShaderData);
        SaveStage("DS", p.DomainShaderData);
        SaveStage("GS", p.GeometryShaderData);
        SaveStage("PS", p.PixelShaderData);
        SaveStage("CS", p.ComputeShaderData);
        
        p.ConstantBindings = new List<(ShaderConstantBufferInfo, ShaderSlotHandle)>(p.constantCount);

        for (int i = 0; i < p.constantCount; i++)
        {
            ulong cbOffset = p.constantInfos + (ulong)(i * 0x20);
            ulong slotOffset = p.srvSlots + (ulong)(i * 0x8);
            var cb = ReadConstantBufferInfo(br, cbOffset);
            var slot = ReadShaderSlotHandle(br, slotOffset);
            p.ConstantBindings.Add((cb, slot));
        }
        
        p.SrvBindings = new List<(ShaderResourceInfo, ShaderSlotHandle)>(p.srvCount);
        for (int i = 0; i < p.srvCount; i++)
        {
            ulong infoOffset = p.srvInfos + (ulong)(i * 0x10);
            ulong slotOffset = p.srvSlots + (ulong)(i * 0x8);

            var info = ReadShaderResourceInfo(br, infoOffset);
            var slot = ReadShaderSlotHandle(br, slotOffset);

            p.SrvBindings.Add((info, slot));
        }
        
        p.SamplerBindings = new List<(ShaderResourceInfo, ShaderSlotHandle)>(p.samplerCount);

        for (int i = 0; i < p.samplerCount; i++)
        {
            ulong infoOffset = p.samplerInfos + (ulong)(i * 0x10);
            ulong slotOffset = p.samplerSlots + (ulong)(i * 0x8);

            var info = ReadShaderResourceInfo(br, infoOffset);
            var slot = ReadShaderSlotHandle(br, slotOffset);

            p.SamplerBindings.Add((info, slot));
        }
        
        p.InputLayout = ReadInputLayoutDesc(br, p.ILPtr);
        
        return p;
    }
    
    private static byte[] ReadShaderData(BinaryReader br, ulong ptr, uint size)
    {
        if (ptr == 0 || size == 0)
            return Array.Empty<byte>();

        long currentPos = br.BaseStream.Position;
        br.BaseStream.Seek((long)ptr, SeekOrigin.Begin);
        byte[] data = br.ReadBytes((int)size);
        br.BaseStream.Seek(currentPos, SeekOrigin.Begin);
        return data;
    }
    
    private static ShaderConstantBufferInfo ReadConstantBufferInfo(BinaryReader br, ulong offset)
    {
        long returnPos = br.BaseStream.Position;
        br.BaseStream.Seek((long)offset, SeekOrigin.Begin);

        long namePtr = br.ReadInt64();
        uint nameHash = br.ReadUInt32();

        uint shaderIdAndFlag = br.ReadUInt32();
        uint shaderID = shaderIdAndFlag & 0x7FFFFFFF;
        bool sceneDependent = (shaderIdAndFlag & 0x80000000) != 0;

        uint size = br.ReadUInt32();
        uint variableCount = br.ReadUInt32();
        ulong variablesPtr = br.ReadUInt64();

        var cb = new ShaderConstantBufferInfo
        {
            Name = br.ReadNullTerminatedString(namePtr),
            NameHash = nameHash,
            ShaderID = shaderID,
            SceneDependent = sceneDependent,
            Size = size,
            VariableCount = variableCount
        };
        
        cb.Variables = new List<ShaderConstantVariableHandle>((int)variableCount);

        for (int i = 0; i < variableCount; i++)
        {
            ulong varOffset = variablesPtr + (ulong)(i * 0x10);
            var variable = ReadConstantVariableHandle(br, varOffset);
            cb.Variables.Add(variable);
        }

        br.BaseStream.Seek(returnPos, SeekOrigin.Begin);

        return cb;
    }
    
    private static ShaderConstantVariableHandle ReadConstantVariableHandle(BinaryReader br, ulong offset)
    {
        long returnPos = br.BaseStream.Position;
        br.BaseStream.Seek((long)offset, SeekOrigin.Begin);

        long namePtr = br.ReadInt64();
        uint nameHash = br.ReadUInt32();
        byte[] elementData = br.ReadBytes(4);

        var variable = new ShaderConstantVariableHandle
        {
            Name = br.ReadNullTerminatedString(namePtr),
            NameHash = nameHash,
            Data = elementData
        };

        br.BaseStream.Seek(returnPos, SeekOrigin.Begin);

        return variable;
    }
    
    private static ShaderResourceInfo ReadShaderResourceInfo(BinaryReader br, ulong offset)
    {
        long returnPos = br.BaseStream.Position;
        br.BaseStream.Seek((long)offset, SeekOrigin.Begin);

        long namePtr = br.ReadInt64();
        uint nameHash = br.ReadUInt32();

        uint shaderIdAndFlag = br.ReadUInt32();
        uint shaderID = shaderIdAndFlag & 0x7FFFFFFF;
        bool sceneDependent = (shaderIdAndFlag & 0x80000000) != 0;

        var info = new ShaderResourceInfo
        {
            Name = br.ReadNullTerminatedString(namePtr),
            NameHash = nameHash,
            ShaderID = shaderID,
            SceneDependent = sceneDependent
        };

        br.BaseStream.Seek(returnPos, SeekOrigin.Begin);
        return info;
    }
    
    private static ShaderSlotHandle ReadShaderSlotHandle(BinaryReader br, ulong offset)
    {
        long ret = br.BaseStream.Position;
        br.BaseStream.Seek((long)offset, SeekOrigin.Begin);

        var h = new ShaderSlotHandle();
        h.Raw = br.ReadBytes(8);

        br.BaseStream.Seek(ret, SeekOrigin.Begin);
        return h;
    }
    
    private static InputLayoutDesc ReadInputLayoutDesc(BinaryReader br, ulong offset)
    {
        if (offset == 0)
        {
            return new InputLayoutDesc
            {
                ElementCount = 0,
                Elements = new List<InputElement>()
            };
        }
        long returnPos = br.BaseStream.Position;
        br.BaseStream.Seek((long)offset, SeekOrigin.Begin);

        var layout = new InputLayoutDesc();

        layout.ElementCount = br.ReadUInt32();
        layout.Elements = new List<InputElement>((int)layout.ElementCount);
        br.ReadUInt32();

        for (int i = 0; i < layout.ElementCount; i++)
        {
            ulong raw = br.ReadUInt64();

            var element = new InputElement
            {
                SemanticType  = (SemanticType)(raw & 0xFF),
                Format        = (byte)((raw >> 8) & 0xFF),
                InputSlot     = (byte)((raw >> 16) & 0xFF),
                SemanticIndex = (byte)((raw >> 24) & 0xFF),
                Offset        = (uint)((raw >> 32) & 0x7FFFFFFF),
                IsInstanceData = ((raw >> 63) & 1) != 0
            };

            layout.Elements.Add(element);
        }

        br.BaseStream.Seek(returnPos, SeekOrigin.Begin);
        return layout;
    }
    
    public bool ResolveDependencies(SdfData asset) => true;
}
