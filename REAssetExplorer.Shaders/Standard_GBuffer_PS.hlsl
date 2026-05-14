SamplerState AutomaticWrap : register(s0);

Texture2D<float4> BaseMetalMap : register(t0);                      // RGB: Base Color, A: Metallic
Texture2D<float4> NormalRoughnessMap : register(t1);                // RGB: Normal Map, A: Roughness
Texture2D<float4> AlphaTranslucentOcclusionSSSMap : register(t2);   // R: Alpha, G: Translucency, B: AO, A: SSS

cbuffer SceneInfo : register(b0)
{
    float4x4 viewProjMat;           // Matriz view-projection
    float4x4 viewMat;               // Matriz view
    float4x4 viewInvMat;            // Inversa de view
    float4x4 projMat;               // Matriz projection
    float4x4 projInvMat;            // Inversa de projection
    float4x4 viewProjInvMat;        // Inversa de view-projection
    float4x4 prevViewProjMat;       // View-projection del frame anterior
    float3   prevCameraPos;         // Posición de cámara anterior
    float    cameraNearPlane;       // Plano cercano de la cámara
    float3   prevCameraDir;         // Dirección de cámara anterior
    float    cameraFarPlane;        // Plano lejano de la cámara
    float4   viewFrustum[6];        // Planos del frustum (6 planos)
    float3   ZToLinear;             // Parámetros para convertir Z a lineal
    float    subdivisionLevel;      // Nivel de subdivisión (LOD)
    float2   screenSize;            // Tamaño de pantalla en píxeles
    float2   screenInverseSize;     // Inversa del tamaño (1/width, 1/height)
};

cbuffer GBufferType : register(b1)
{
    float gbufferTypeFlag;          // Flag que indica tipo de GBuffer (0-3)
    float3 _gbufferPad;             // Padding para alineación
};

cbuffer UserMaterial : register(b2)
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
    float4 Position : SV_Position;      // Posición en screen space
    float4 param1   : INTERPOLATOR0;    // tangent.xyz, uv.x
    float4 param2   : INTERPOLATOR1;    // bitangent.xyz, uv.y
    float4 param3   : INTERPOLATOR2;    // flags / datos extra
    float4 param4   : INTERPOLATOR3;    // motion vectors / datos adicionales
    float  param5   : INTERPOLATOR4;    // depth / extra
    float  param6   : NOINTERPOLATOR;   // flag sin interpolar
};

struct OutputStruct
{
    float4 Target0 : SV_Target0;    // RT0: Emisivo / Lighting acum
    float4 Target1 : SV_Target1;    // RT1: Albedo RGB + Metallic/Translucency
    float4 Target2 : SV_Target2;    // RT2: Normal (encoded) + Roughness + GBuffer type
    float4 Target3 : SV_Target3;    // RT3: AO + Screen UV + flags
};

float2 EncodeNormalOct(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    
    float2 enc = n.xy;
    
    if (n.z < 0.0)
        enc = (1.0 - abs(enc.yx)) * sign(enc.xy);
    
    return enc * 0.5 + 0.5;
}

OutputStruct main(in InputStruct IN)
{
    OutputStruct OUT = (OutputStruct)0;

    OUT.Target0 = float4(0, 0, 0, 0);

    float2 uv = float2(IN.param1.w, IN.param2.w);

    float4 baseMetal = BaseMetalMap.Sample(AutomaticWrap, uv);
    float4 normalRough = NormalRoughnessMap.Sample(AutomaticWrap, uv);
    float occSelector = saturate(OCC_UVSelect);

    float2 baseUV = uv;
    float2 altUV = IN.param3.yz;
    float2 atosUV = lerp(baseUV, altUV, occSelector);
    float ao = AlphaTranslucentOcclusionSSSMap
            .Sample(AutomaticWrap, atosUV)
            .z;

    float3 albedo = baseMetal.rgb * BaseColor.rgb;

    float metallic = saturate(baseMetal.a * Metallic);
    float translucency = saturate((1.0 - ao * Translucency) * 0.5);

    OUT.Target1.rgb = albedo;
    OUT.Target1.a   = (metallic > 0.0) ? (metallic * 0.5 + 0.5) : translucency;

    float3 T = normalize(IN.param1.xyz);
    float3 B = normalize(IN.param2.xyz);
    float3 N = normalize(cross(T, B));

    if (IN.param3.x < 0.0)
        N = -N;

    float3 mapN = normalRough.xyz * 2.0 - 1.0;
    
    float3 finalN = normalize(
        mapN.x * T +
        mapN.y * B +
        mapN.z * N
    );

    // Keep normal in world space for lighting calculations
    float2 encN = EncodeNormalOct(finalN);

    OUT.Target2.xy = encN;
    OUT.Target2.z  = normalRough.w * Roughness;
    OUT.Target2.w  = gbufferTypeFlag * (1.0 / 3.0);

    OUT.Target3.x = ao;

    float2 screenUV = IN.Position.xy * screenInverseSize;
    OUT.Target3.yz = screenUV;
    OUT.Target3.w  = 1.0;   // Flag / Alpha

    return OUT;
}
