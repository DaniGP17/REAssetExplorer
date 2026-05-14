// Pixel Shader para Alpha Mask Pre-pass (Depth-Only)
// Este shader solo descarta píxeles basándose en alpha, sin escribir color

SamplerState AutomaticWrap : register(s0);

// Solo necesitamos la textura con canal alpha
Texture2D<float4> AlphaTranslucentOcclusionSSSMap : register(t0);   // R: Alpha, G: Translucency, B: AO, A: SSS

cbuffer UserMaterial : register(b0)
{
    float4 BaseColor;               // Color base RGBA
    float  Metallic;                // Factor metálico [0-1]
    float  Roughness;               // Rugosidad [0-1]
    float  Translucency;            // Translucidez [0-1]
    float  AlphaTestRef;            // Referencia para alpha test
    float  OCC_UVSelect;            // Selector de UVs para occlusion
    float3 _matPad;                 // Padding
};

struct InputStruct
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

// No necesitamos output de color, solo depth
void main(in InputStruct IN)
{
    // Sample alpha channel
    float alpha = AlphaTranslucentOcclusionSSSMap.Sample(AutomaticWrap, IN.UV).r;
    
    // Descartar píxeles con alpha menor al threshold
    // Solo los píxeles opacos escribirán en el depth buffer
    clip(alpha - AlphaTestRef);
    
    // No escribimos color, solo depth
    // El hardware automáticamente escribe depth para píxeles que pasan clip()
}
