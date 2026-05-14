// Debug Pixel Shader - Write solid red to GBuffer

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
};

struct PS_OUTPUT
{
    float4 GBuffer0 : SV_TARGET0;
    float4 GBuffer1 : SV_TARGET1;
    float4 GBuffer2 : SV_TARGET2;
    float4 GBuffer3 : SV_TARGET3;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    
    // Write solid red to albedo (GBuffer1)
    output.GBuffer0 = float4(0, 0, 0, 1);
    output.GBuffer1 = float4(1, 0, 0, 1); // RED
    output.GBuffer2 = float4(0, 0, 0, 1);
    output.GBuffer3 = float4(0, 0, 0, 1);
    
    return output;
}
