// Deferred Lighting Pass Pixel Shader
// Reads G-Buffers and computes final lighting

Texture2D<float4> GBuffer0 : register(t0);  // Emissive / Lighting accumulation
Texture2D<float4> GBuffer1 : register(t1);  // Albedo + Metallic
Texture2D<float4> GBuffer2 : register(t2);  // Normal + Roughness
Texture2D<float4> GBuffer3 : register(t3);  // AO + ScreenUV + Flags

SamplerState pointSampler : register(s0);

struct PS_INPUT
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Decode octahedral normal
float3 DecodeNormalOct(float2 enc)
{
    enc = enc * 2.0 - 1.0;
    float3 n = float3(enc.x, enc.y, 1.0 - abs(enc.x) - abs(enc.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

float4 main(PS_INPUT input) : SV_Target
{
    // Sample G-Buffers
    float4 gb0 = GBuffer0.Sample(pointSampler, input.texCoord);
    float4 gb1 = GBuffer1.Sample(pointSampler, input.texCoord);
    float4 gb2 = GBuffer2.Sample(pointSampler, input.texCoord);
    float4 gb3 = GBuffer3.Sample(pointSampler, input.texCoord);
    
    // Extract data from G-Buffers
    float3 emissive = gb0.rgb;
    float3 albedo = gb1.rgb;
    float metallic = gb1.a;
    
    // Decode normal from octahedral encoding
    float2 normalEnc = gb2.xy;
    float3 worldNormal = DecodeNormalOct(normalEnc);
    float roughness = gb2.z;
    float gbufferType = gb2.w;
    
    float ao = gb3.x;
    
    // Simple lighting calculation (HDR)
    // Light direction in world space (pointing towards the light)
    float3 lightDir = normalize(float3(-0.5, 1.0, -0.3));
    float lightIntensity = 1.0; // HDR light intensity (reduced for more natural look)
    
    // Diffuse
    float ndotl = max(dot(worldNormal, lightDir), 0.0);
    float3 diffuse = albedo * (ndotl * lightIntensity + 0.10); // Ambient contribution
    
    // Apply AO
    diffuse *= ao;
    
    // Specular (simplified PBR)
    // For proper specular, we would need camera position, but for now use a simplified approach
    float3 halfVec = normalize(lightDir);
    float ndoth = max(dot(worldNormal, halfVec), 0.0);
    float specPower = (1.0 - roughness) * 128.0 + 1.0;
    float specular = pow(ndoth, specPower) * metallic * lightIntensity;
    
    // Combine (HDR - values can exceed 1.0)
    float3 finalColor = diffuse + specular * 0.2 + emissive;
    
    // Return HDR color (no clamping)
    return float4(finalColor, 1.0);
}
