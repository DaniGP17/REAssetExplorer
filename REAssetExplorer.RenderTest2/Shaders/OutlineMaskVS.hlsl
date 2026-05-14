cbuffer PerObjectConstants : register(b0)
{
    row_major float4x4 Model;
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ModelViewProjection;
};

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV0      : TEXCOORD0;
    float2 UV1      : TEXCOORD1;
};

float4 main(VertexInput input) : SV_POSITION
{
    return mul(float4(input.Position, 1.0f), ModelViewProjection);
}
