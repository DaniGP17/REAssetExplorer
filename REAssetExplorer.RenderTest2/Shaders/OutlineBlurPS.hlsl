// CodeXRE-style two-pass outer-glow outline.
//
// Stage 0 (horizontal blur):
//   Read MaskTex along +X, accumulate, write the blurred mask to BlurTex.
// Stage 1 (vertical blur + composite):
//   Read BlurTex along +Y, accumulate. Re-sample MaskTex with a 5-tap box to
//   detect "inside the silhouette" — pixels inside are either filled with
//   FillColour or discarded if FillColour.a <= 0. Outside pixels are coloured
//   OutlineColour with alpha proportional to the accumulated blur.

cbuffer OutlineBlurVars : register(b0)
{
    float4 OutlineColour;
    float4 FillColour;
    int    StepDirectionX;
    int    StepDirectionY;
    int    Stage;   // 0 = horizontal, 1 = vertical/composite
    int    Width;
};

Texture2D MaskTex : register(t0);
Texture2D BlurTex : register(t1); // only used in Stage 1

float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    float fillMask = 0;
    if (Stage == 1)
    {
        int3 ssloc = int3(pos.xy, 0);
        float4 p  = MaskTex.Load(ssloc);
        float4 p1 = MaskTex.Load(ssloc + int3( 1,  1, 0));
        float4 p2 = MaskTex.Load(ssloc + int3( 1, -1, 0));
        float4 p3 = MaskTex.Load(ssloc + int3(-1,  1, 0));
        float4 p4 = MaskTex.Load(ssloc + int3(-1, -1, 0));
        fillMask = saturate((p.r + p1.r + p2.r + p3.r + p4.r) * 0.2f);
        if ((fillMask >= 1) && (FillColour.a <= 0))
            discard;
    }

    int   width = min(Width, 8);
    int2  ss    = int2(pos.xy);
    int2  step  = int2(StepDirectionX, StepDirectionY);
    float tot   = 0;
    int   start = -width;
    for (int i = start; i <= width; i++)
    {
        int3 sp = int3(max(ss + step * i, int2(0, 0)), 0);
        float4 sv = (Stage == 1) ? BlurTex.Load(sp) : MaskTex.Load(sp);
        tot += sv.r;
    }
    float f = saturate(tot / (float) width);

    if (Stage == 1)
    {
        float4 colour = lerp(OutlineColour, FillColour, (float4) fillMask);
        return float4(colour.rgb, colour.a * f);
    }
    else
    {
        return float4(f, f, f, 1);
    }
}
