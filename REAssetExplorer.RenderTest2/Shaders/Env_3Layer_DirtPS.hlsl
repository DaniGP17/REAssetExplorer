// Deferred Rendering Pixel Shader

// Textures
Texture2D LayerMaskOcclusionMap : register(t0);
Texture2D BaseDielectricMapBase : register(t1);
Texture2D NormalRoughnessCavityMapBase : register(t2);
Texture2D BaseDielectricMap : register(t3);
Texture2D NormalRoughnessCavityMap : register(t4);
Texture2D WearMap : register(t5);
Texture2D BaseDielectricMap2 : register(t6);
Texture2D NormalRoughnessCavityMap2 : register(t7);
Texture2D WearMap2 : register(t8);
Texture2D DirtWearMap : register(t9);
Texture2D ExtraWearMap : register(t10);

// Samplers
SamplerState LinearSampler : register(s0);

// Pixel input structure (from vertex shader)
struct PixelInput
{
    float4 PositionCS   : SV_POSITION;
    float3 PositionWS   : POSITION0;
    float3 NormalWS     : NORMAL;
    float3 TangentWS    : TANGENT;
    float3 BinormalWS   : BINORMAL;
    float2 UV0          : TEXCOORD0;
    float2 UV1          : TEXCOORD1;
};

cbuffer UserMaterial : register(b0)
{
    float4 BaseColor;
    float  Metallic;
    float  Roughness;
    float  Translucency;
    float  AlphaTestRef;
    float  OCC_UVSelect;
};

struct OutputStruct
{
    float4 Target0 : SV_Target0;
    float4 Target1 : SV_Target1;
    float4 Target2 : SV_Target2;
    float4 Target3 : SV_Target3;
};

float2 EncodeNormalOct(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 enc = n.xy;
    if (n.z < 0.0)
        enc = (1.0 - abs(enc.yx)) * sign(enc.xy);
    return enc * 0.5 + 0.5;
}

OutputStruct main(PixelInput input)
{
    OutputStruct OUT = (OutputStruct)0;

    // --------------------------------------------------
    // UVs
    float2 uv0 = input.UV0;
    float2 uv1 = input.UV1;

    float2 uvOcc = lerp(uv0, uv1, saturate(OCC_UVSelect));

    // --------------------------------------------------
    // Samples
    float4 layerMaskOcc = LayerMaskOcclusionMap.Sample(LinearSampler, uvOcc);

    float w1  = saturate(layerMaskOcc.r);
    float w2  = saturate(layerMaskOcc.g);
    float occ = saturate(layerMaskOcc.a);

    float wear1 = saturate(WearMap.Sample(LinearSampler, uv0).r);
    float wear2 = saturate(WearMap2.Sample(LinearSampler, uv0).r);
    float dirt  = saturate(DirtWearMap.Sample(LinearSampler, uv0).r);
    float extra = saturate(ExtraWearMap.Sample(LinearSampler, uv0).r);

    w1 *= wear1;
    w2 *= wear2;

    float wSum = saturate(w1 + w2);
    float wBase = 1.0 - wSum;

    // --------------------------------------------------
    // Base color layers
    float4 base0 = BaseDielectricMapBase.Sample(LinearSampler, uv0);
    float4 base1 = BaseDielectricMap.Sample(LinearSampler, uv0);
    float4 base2 = BaseDielectricMap2.Sample(LinearSampler, uv0);

    float4 baseCol = base0 * wBase + base1 * w1 + base2 * w2;

    float dirtDarken = lerp(1.0, 0.55, dirt);
    baseCol.rgb *= dirtDarken;
    baseCol.rgb = lerp(baseCol.rgb, baseCol.rgb * 1.10, extra);

    baseCol.rgb *= BaseColor.rgb;
    baseCol.a   *= BaseColor.a;

    clip(baseCol.a - AlphaTestRef);

    // --------------------------------------------------
    // Normal / Roughness
    float4 nrc0 = NormalRoughnessCavityMapBase.Sample(LinearSampler, uv0);
    float4 nrc1 = NormalRoughnessCavityMap.Sample(LinearSampler, uv0);
    float4 nrc2 = NormalRoughnessCavityMap2.Sample(LinearSampler, uv0);

    float4 nrc = nrc0 * wBase + nrc1 * w1 + nrc2 * w2;

    float2 nXY = nrc.rg * 2.0 - 1.0;
    float  nZ  = sqrt(saturate(1.0 - dot(nXY, nXY)));
    float3 nTS = normalize(float3(nXY, nZ));

    float3 T = normalize(input.TangentWS);
    float3 B = normalize(input.BinormalWS);
    float3 N = normalize(input.NormalWS);

    float3 nWS = normalize(T * nTS.x + B * nTS.y + N * nTS.z);

    float roughTex = saturate(nrc.b);
    float cavity   = saturate(nrc.a);

    roughTex = saturate(roughTex + dirt * 0.35);
    cavity   = saturate(cavity * lerp(1.0, 0.75, dirt));

    float roughFinal = saturate(roughTex * Roughness);
    float metalFinal = saturate(Metallic);

    // --------------------------------------------------
    // Target0 (igual que el otro shader)
    OUT.Target0 = float4(baseCol.rgb, 1.0);

    // --------------------------------------------------
    // Target1 (Albedo + metallic/translucency)
    OUT.Target1.rgb = baseCol.rgb;
    OUT.Target1.a = (metalFinal > 0.0) ? (metalFinal * 0.5 + 0.5) : saturate(Translucency);

    // --------------------------------------------------
    // Target2 (normal encoded + roughness)
    float2 encN = EncodeNormalOct(nWS);

    OUT.Target2.xy = encN;
    OUT.Target2.z  = roughFinal;
    OUT.Target2.w  = 1.0 / 3.0;

    // --------------------------------------------------
    // Target3 (AO + screenUV)
    OUT.Target3.x = occ;

    float2 screenUV = input.PositionCS.xy * float2(1.0/1280.0, 1.0/720.0);
    OUT.Target3.yz = screenUV;
    OUT.Target3.w  = 1.0;

    return OUT;
}