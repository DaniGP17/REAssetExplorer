// Debug Vertex Shader - Simple pass-through

cbuffer SceneInfo : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix WorldViewProjection;
    float4 CameraPosition;
    float4 ViewportSize;
};

struct VS_INPUT
{
    float3 Position : POSITION0;
    float4 Normal : NORMAL0;
    float4 Tangent : TANGENT0;
    float2 TexCoord0 : TEXCOORD0;
    float2 TexCoord1 : TEXCOORD1;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    // Transform position to clip space
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, mul(View, Projection));
    output.TexCoord = input.TexCoord0;
    
    return output;
}
