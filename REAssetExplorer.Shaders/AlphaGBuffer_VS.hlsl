// Alpha GBuffer Vertex Shader
// For materials with alpha transparency (Env_Alpha)
// Compatible with AlphaGBuffer_PS.hlsl

cbuffer MatrixBuffer : register(b0)
{
    float4x4 worldMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
};

struct VS_INPUT
{
    float3 position  : POSITION;
    float4 normal    : NORMAL;      // Normal is packed as float4
    float4 tangent   : TANGENT;     // Tangent is packed as float4
    float2 texCoord  : TEXCOORD0;
    float2 texCoord2 : TEXCOORD1;
};

// Output must match InputStruct in AlphaGBuffer_PS.hlsl
struct PS_INPUT
{
    float4 Position : SV_Position;      // Posición en screen space
    float4 param1   : INTERPOLATOR0;    // tangent.xyz, uv.x
    float4 param2   : INTERPOLATOR1;    // bitangent.xyz, uv.y
    float4 param3   : INTERPOLATOR2;    // flags / datos extra
    float4 param4   : INTERPOLATOR3;    // motion vectors / datos adicionales
    float  param5   : INTERPOLATOR4;    // depth / extra
    float  param6   : NOINTERPOLATOR;   // flag sin interpolar
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    
    // Transform position to world space
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    
    // Transform to view space
    float4 viewPos = mul(worldPos, viewMatrix);
    
    // Transform to clip space
    output.Position = mul(viewPos, projectionMatrix);
    
    // Extract normal from packed float4
    float3 normal = normalize(input.normal.xyz);
    
    // Extract tangent from packed float4
    float3 tangent = normalize(input.tangent.xyz);
    
    // Transform tangent and normal to world space
    float3 worldTangent = normalize(mul(tangent, (float3x3)worldMatrix));
    float3 worldNormal = normalize(mul(normal, (float3x3)worldMatrix));
    
    // Calculate bitangent (perpendicular to tangent and normal)
    // The handedness is stored in tangent.w
    float handedness = input.tangent.w > 0.0f ? 1.0f : -1.0f;
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent) * handedness);
    
    // Pack data into output structure
    output.param1 = float4(worldTangent, input.texCoord.x);
    output.param2 = float4(worldBitangent, input.texCoord.y);
    
    // param3.x is used as a flag in the pixel shader (for normal flipping)
    // Set to positive by default
    output.param3 = float4(1.0f, 0.0f, 0.0f, 0.0f);
    
    // Motion vectors (not implemented yet, set to zero)
    output.param4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    // View space depth
    output.param5 = viewPos.z;
    
    // Additional flags
    output.param6 = 0.0f;
    
    return output;
}
