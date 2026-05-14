using System;
using System.Runtime.InteropServices;
using SharpDX;

namespace REAssetExplorer.Rendering.Shaders;

/// <summary>
/// Constant buffers para el sistema de renderizado RE Engine
/// </summary>

/// <summary>
/// SceneInfo - Información de la escena (Matrices y viewport) - register b0
/// Size: 288 bytes (aligned to 16)
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct SceneInfoBuffer
{
    public Matrix World;
    public Matrix View;
    public Matrix Projection;
    public Matrix WorldViewProjection;
    public Vector4 CameraPosition;
    public Vector4 ViewportSize; // (width, height, 1/width, 1/height)
}

/// <summary>
/// UserMaterial - Propiedades del material - register b1
/// Size: 48 bytes (aligned to 16)
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct UserMaterialBuffer
{
    public Vector4 BaseColor;      // Color base RGBA
    public float Metallic;         // Factor metálico [0-1]
    public float Roughness;        // Rugosidad [0-1]
    public float Translucency;     // Translucidez [0-1]
    public float AlphaTestRef;     // Referencia para alpha test [0-1]
    public float OccUVSelect;      // Selector de UV para occlusion
    public Vector3 Padding;        // Padding para alineación
}

/// <summary>
/// GBufferType - Tipo de GBuffer para identificar materiales - register b2
/// Size: 16 bytes (aligned to 16)
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct GBufferTypeBuffer
{
    public float GBufferTypeFlag;  // Flag de tipo de material
    public Vector3 Padding;        // Padding para alineación
}

/// <summary>
/// RootConstant - Constantes raíz del shader - PS register b0
/// Size: 16 bytes
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct RootConstantBuffer
{
    public Vector4 Constant32Bits; // 4 floats o ints como constantes
}

/// <summary>
/// CheckerBoardInfo - Información de checkerboard rendering - PS register b1
/// Size: 16 bytes
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct CheckerBoardInfoBuffer
{
    public Vector2 Cbr;          // Checkerboard resolution
    public float CbrBias;        // Checkerboard bias
    public float CbrUsing;       // Checkerboard en uso (0/1)
}

/// <summary>
/// LightInfo - Información de iluminación - PS register b2
/// Size: 352 bytes
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct LightInfoBuffer
{
    // Light counts
    public int PunctualLightCount;
    public int AreaLightCount;
    public int PunctualLightForwardCount;
    public int AreaLightForwardCount;
    
    // Culling info
    public Vector2 LightCullingScreenSize;
    public Vector2 InverseLightCullingScreenSize;
    public float LightCullingOffsetScale;
    public int RT_PunctualLightCount;
    public int RT_AreaLightCount;
    public int CubemapArrayCount;
    
    // Directional light
    public Vector3 DL_Direction;
    public float DL_Enable;
    public Vector3 DL_Color;
    public float DL_MinAlpha;
    public Vector3 DL_VolumetricScatteringColor;
    public float DL_Reserved;
    
    public Matrix DL_ViewProjection;
    
    public float DL_Variance;
    public int DL_ArrayIndex;
    public int DL_TranslucentArrayIndex;
    public float DL_Bias;
    
    // Cascades (simplified - original has more fields)
    public Vector3 Cascade_Translate1;
    public float Cascade_Bias1;
    public Vector3 Cascade_Translate2;
    public float Cascade_Bias2;
    public Vector3 Cascade_Translate3;
    public float Cascade_Bias3;
    public Vector2 Cascade_Scale1;
    public Vector2 Cascade_Scale2;
    public Vector2 Cascade_Scale3;
    
    public float SDSMEnable;
    public float SDSMDebugDraw;
    public Vector4 CascadeDistance;
    
    // Atmosphere
    public Vector3 Atmopshere_Reserved;
    public int Atmopshere_Flags;
    
    // Light probes
    public float LightProbeOffset;
    public int SparseLightProbeAreaNum;
    public int TetNumMinus1;
    public int SparseTetNumMinus1;
    public float SmoothStepRateMinus;
    public float SmoothStepRateRcp;
    public float LightProbeReserve1;
    public float LightProbeReserve2;
    
    // AO
    public Vector3 AOTint;
    public float AOReserve1;
    
    public Vector3 LightProbe_WorldOffset;
    public float LightProbe_Reserved;
}

/// <summary>
/// OutdoorLightProbeParam - Parámetros de light probes outdoor - PS register b3
/// Size: 64 bytes
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct OutdoorLightProbeParamBuffer
{
    public Vector3 GridOffset;
    public float GridScale;
    public Vector3 OutdoorGridOffset;
    public float GridDepth;
    public Vector3 OutdoorGridScale;
    public float OutdoorLightProbeParam3;
    public Vector2 OutdoorGridMin;
    public Vector2 OutdoorGridMax;
}

/// <summary>
/// Material simple - Color base, metallic, roughness (DEPRECATED - usar UserMaterialBuffer)
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
[Obsolete("Usar UserMaterialBuffer en su lugar")]
public struct MaterialBuffer
{
    public Vector4 BaseColor;       // Color base RGBA
    public float Metallic;          // Factor metálico [0-1]
    public float Roughness;         // Rugosidad [0-1]
    public float Padding1;          // Padding
    public float Padding2;          // Padding
}
