using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using Vortice.Direct3D12;

namespace REAssetExplorer.RenderTest2.Assets;

/// <summary>
/// Enum to identify material structure types
/// </summary>
public enum MaterialStructureType
{
    Default,
    Env_1Layer_Dirt,
    Env_1Layer_Common_Dirt,
    Env_1Layer_Common_Dirt_DecalBlend,
    Env_2Layer_Common_Dirt,
    Env_2Layer_Dirt,
    Env_3Layer_Common_Dirt,
    Env_3Layer_Dirt
}

/// <summary>
/// Attribute to define material type metadata (shaders, master material name)
/// Apply this to material constant structures
/// </summary>
[AttributeUsage(AttributeTargets.Struct)]
public class MaterialTypeAttribute : Attribute
{
    public string MasterMaterialName { get; set; }
    public string VertexShader { get; set; }
    public string PixelShader { get; set; }
    public MaterialStructureType StructureType { get; set; }
    
    public MaterialTypeAttribute(
        MaterialStructureType structureType,
        string masterMaterialName,
        string vertexShader = "Shaders/DeferredVertex.hlsl",
        string pixelShader = "Shaders/Env_DefaultPS.hlsl")
    {
        StructureType = structureType;
        MasterMaterialName = masterMaterialName;
        VertexShader = vertexShader;
        PixelShader = pixelShader;
    }
}

/// <summary>
/// Base interface for material constant structures
/// </summary>
public interface IMaterialConstants
{
    MaterialStructureType StructureType { get; }
}

/// <summary>
/// Default material constants (Env_Default)
/// </summary>
[MaterialType(
    MaterialStructureType.Default,
    "Env_Default",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_DefaultPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Default : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Default;
    
    // Basic material properties
    public Vector4 BaseColor;
    public float Metallic;
    public float Roughness;
    public float Translucency;
    public float AlphaTestRef;
    public float OCC_UVSelect;
}

[MaterialType(
    MaterialStructureType.Env_1Layer_Common_Dirt,
    "Env_1Layer_Common_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_1Layer_Common_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_1Layer_Common_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_1Layer_Common_Dirt;
    
    // Vector4 fields
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

[MaterialType(
    MaterialStructureType.Env_2Layer_Common_Dirt,
    "Env_2Layer_Common_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_2Layer_Common_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_2Layer_Common_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_2Layer_Common_Dirt;
    
    // Vector4 fields
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 BaseColor1;
    public Vector4 UV_Tiling_Offset1;
    public Vector4 WearMap_Tiling_Offset1;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float ExtraLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;
    public float MaterialColor_AddBlend1;
    public float MaterialColor_Override1;
    public float MaterialColor_Intensity1;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Roughness1;
    public float MaterialTranslucency1;
    public float PhysicalMaterialIndex1;
    public float UV_Tiling1;
    public float Use_AdvancedUVSetting1;
    public float NormalBlend_Balance1;
    public float WearMap_Tiling1;
    public float Use_AdvancedUVSetting_WearMap1;
    public float WearMap_Inverse1;
    public float WearMap_Blend1;
    public float WearMap_Normal_BlendMode1;
    public float WearMap_NormalIntensity1;
    public float LayerMask_BrightnessMasking1;
    public float LayerMask_Brightness1;
    public float LayerMask_Contrast1;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

[MaterialType(
    MaterialStructureType.Env_1Layer_Dirt,
    "Env_1Layer_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_1Layer_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_1Layer_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_1Layer_Dirt;
    
    // Vector4 fields
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 BaseColor1;
    public Vector4 UV_Tiling_Offset1;
    public Vector4 WearMap_Tiling_Offset1;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float ExtraLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;
    public float MaterialColor_AddBlend1;
    public float MaterialColor_Override1;
    public float MaterialColor_Intensity1;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Roughness1;
    public float MaterialTranslucency1;
    public float PhysicalMaterialIndex1;
    public float UV_Tiling1;
    public float Use_AdvancedUVSetting1;
    public float NormalBlend_Balance1;
    public float WearMap_Tiling1;
    public float Use_AdvancedUVSetting_WearMap1;
    public float WearMap_Inverse1;
    public float WearMap_Blend1;
    public float WearMap_Normal_BlendMode1;
    public float WearMap_NormalIntensity1;
    public float LayerMask_BrightnessMasking1;
    public float LayerMask_Brightness1;
    public float LayerMask_Contrast1;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

[MaterialType(
    MaterialStructureType.Env_1Layer_Dirt,
    "Env_3Layer_Common_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_3Layer_Common_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_3Layer_Common_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_3Layer_Common_Dirt;
    
    // Vector4 fields
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 BaseColor1;
    public Vector4 UV_Tiling_Offset1;
    public Vector4 WearMap_Tiling_Offset1;
    public Vector4 BaseColor2;
    public Vector4 UV_Tiling_Offset2;
    public Vector4 WearMap_Tiling_Offset2;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float ExtraLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;
    public float MaterialColor_AddBlend1;
    public float MaterialColor_Override1;
    public float MaterialColor_Intensity1;
    public float MaterialColor_AddBlend2;
    public float MaterialColor_Override2;
    public float MaterialColor_Intensity2;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Roughness1;
    public float MaterialTranslucency1;
    public float PhysicalMaterialIndex1;
    public float UV_Tiling1;
    public float Use_AdvancedUVSetting1;
    public float NormalBlend_Balance1;
    public float WearMap_Tiling1;
    public float Use_AdvancedUVSetting_WearMap1;
    public float WearMap_Inverse1;
    public float WearMap_Blend1;
    public float WearMap_Normal_BlendMode1;
    public float WearMap_NormalIntensity1;
    public float LayerMask_BrightnessMasking1;
    public float LayerMask_Brightness1;
    public float LayerMask_Contrast1;
    public float Roughness2;
    public float MaterialTranslucency2;
    public float PhysicalMaterialIndex2;
    public float UV_Tiling2;
    public float Use_AdvancedUVSetting2;
    public float NormalBlend_Balance2;
    public float WearMap_Tiling2;
    public float Use_AdvancedUVSetting_WearMap2;
    public float WearMap_Inverse2;
    public float WearMap_Blend2;
    public float WearMap_Normal_BlendMode2;
    public float WearMap_NormalIntensity2;
    public float LayerMask_BrightnessMasking2;
    public float LayerMask_Brightness2;
    public float LayerMask_Contrast2;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

[MaterialType(
    MaterialStructureType.Env_2Layer_Dirt,
    "Env_2Layer_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_2Layer_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_2Layer_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_2Layer_Dirt;
    
    // Vector4 fields
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 BaseColor1;
    public Vector4 UV_Tiling_Offset1;
    public Vector4 WearMap_Tiling_Offset1;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;
    public Vector4 WaterLine_Color;
    public Vector4 WaterLine_EdgeColor;

    // Float fields
    public float Occlusion_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float ExtraLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float UV_Switch;
    public float Roughness;
    public float MaterialTranslucency;
    public float PhysicalMaterialIndex;
    public float UV_Tiling;
    public float Use_AdvancedUVSetting;
    public float Roughness1;
    public float MaterialTranslucency1;
    public float PhysicalMaterialIndex1;
    public float UV_Tiling1;
    public float Use_AdvancedUVSetting1;
    public float NormalBlend_Balance1;
    public float WearMap_Tiling1;
    public float Use_AdvancedUVSetting_WearMap1;
    public float WearMap_Inverse1;
    public float WearMap_Blend1;
    public float WearMap_Normal_BlendMode1;
    public float WearMap_NormalIntensity1;
    public float LayerMask_BrightnessMasking1;
    public float LayerMask_Brightness1;
    public float LayerMask_Contrast1;
    public float Dirt_Enable;
    public float DirtWearMap_Inverse;
    public float DirtMask_Brightness;
    public float DirtMask_Contrast;
    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;
    public float Dirt_Metallic;
    public float Dirt_Roughness;
    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
    public float Enable_Oil;
    public float Oil_Intensity;
    public float Oil_Thickness;
    public float Oil_WearTiling;
    public float Oil_WearBlend;
    public float Oil_DarkColor;
    public float Oil_Roughness;
    public float Enable_WaterLine;
    public float WaterLine_WorldHeight;
    public float WaterLine_BlurWidth;
    public float WaterLine_Contrast;
    public float WaterLine_WearBlend;
    public float WaterLine_WearTiling_Top;
    public float WaterLine_WearTiling_Side;
    public float WaterLine_EdgeColorOverride;
    public float WaterLine_RoughnessBlend;
    public float WaterLine_Roughness;
    public float WaterLine_Roughness_Occluded;
    public float WaterLine_NormalWeaken;
    public float WaterLine_NormalWeaken_Occluded;
}

/// <summary>
/// Material constants for Env_3Layer_Dirt shader
/// </summary>
[MaterialType(
    MaterialStructureType.Env_3Layer_Dirt,
    "Env_3Layer_Dirt",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_3Layer_DirtPS.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct MaterialConstants_Env_3Layer_Dirt : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_3Layer_Dirt;
    
    public Vector4 BaseColor;
    public float Metallic;
    public float Roughness;
    public float Translucency;
    public float AlphaTestRef;
    public float OCC_UVSelect;
}

[MaterialType(
    MaterialStructureType.Env_1Layer_Common_Dirt_DecalBlend,
    "Env_1Layer_Common_Dirt_DecalBlend",
    "Shaders/DeferredVertex.hlsl",
    "Shaders/Env_1Layer_Common_Dirt_DecalBlend.hlsl")]
[StructLayout(LayoutKind.Sequential)]
public struct Env_1Layer_Common_Dirt_DecalBlend : IMaterialConstants
{
    public MaterialStructureType StructureType => MaterialStructureType.Env_1Layer_Common_Dirt_DecalBlend;
    
    public Vector4 BaseAlphaMap_Tiling_Offset;
    public Vector4 BaseColor;
    public Vector4 UV_Tiling_Offset;
    public Vector4 DirtColor1;
    public Vector4 DirtColor2;
    public Vector4 DirtColor3;
    public Vector4 DirtWearMap_Tiling_Offset;

    public float Occlusion_Use_SecondaryUV;
    public float MaterialColor_Use_SecondaryUV;
    public float LayerMask_Use_SecondaryUV;
    public float BaseLayer_Use_SecondaryUV;
    public float DirtMap_Use_SecondaryUV;
    public float AlphaTranslucent_Use_SecondaryUV;

    public float MaterialColor_AddBlend;
    public float MaterialColor_Override;
    public float MaterialColor_Intensity;

    public float AlphaThreshold;
    public float AlphaContrast;

    public float Roughness;
    public float PhysicalMaterialIndex;

    public float UV_Tiling;
    public float Use_AdvancedUVSetting;

    public float Dirt_Enable;
    public float DirtWearMap_Inverse;

    public float DirtMask_Brightness;
    public float DirtMask_Contrast;

    public float DirtColor_AddBlend;
    public float DirtColor_Override;
    public float DirtColorControl;

    public float Dirt_Metallic;
    public float Dirt_Roughness;

    public float DirtWearMap_Tiling;
    public float Use_AdvancedUVSetting_DirtWearMap;
}

/// <summary>
/// Base interface for material constant buffer wrappers
/// Allows handling different material types polymorphically without switch statements
/// </summary>
public interface IMaterialConstantBufferWrapper : IDisposable
{
    MaterialStructureType StructureType { get; }
    void UpdateConstants(int frameIndex, object materialParams);
    ulong GetGPUVirtualAddress(int frameIndex);
}

/// <summary>
/// Generic wrapper for material constant buffers
/// Encapsulates ConstantBuffer<T> to allow polymorphic handling
/// </summary>
public class MaterialConstantBufferWrapper<T> : IMaterialConstantBufferWrapper where T : unmanaged, IMaterialConstants
{
    private readonly DX12.ConstantBuffer<T> _constantBuffer;
    
    public MaterialStructureType StructureType { get; }
    
    public MaterialConstantBufferWrapper(ID3D12Device device, int bufferCount)
    {
        _constantBuffer = new DX12.ConstantBuffer<T>(device, bufferCount);
        
        // Get structure type from the type itself
        var defaultValue = new T();
        StructureType = defaultValue.StructureType;
    }
    
    public void UpdateConstants(int frameIndex, object materialParams)
    {
        if (materialParams is T typedParams)
        {
            _constantBuffer.Update(frameIndex, ref typedParams);
        }
        else
        {
            throw new ArgumentException($"Material params must be of type {typeof(T).Name}");
        }
    }
    
    public ulong GetGPUVirtualAddress(int frameIndex)
    {
        return _constantBuffer.GetGPUVirtualAddress(frameIndex);
    }
    
    public void Dispose()
    {
        _constantBuffer?.Dispose();
    }
}

/// <summary>
/// Factory for creating material constant buffer wrappers based on structure type
/// Uses reflection to create the appropriate generic wrapper
/// </summary>
public static class MaterialConstantBufferFactory
{
    /// <summary>
    /// Creates a material constant buffer wrapper for the specified structure type
    /// </summary>
    public static IMaterialConstantBufferWrapper? CreateWrapper(
        MaterialStructureType structureType,
        ID3D12Device device,
        int bufferCount = 3)
    {
        var info = MaterialTypeRegistry.GetInfo(structureType);
        if (info == null)
        {
            Console.WriteLine($"Unknown material structure type: {structureType}");
            return null;
        }
        
        // Create MaterialConstantBufferWrapper<T> where T is info.Type
        var wrapperType = typeof(MaterialConstantBufferWrapper<>).MakeGenericType(info.Type);
        var wrapper = Activator.CreateInstance(wrapperType, device, bufferCount);
        
        return wrapper as IMaterialConstantBufferWrapper;
    }
}

/// <summary>
/// Registry that automatically discovers and caches material type metadata using reflection
/// Eliminates the need for manual switch statements when adding new material types
/// </summary>
public static class MaterialTypeRegistry
{
    private static readonly Dictionary<MaterialStructureType, MaterialTypeInfo> _typeToInfo = new();
    private static readonly Dictionary<string, MaterialTypeInfo> _nameToInfo = new();
    private static bool _initialized = false;
    
    public class MaterialTypeInfo
    {
        public Type Type { get; set; } = typeof(object);
        public MaterialStructureType StructureType { get; set; }
        public string MasterMaterialName { get; set; } = "";
        public string VertexShader { get; set; } = "";
        public string PixelShader { get; set; } = "";
    }
    
    static MaterialTypeRegistry()
    {
        Initialize();
    }
    
    private static void Initialize()
    {
        if (_initialized)
            return;
        
        // Find all structs that implement IMaterialConstants in this assembly
        var assembly = typeof(MaterialTypeRegistry).Assembly;
        var materialTypes = assembly.GetTypes()
            .Where(t => t.IsValueType && 
                   typeof(IMaterialConstants).IsAssignableFrom(t) && 
                   t.GetCustomAttributes(typeof(MaterialTypeAttribute), false).Length > 0);
        
        foreach (var type in materialTypes)
        {
            var attr = (MaterialTypeAttribute)type.GetCustomAttributes(typeof(MaterialTypeAttribute), false)[0];
            
            var info = new MaterialTypeInfo
            {
                Type = type,
                StructureType = attr.StructureType,
                MasterMaterialName = attr.MasterMaterialName,
                VertexShader = attr.VertexShader,
                PixelShader = attr.PixelShader
            };
            
            _typeToInfo[attr.StructureType] = info;
            _nameToInfo[attr.MasterMaterialName.ToLowerInvariant()] = info;
        }
        
        _initialized = true;
    }
    
    /// <summary>
    /// Gets material type info by structure type
    /// </summary>
    public static MaterialTypeInfo? GetInfo(MaterialStructureType structureType)
    {
        return _typeToInfo.TryGetValue(structureType, out var info) ? info : null;
    }
    
    /// <summary>
    /// Gets material type info by master material name
    /// </summary>
    public static MaterialTypeInfo? GetInfo(string masterMaterialName)
    {
        return _nameToInfo.TryGetValue(masterMaterialName.ToLowerInvariant(), out var info) ? info : null;
    }
    
    /// <summary>
    /// Gets all registered material types
    /// </summary>
    public static IEnumerable<MaterialTypeInfo> GetAllTypes()
    {
        return _typeToInfo.Values;
    }
}