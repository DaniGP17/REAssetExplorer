// Tone Mapping Pixel Shader
// Converts HDR color to LDR with tone mapping

Texture2D<float4> HDRTexture : register(t0);
SamplerState PointSampler : register(s0);

cbuffer ToneMappingParams : register(b0)
{
    float Exposure;          // Exposure adjustment (default: 1.0)
    float Gamma;             // Gamma correction (default: 2.2)
    int ToneMappingMode;     // 0 = Reinhard, 1 = ACES, 2 = Uncharted 2
    float Padding;
};

struct PS_INPUT
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// ACES Filmic Tone Mapping
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilmic(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reinhard Tone Mapping
float3 Reinhard(float3 color)
{
    return color / (1.0 + color);
}

// Uncharted 2 Tone Mapping
// Reference: http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 Uncharted2Tonemap(float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2(float3 color)
{
    float exposureBias = 2.0;
    float3 curr = Uncharted2Tonemap(exposureBias * color);
    float3 W = float3(11.2, 11.2, 11.2);
    float3 whiteScale = 1.0 / Uncharted2Tonemap(W);
    return curr * whiteScale;
}

// Gamma correction
float3 GammaCorrect(float3 color, float gamma)
{
    return pow(color, 1.0 / gamma);
}

float4 main(PS_INPUT input) : SV_Target
{
    // Sample HDR color
    float3 hdrColor = HDRTexture.Sample(PointSampler, input.texCoord).rgb;
    
    // Apply exposure
    hdrColor *= Exposure;
    
    // Apply tone mapping
    float3 ldrColor;
    
    if (ToneMappingMode == 0)
    {
        // Reinhard
        ldrColor = Reinhard(hdrColor);
    }
    else if (ToneMappingMode == 1)
    {
        // ACES (default)
        ldrColor = ACESFilmic(hdrColor);
    }
    else if (ToneMappingMode == 2)
    {
        // Uncharted 2
        ldrColor = Uncharted2(hdrColor);
    }
    else
    {
        // Fallback to ACES
        ldrColor = ACESFilmic(hdrColor);
    }
    
    // Apply gamma correction
    ldrColor = GammaCorrect(ldrColor, Gamma);
    
    return float4(ldrColor, 1.0);
}
