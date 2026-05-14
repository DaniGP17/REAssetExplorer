// Unlit Pass Pixel Shader
// Simply outputs the albedo from GBuffer1

Texture2D<float4> GBuffer1 : register(t0);  // Albedo RGB + Metallic/Translucency
SamplerState LinearSampler : register(s0);

struct PS_INPUT
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
    // Sample albedo from GBuffer1
    float4 albedo = GBuffer1.Sample(LinearSampler, input.TexCoord);
    
    // Return albedo as-is (unlit)
    return float4(albedo.rgb, 1.0);
}
