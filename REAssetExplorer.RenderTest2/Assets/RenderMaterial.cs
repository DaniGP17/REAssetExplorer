using System;
using System.Linq;
using Vortice.Direct3D12;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.RenderTest2.Interop.Structures;

namespace REAssetExplorer.RenderTest2.Assets;

public class RenderMaterial : Resource
{
    public override ResourceType Type => ResourceType.Material;
    
    public ShaderType ShaderType;
    public MaterialFlags Flags;
    public RenderShader? Shader;
    public string MasterMaterialName = "";
    public MaterialTexture[] Textures = Array.Empty<MaterialTexture>();
    public MaterialProperty[] Properties = Array.Empty<MaterialProperty>();
    
    /// <summary>
    /// Gets the shader paths and material structure type for this material
    /// Uses MaterialTypeRegistry for automatic lookup - no manual switch needed!
    /// Returns (vertexShaderPath, pixelShaderPath, materialStructureType)
    /// </summary>
    public (string vertexShader, string pixelShader, MaterialStructureType structureType) GetShaderInfo()
    {
        // Try to find material type by master material name
        var info = MaterialTypeRegistry.GetInfo(MasterMaterialName);
        
        if (info != null)
        {
            return (info.VertexShader, info.PixelShader, info.StructureType);
        }
        
        // Fallback to default
        Console.WriteLine($"Unknown master material: {MasterMaterialName}, using default");
        var defaultInfo = MaterialTypeRegistry.GetInfo(MaterialStructureType.Default);
        if (defaultInfo != null)
        {
            return (defaultInfo.VertexShader, defaultInfo.PixelShader, MaterialStructureType.Default);
        }
        
        // Last resort hardcoded fallback
        return ("Shaders/DeferredVertex.hlsl", "Shaders/Env_DefaultPS.hlsl", MaterialStructureType.Default);
    }
    
    /// <summary>
    /// Gets the shader paths for this material (legacy compatibility)
    /// Returns (vertexShaderPath, pixelShaderPath)
    /// </summary>
    public (string vertexShader, string pixelShader) GetShaderPaths()
    {
        var (vs, ps, _) = GetShaderInfo();
        return (vs, ps);
    }
}

public class MaterialTexture
{
    public string TextureType = "";
    public RenderTexture? Texture;
}

public class MaterialProperty
{
    public string Name = "";
    public float[] Parameters = Array.Empty<float>();
}

public class MaterialMapper
{
    public static RenderMaterial? MapToRenderMaterial(
        string materialName, 
        MaterialData? materialData, 
        int matIdx,
        ID3D12Device? device = null,
        ID3D12RootSignature? rootSignature = null)
    {
        if (materialData == null)
        {
            return null;
        }
        
        var materialTextures = new MaterialTexture[materialData.MaterialHeaders[matIdx].TextureCount];
        for (int i = 0; i < materialTextures.Length; i++)
        {
            var textureType = materialData.TextureHeaders[matIdx][i].TextureType;
            var texturePath = materialData.TextureHeaders[matIdx][i].TextureFilePath.ToLowerInvariant();

            if (materialData.ResolvedDependencies.TryGetValue(texturePath, out var textureData))
            {
                materialTextures[i] = new MaterialTexture()
                {
                    TextureType = textureType,
                    Texture = TextureMapper.MapToRenderTexture(texturePath, textureData as TextureData)
                };
            }
            else
            {
                Console.WriteLine($"Warning: Texture dependency not found for material '{materialName}': {materialData.TextureHeaders[matIdx][i].TextureFilePath}");
            }
        }

        var renderMaterial = new RenderMaterial()
        {
            Name = materialName,
            ShaderType = materialData.MaterialHeaders[matIdx].ShaderType,
            Flags = materialData.MaterialHeaders[matIdx].Flags,
            Textures = materialTextures,
            MasterMaterialName = materialData.MaterialHeaders[matIdx].MasterMaterialFilePath.Split('/').LastOrDefault()?.Split('.').FirstOrDefault() ?? "",
            Properties = materialData.PropertyHeaders[matIdx].Select(p => new MaterialProperty()
            {
                Name = p.Name,
                Parameters = p.Parameters
            }).ToArray()
        };
        
        /*var mmtrPath = materialData.MaterialHeaders[matIdx].MasterMaterialFilePath.ToLowerInvariant();
        if (materialData.ResolvedDependencies.TryGetValue(mmtrPath, out var sdfData))
        {
            renderMaterial.RenderShader = ShaderMapper.MapToRenderShader(renderMaterial, sdfData as SdfData);
        }
        else
        {
            Console.WriteLine($"Warning: Master material dependency not found for material '{materialName}': {materialData.MaterialHeaders[matIdx].MasterMaterialFilePath}");
        }*/
        
        return renderMaterial;
    }
}