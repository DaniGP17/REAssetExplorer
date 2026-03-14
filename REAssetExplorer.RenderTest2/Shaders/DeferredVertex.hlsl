cbuffer PerObjectConstants : register(b0)
{
    row_major float4x4 Model;
    row_major float4x4 View;
    row_major float4x4 Projection;
    row_major float4x4 ModelViewProjection;
};

struct VertexInput
{
    float3 Position  : POSITION;
    float3 Normal    : NORMAL;
    float4 Tangent   : TANGENT;
    float2 UV0       : TEXCOORD0;
    float2 UV1       : TEXCOORD1;
};

struct VertexOutput
{
    float4 PositionCS   : SV_POSITION;
    float3 PositionWS   : POSITION0;
    float3 NormalWS     : NORMAL;
    float3 TangentWS    : TANGENT;
    float3 BinormalWS   : BINORMAL;
    float2 UV0          : TEXCOORD0;
    float2 UV1          : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // Transform position to clip space (row-major matrices from C#)
    output.PositionCS = mul(float4(input.Position, 1.0f), ModelViewProjection);
    
    // Transform position to world space
    output.PositionWS = mul(float4(input.Position, 1.0f), Model).xyz;
    
    // Transform normal to world space
    output.NormalWS = normalize(mul(input.Normal, (float3x3)Model));
    
    // Transform tangent to world space
    output.TangentWS = normalize(mul(input.Tangent.xyz, (float3x3)Model));
    
    // Calculate binormal using cross product
    output.BinormalWS = cross(output.NormalWS, output.TangentWS) * input.Tangent.w;
    
    // Pass through UVs
    output.UV0 = input.UV0;
    output.UV1 = input.UV1;
    
    return output;
}
