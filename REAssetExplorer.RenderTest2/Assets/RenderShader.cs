using System;
using System.Collections.Generic;
using Vortice.Direct3D12;
using Vortice.DXGI;
using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.RenderTest2.Assets;

/// <summary>
/// Represents a compiled shader with its bytecode and metadata
/// </summary>
public class RenderShader : Resource
{
    public override ResourceType Type => ResourceType.Shader;

    public new string FilePath { get; set; } = "";
    public Dictionary<Pass, ShaderPass> Pipelines { get; set; } = new();
}

public struct ShaderPass
{
    public ShaderBytecodeSet BytecodeSet;
    public ShaderMetadata Metadata;
}

public enum Pass : ushort
{
    GBuffer = 0,
    GBufferInstancing = 1,
    GBufferInstancing2 = 2,
    ZPrePassedGBuffer = 3,
    Shadow = 5,
    ShadowInstancing = 6,
    Pick = 8,
    Forward = 0xA,
    ForwardAlt = 0xD,
    DepthWrite = 0xE,
    ZPrePass = 0x10,
    StencilWrite = 0x13,
    PreTransform = 0x23,
}

public struct ShaderBytecodeSet
{
    public byte[]? VertexShader;
    public byte[]? PixelShader;
    public byte[]? ComputeShader;
    public byte[]? GeometryShader;
    public byte[]? HullShader;
    public byte[]? DomainShader;
}

public class ShaderMapper
{
    public static Dictionary<Pass, ShaderPass> BuildPasses(List<ShaderProgramData> programs)
    {
        var map = new Dictionary<Pass, string>()
        {
            { Pass.GBuffer, "DeferredStatic" },
        };
        
        var result = new Dictionary<Pass, ShaderPass>();
        foreach (var (pass, programName) in map)
        {
            var program = programs.Find(p => p.name.Equals(programName, StringComparison.OrdinalIgnoreCase));

            var shaderPass = new ShaderPass();

            shaderPass.Metadata = new ShaderMetadata()
            {
                ProgramName = programName,
                ConstantBuffers = MapConstantBuffers(program),
                SrvBindings = MapResourceBindings(program.SrvBindings),
                SamplerBindings = MapResourceBindings(program.SamplerBindings),
                InputLayout = MapInputLayout(program.InputLayout)
            };

            shaderPass.BytecodeSet = new ShaderBytecodeSet()
            {
                VertexShader = program.VertexShaderData,
                PixelShader = program.PixelShaderData,
                ComputeShader = program.ComputeShaderData,
                GeometryShader = program.GeometryShaderData,
                HullShader = program.HullShaderData,
                DomainShader = program.DomainShaderData,
            };
            
            result[pass] = shaderPass;
        }

        return result;
    }
    
    public static RenderShader? MapToRenderShader(RenderMaterial renderMaterial, SdfData? sdfData)
    {
        if (sdfData == null || sdfData.ShaderPrograms.Count == 0)
        {
            Console.WriteLine("Warning: SdfData is null or has no shader programs");
            return null;
        }
        
        var shader = new RenderShader
        {
            Pipelines = BuildPasses(sdfData.ShaderPrograms)
        };
        
        return shader;
    }
    
    private static List<ConstantBufferBinding> MapConstantBuffers(ShaderProgramData program)
    {
        if (program.ConstantBindings == null)
            return new List<ConstantBufferBinding>();
        var bindings = new List<ConstantBufferBinding>();
        
        foreach (var (cbInfo, slot) in program.ConstantBindings)
        {
            var binding = new ConstantBufferBinding
            {
                Name = cbInfo.Name,
                NameHash = cbInfo.NameHash,
                Size = cbInfo.Size,
                Slot = slot.Slot,
                ActiveStages = GetActiveStages(slot.StageBit)
            };
            
            bindings.Add(binding);
        }
        
        return bindings;
    }
    
    private static List<ResourceBinding> MapResourceBindings(
        List<(ShaderResourceInfo, ShaderSlotHandle)> sourceBindings)
    {
        if (sourceBindings == null)
            return new List<ResourceBinding>();
        var bindings = new List<ResourceBinding>();
        
        foreach (var (resInfo, slot) in sourceBindings)
        {
            var binding = new ResourceBinding
            {
                Name = resInfo.Name,
                NameHash = resInfo.NameHash,
                Slot = slot.Slot,
                ResourceType = slot.ResourceType,
                ActiveStages = GetActiveStages(slot.StageBit)
            };
            
            bindings.Add(binding);
        }
        
        return bindings;
    }
    
    private static InputLayoutDescription MapInputLayout(InputLayoutDesc layoutDesc)
    {
        if (layoutDesc.Elements == null)
            return new InputLayoutDescription();
        var elements = new List<InputElementInfo>();
        
        foreach (var elem in layoutDesc.Elements)
        {
            elements.Add(new InputElementInfo
            {
                SemanticName = GetSemanticName(elem.SemanticType),
                SemanticIndex = elem.SemanticIndex,
                Format = elem.Format,
                InputSlot = elem.InputSlot,
                Offset = elem.Offset,
                IsInstanceData = elem.IsInstanceData
            });
        }
        
        return new InputLayoutDescription
        {
            ElementCount = layoutDesc.ElementCount,
            Elements = elements
        };
    }
    
    private static ShaderStage GetActiveStages(byte stageBit)
    {
        var stages = ShaderStage.None;
        
        if ((stageBit & 0x01) != 0) stages |= ShaderStage.Vertex;
        if ((stageBit & 0x02) != 0) stages |= ShaderStage.Hull;
        if ((stageBit & 0x04) != 0) stages |= ShaderStage.Domain;
        if ((stageBit & 0x08) != 0) stages |= ShaderStage.Geometry;
        if ((stageBit & 0x10) != 0) stages |= ShaderStage.Pixel;
        if ((stageBit & 0x20) != 0) stages |= ShaderStage.Compute;
        
        return stages;
    }
    
    private static string GetSemanticName(SemanticType type)
    {
        return type switch
        {
            SemanticType.Position => "POSITION",
            SemanticType.Normal => "NORMAL",
            SemanticType.Binormal => "BINORMAL",
            SemanticType.Tangent => "TANGENT",
            SemanticType.Texcoord => "TEXCOORD",
            SemanticType.Index => "BLENDINDICES",
            SemanticType.Weight => "BLENDWEIGHT",
            SemanticType.Color => "COLOR",
            SemanticType.VertexId => "SV_VertexID",
            SemanticType.InstanceId => "SV_InstanceID",
            _ => "TEXCOORD"
        };
    }
}

/// <summary>
/// Shader metadata extracted from SDF
/// </summary>
public class ShaderMetadata
{
    public string ProgramName { get; set; } = "";
    public List<ConstantBufferBinding> ConstantBuffers { get; set; } = new();
    public List<ResourceBinding> SrvBindings { get; set; } = new();
    public List<ResourceBinding> SamplerBindings { get; set; } = new();
    public InputLayoutDescription InputLayout { get; set; } = new();
}

public class ConstantBufferBinding
{
    public string Name { get; set; } = "";
    public uint NameHash { get; set; }
    public uint Size { get; set; }
    public ushort Slot { get; set; }
    public ShaderStage ActiveStages { get; set; }
}

public class ResourceBinding
{
    public string Name { get; set; } = "";
    public uint NameHash { get; set; }
    public ushort Slot { get; set; }
    public byte ResourceType { get; set; }
    public ShaderStage ActiveStages { get; set; }
}

public class InputLayoutDescription
{
    public uint ElementCount { get; set; }
    public List<InputElementInfo> Elements { get; set; } = new();
}

public class InputElementInfo
{
    public string SemanticName { get; set; } = "";
    public byte SemanticIndex { get; set; }
    public byte Format { get; set; }
    public byte InputSlot { get; set; }
    public uint Offset { get; set; }
    public bool IsInstanceData { get; set; }
}
