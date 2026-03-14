// Deferred Rendering Pixel Shader

// Textures
Texture2D BaseMetalMap : register(t0);
Texture2D NormalRoughnessMap : register(t1);
Texture2D AlphaTranslucentOcclusionSSSMap : register(t2);

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
    float2 uv = input.UV0;

    // --------------------------------------------------
    // Sample material maps
    float4 baseMetal = BaseMetalMap.Sample(LinearSampler, uv);
    float4 normalRough = NormalRoughnessMap.Sample(LinearSampler, uv);

    float occSelector = saturate(OCC_UVSelect);

    float2 baseUV = uv;
    float2 altUV = input.UV1;
    float2 atosUV = lerp(baseUV, altUV, occSelector);

    float4 atos = AlphaTranslucentOcclusionSSSMap.Sample(LinearSampler, atosUV);
    
    clip(atos.r - 0.5);

    // --------------------------------------------------
    // Albedo
    float3 albedo = baseMetal.rgb * BaseColor.rgb;

    // Metallic / Translucency packing
    float metallic = saturate(baseMetal.a * Metallic);
    float translucency = saturate((1.0 - atos.z * Translucency) * 0.5);

    // --------------------------------------------------
    // RT0: Display albedo for now (will be final lit color in full deferred pipeline)
    OUT.Target0 = float4(albedo, 1.0);

    // RT1: Albedo + Metallic/Translucency
    OUT.Target1.rgb = albedo;
    OUT.Target1.a = (metallic > 0.0) ? (metallic * 0.5 + 0.5) : translucency;

    // --------------------------------------------------
    // TBN reconstruction
    float3 T = normalize(input.TangentWS);
    float3 B = normalize(input.BinormalWS);
    float3 N = normalize(input.NormalWS);

    float3 mapN = normalRough.xyz * 2.0 - 1.0;

    float3 finalN = normalize(
        mapN.x * T +
        mapN.y * B +
        mapN.z * N
    );

    // For now, world space normal (should use view space with view matrix)
    float3 viewN = finalN;

    float2 encN = EncodeNormalOct(viewN);

    OUT.Target2.xy = encN;
    OUT.Target2.z = normalRough.w * Roughness;
    OUT.Target2.w = 1.0 / 3.0;

    // --------------------------------------------------
    // RT3: AO + screen UV
    OUT.Target3.x = atos.z;

    // Approximate screen UV (should use proper screen size from constant buffer)
    float2 screenUV = input.PositionCS.xy * float2(1.0/1280.0, 1.0/720.0);
    OUT.Target3.yz = screenUV;
    OUT.Target3.w = 1.0;

    return OUT;
}
