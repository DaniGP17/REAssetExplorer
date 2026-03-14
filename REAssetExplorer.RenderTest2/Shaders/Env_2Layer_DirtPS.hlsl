Texture2D LayerMaskOcclusionMap          : register(t0);
Texture2D BaseDielectricMapBase          : register(t1);
Texture2D NormalRoughnessCavityMapBase   : register(t2);
Texture2D BaseDielectricMap1             : register(t3);
Texture2D NormalRoughnessCavityMap1      : register(t4);
Texture2D WearMap1                       : register(t5);
Texture2D DirtWearMap                    : register(t6);
Texture2D ExtraWearMap                   : register(t7);

SamplerState BilinearWrap  : register(s0, space0);

cbuffer UserMaterial : register(b0)
{
    float4 BaseColor;
    float4 UV_Tiling_Offset;
    float4 BaseColor1;
    float4 UV_Tiling_Offset1;
    float4 WearMap_Tiling_Offset1;
    float4 DirtColor1;
    float4 DirtColor2;
    float4 DirtColor3;
    float4 DirtWearMap_Tiling_Offset;
    float4 WaterLine_Color;
    float4 WaterLine_EdgeColor;

    float Occlusion_Use_SecondaryUV;
    float LayerMask_Use_SecondaryUV;
    float BaseLayer_Use_SecondaryUV;
    float ExtraLayer_Use_SecondaryUV;
    float DirtMap_Use_SecondaryUV;
    float UV_Switch;
    float Roughness;
    float MaterialTranslucency;
    float PhysicalMaterialIndex;
    float UV_Tiling;
    float Use_AdvancedUVSetting;
    float Roughness1;
    float MaterialTranslucency1;
    float PhysicalMaterialIndex1;
    float UV_Tiling1;
    float Use_AdvancedUVSetting1;
    float NormalBlend_Balance1;
    float WearMap_Tiling1;
    float Use_AdvancedUVSetting_WearMap1;
    float WearMap_Inverse1;
    float WearMap_Blend1;
    float WearMap_Normal_BlendMode1;
    float WearMap_NormalIntensity1;
    float LayerMask_BrightnessMasking1;
    float LayerMask_Brightness1;
    float LayerMask_Contrast1;
    float Dirt_Enable;
    float DirtWearMap_Inverse;
    float DirtMask_Brightness;
    float DirtMask_Contrast;
    float DirtColor_AddBlend;
    float DirtColor_Override;
    float DirtColorControl;
    float Dirt_Metallic;
    float Dirt_Roughness;
    float DirtWearMap_Tiling;
    float Use_AdvancedUVSetting_DirtWearMap;
    float Enable_Oil;
    float Oil_Intensity;
    float Oil_Thickness;
    float Oil_WearTiling;
    float Oil_WearBlend;
    float Oil_DarkColor;
    float Oil_Roughness;
    float Enable_WaterLine;
    float WaterLine_WorldHeight;
    float WaterLine_BlurWidth;
    float WaterLine_Contrast;
    float WaterLine_WearBlend;
    float WaterLine_WearTiling_Top;
    float WaterLine_WearTiling_Side;
    float WaterLine_EdgeColorOverride;
    float WaterLine_RoughnessBlend;
    float WaterLine_Roughness;
    float WaterLine_Roughness_Occluded;
    float WaterLine_NormalWeaken;
    float WaterLine_NormalWeaken_Occluded;
};

struct InputStruct
{
    float4 PositionCS   : SV_POSITION;
    float3 PositionWS   : POSITION0;
    float3 NormalWS     : NORMAL;
    float3 TangentWS    : TANGENT;
    float3 BinormalWS   : BINORMAL;
    float2 UV0          : TEXCOORD0;
    float2 UV1          : TEXCOORD1;
};

struct OutputStruct {
	float4 Target0 : SV_Target0;
	float4 Target1 : SV_Target1;
	float4 Target2 : SV_Target2;
	float4 Target3 : SV_Target3;
};

OutputStruct main(in InputStruct IN)
{
    OutputStruct OUT = (OutputStruct)0;

    // ---- TBN normalization ----
    float3 N = normalize(IN.NormalWS);
    float3 T = normalize(IN.TangentWS);
    float3 B = normalize(IN.BinormalWS);

    float3 posWS = IN.PositionWS;

    // ---- UV switch / blend ----
    // uvBase = UV_Switch*(UV0 - UV1) + UV1 = lerp(UV1, UV0, UV_Switch)
    float2 uvBase = UV_Switch * (IN.UV0 - IN.UV1) + IN.UV1;
    // uvAlt  = UV_Switch*(UV1 - UV0) + UV0 = lerp(UV0, UV1, UV_Switch)
    float2 uvAlt  = UV_Switch * (IN.UV1 - IN.UV0) + IN.UV0;

    // ExtraLayer and BaseLayer UV selection
    float2 uvDelta = uvBase - uvAlt;
    float2 uvExtra = uvDelta * ExtraLayer_Use_SecondaryUV + uvAlt;
    float2 uvBase0 = uvDelta * BaseLayer_Use_SecondaryUV  + uvAlt;

    // ---- UV tiling / offset for base layer ----
    float2 baseTiling = lerp((float2)UV_Tiling,  UV_Tiling_Offset.xy,  Use_AdvancedUVSetting);
    float2 baseOffset = Use_AdvancedUVSetting  * UV_Tiling_Offset.zw;
    float2 uvFinalBase  = uvBase0 * baseTiling + baseOffset;

    // ---- UV tiling / offset for extra layer ----
    float2 extraTiling = lerp((float2)UV_Tiling1, UV_Tiling_Offset1.xy, Use_AdvancedUVSetting1);
    float2 extraOffset = Use_AdvancedUVSetting1 * UV_Tiling_Offset1.zw;
    float2 uvFinalExtra = uvExtra * extraTiling + extraOffset;

    // ---- LayerMask / Occlusion map ----
    float4 lmoBase = LayerMaskOcclusionMap.Sample(BilinearWrap, uvBase);
    float4 lmoAlt  = LayerMaskOcclusionMap.Sample(BilinearWrap, uvAlt);
    // phi: if (LayerMask_Use_SecondaryUV * Occlusion_Use_SecondaryUV) > 0 use uvBase sample, else uvAlt
    float4 lmoPhi  = (LayerMask_Use_SecondaryUV * Occlusion_Use_SecondaryUV > 0.0) ? lmoBase : lmoAlt;
    float layerMaskR = lerp(lmoPhi.x, lmoBase.x, LayerMask_Use_SecondaryUV); // blended mask R
    float layerMaskG = lerp(lmoPhi.z, lmoBase.z, LayerMask_Use_SecondaryUV); // blended mask Z (cavity/AO)
    // Occlusion value (written to Target3.z)
    float occValue   = lerp(lmoPhi.w, lmoBase.w, Occlusion_Use_SecondaryUV);

    // ---- Texture samples ----
    float4 bdBase   = BaseDielectricMapBase.Sample(BilinearWrap, uvFinalBase);
    float4 nrcBase  = NormalRoughnessCavityMapBase.Sample(BilinearWrap, uvFinalBase);
    float4 bdExtra  = BaseDielectricMap1.Sample(BilinearWrap, uvFinalExtra);
    float4 nrcExtra = NormalRoughnessCavityMap1.Sample(BilinearWrap, uvFinalExtra);

    // ---- WearMap1 UV (with its own tiling) ----
    float2 wearTiling1 = lerp((float2)WearMap_Tiling1, WearMap_Tiling_Offset1.xy, Use_AdvancedUVSetting_WearMap1);
    float2 wearOffset1 = Use_AdvancedUVSetting_WearMap1 * WearMap_Tiling_Offset1.zw;
    float2 uvWear1     = uvExtra * wearTiling1 + wearOffset1;
    float4 wearSample  = WearMap1.Sample(BilinearWrap, uvWear1);

    // ---- Wear mask blend factor computation ----
    float wearNInten   = WearMap_NormalIntensity1 * 4.0 - 2.0;
    float layerBright  = LayerMask_Brightness1 * 2.0 - 1.0;

    // Invert wear per WearMap_Inverse1
    float wearInverted = (wearSample.x * -2.0 + 1.0) * WearMap_Inverse1 + wearSample.x;

    float lm_raw   = layerMaskR + layerBright;
    float lm_sat2  = saturate(layerMaskR * 2.0);
    float lm_mask  = (lm_sat2 * layerBright - layerBright) * LayerMask_BrightnessMasking1;
    float lmAdj    = lm_raw + lm_mask;
    float lm_sum   = lmAdj + wearInverted;

    // Screen-blend mode style saturation
    float oneMinusWear = 1.0 - wearInverted;
    float screenBlend  = lm_sum > 1.0 ? 1.0 : (lmAdj > 0.0 ? lmAdj / oneMinusWear : 0.0);

    // Contrast-based final wear blend
    float contrast05   = LayerMask_Contrast1 * 0.5;
    float blendAdj     = (screenBlend - lmAdj) * WearMap_Blend1 + lmAdj - contrast05;
    float contrastRng  = 1.0 - contrast05 - contrast05;
    float contrastSign = contrastRng < 0.0 ? -1.0 : 1.0;
    float contrastAbs  = max(0.0001, abs(contrastRng));
    float wearBlend    = saturate(blendAdj / (contrastAbs * contrastSign)); // _640

    // ---- Decode tangent-space normals (ONN-style) ----
    // NRC channels: r=roughness, g+a=packed normal XY, b=cavity
    // Decode nrcExtra (NormalRoughnessCavityMap1):
    float2 nExy = float2(nrcExtra.y, nrcExtra.w) * 2.0 - 1.0;
    float  nEfx = nExy.x * nExy.x * (nExy.x > 0.0 ? 1.0 : -1.0);
    float  nEfy = nExy.y * nExy.y * (nExy.y > 0.0 ? 1.0 : -1.0);
    float  nEp  = (nEfx + nEfy) * 0.5;
    float  nEq  = (nEfx - nEfy) * 0.5;
    float3 nExtra = normalize(float3(nEp, nEq, 1.0 - abs(nEp) - abs(nEq)));

    // Decode nrcBase (NormalRoughnessCavityMapBase):
    float2 nBxy = float2(nrcBase.y, nrcBase.w) * 2.0 - 1.0;
    float  nBfx = nBxy.x * nBxy.x * (nBxy.x > 0.0 ? 1.0 : -1.0);
    float  nBfy = nBxy.y * nBxy.y * (nBxy.y > 0.0 ? 1.0 : -1.0);
    float  nBp  = (nBfx + nBfy) * 0.5;
    float  nBq  = (nBfx - nBfy) * 0.5;
    float3 nBase = normalize(float3(nBp, nBq, 1.0 - abs(nBp) - abs(nBq)));

    // ---- Base color / roughness / metallic ----
    float3 colorBase  = bdBase.xyz  * BaseColor.xyz;
    float3 colorExtra = bdExtra.xyz * BaseColor1.xyz;
    float  metBase    = 1.0 - bdBase.w;
    float  rghBase    = nrcBase.x  * Roughness;
    float  rghExtra   = nrcExtra.x * Roughness1;

    // Blend by wearBlend
    float  metallic      = 1.0 - lerp(bdBase.w, bdExtra.w, wearBlend);
    float3 colorBlended  = lerp(colorBase, colorExtra, wearBlend);
    float  rghBlended    = lerp(rghBase, rghExtra, wearBlend);

    // ---- Waterline depth factor ----
    float wlAbsBlur = max(0.0001, abs(WaterLine_BlurWidth));
    float wlSign    = WaterLine_BlurWidth > 0.0 ? -1.0 : 1.0;
    float wl        = saturate((posWS.y - WaterLine_WorldHeight - WaterLine_BlurWidth) / (wlAbsBlur * wlSign));

    // ---- Dirt wear map ----
    float2 dirtTiling  = lerp((float2)DirtWearMap_Tiling, DirtWearMap_Tiling_Offset.xy, Use_AdvancedUVSetting_DirtWearMap);
    float2 dirtOffset  = Use_AdvancedUVSetting_DirtWearMap * DirtWearMap_Tiling_Offset.zw;
    float2 uvDirtBase2 = DirtMap_Use_SecondaryUV * (IN.UV1 - IN.UV0) + IN.UV0; // lerp(UV0, UV1, DirtMap_Use_SecondaryUV)
    float2 uvDirtFinal = uvDirtBase2 * dirtTiling + dirtOffset;

    float dirtWearRaw = 0.0;
    if (DirtMask_Brightness * Dirt_Enable > 0.0)
        dirtWearRaw = DirtWearMap.Sample(BilinearWrap, uvDirtFinal).x;

    float dirtMask = (dirtWearRaw * -2.0 + 1.0) * DirtWearMap_Inverse + dirtWearRaw;

    // ---- Dirt color ramp ----
    float dirtAbs    = abs(DirtColorControl - 0.5) * 20.0;
    float dirtPow    = exp2(dirtAbs);
    float dirtHigh   = DirtColorControl < 0.5 ? 0.0 : 1.0;
    float dirtSqrt   = exp2(log2(max(1.0 / dirtPow, 1e-6)) * 0.5);
    float dirtGamma  = (dirtPow - dirtSqrt) * dirtHigh + dirtSqrt;
    float dirtM01    = exp2(log2(max(dirtMask, 1e-6)) * dirtGamma);
    float dirtT2     = dirtM01 * 2.0;
    float dirtT1     = dirtT2 - 1.0;
    float3 dirtLow   = dirtT2 * (DirtColor2.xyz - DirtColor1.xyz) + DirtColor1.xyz;
    float3 dirtHigh3 = dirtT1 * (DirtColor3.xyz - DirtColor2.xyz) + DirtColor2.xyz;
    float3 dirtColor = lerp(dirtLow, dirtHigh3, dirtM01 < 0.5 ? 0.0 : 1.0);

    // ---- Dirt blend factor ----
    float dirtBright    = DirtMask_Brightness * 2.0 - 1.0;
    float dirtCombined  = saturate(dirtBright + layerMaskG * Dirt_Enable);
    float dirtMulA      = saturate(dirtCombined * 2.0 - 1.0) * (1.0 - dirtMask);
    float dirtMulB      = saturate(dirtCombined * 2.0) * dirtMask;

    float3 dirtMulCol  = dirtColor * colorBlended;
    float3 dirtScrCol  = dirtColor + colorBlended - dirtMulCol;
    float3 dirtBlended = lerp(dirtMulCol, dirtScrCol, DirtColor_AddBlend);
    float3 dirtOverride = (dirtColor - dirtBlended) * DirtColor_Override + dirtBlended;

    float dirtContrastH  = DirtMask_Contrast * 0.5;
    float dirtNumFinal   = dirtMulA + dirtMulB - dirtContrastH;
    float dirtDenomRaw   = 1.0 - DirtMask_Contrast;
    float dirtDenomSign  = dirtDenomRaw < 0.0 ? -1.0 : 1.0;
    float dirtDenomAbs   = max(0.0001, abs(dirtDenomRaw));
    float dirtFinal      = saturate(dirtNumFinal / (dirtDenomAbs * dirtDenomSign));

    float3 finalColor = lerp(colorBlended, dirtOverride, dirtFinal);
    float  rghFinal   = lerp(rghBlended,   rghBlended * Dirt_Roughness,  dirtFinal);
    float  metFinal   = lerp(metallic,     metallic   * Dirt_Metallic,   dirtFinal);

    // ---- Oil effect ----
    // Enable_Oil > 0: iridescent sheen on surface using ExtraWearMap
    if (Enable_Oil > 0.0)
    {
        float2 uvOil = uvBase0 * Oil_WearTiling;
        float  oilWear = ExtraWearMap.Sample(BilinearWrap, uvOil).x;
        float  oilT    = oilWear * 2.0 - 1.0;
        float  oilThick = oilT * Oil_WearBlend + Oil_Thickness;

        // Approximate NdotV with a fixed view direction (no camera pos available)
        float3 worldN = normalize(N * 1.0); // use vertex normal as approximation
        // iridescence: cos-based spectral shift
        float NdotV = abs(dot(worldN, float3(0, 0, 1))); // placeholder view dir
        float oilFreq   = NdotV * oilThick;
        float oilR = (cos(oilFreq * 35.0000)  * -0.5 + 0.5);
        float oilG = (cos(oilFreq * 24.8500)  * -0.5 + 0.5);
        float oilB = (cos(oilFreq * 30.4500)  * -0.5 + 0.5);
        float3 oilIrid = float3(oilR, oilG, oilB);
        float3 oilDark = (float3)(1.0 - Oil_DarkColor);
        float3 oilEffect = (oilIrid * oilIrid * oilDark * 2.0 - oilDark) * Oil_Intensity;
        finalColor = finalColor * oilDark + oilEffect * finalColor;
        rghFinal   = min(rghFinal, Oil_Roughness);
    }

    // ---- WaterLine effect ----
    float3 wlF = (float3)0.0;
    if (Enable_WaterLine > 0.0)
    {
        float wlFrac = wl;
        if (frac(wl) > 0.0)
        {
            // Triplanar ExtraWearMap sample
            float ndotRight = dot(N, float3(1, 0, 0));
            float absNR     = abs(ndotRight);
            float sigmoid   = 1.0 / (exp2((absNR - 0.5) * -15.8217) + 1.0);
            float wlSide    = saturate(sigmoid);

            float sZa = ExtraWearMap.Sample(BilinearWrap, float2(posWS.x, posWS.y) * WaterLine_WearTiling_Side).z;
            float sZb = ExtraWearMap.Sample(BilinearWrap, float2(posWS.z, posWS.y) * WaterLine_WearTiling_Side).z;
            float ndotUp = dot(N, float3(0, 1, 0));
            float sGtop  = ExtraWearMap.Sample(BilinearWrap, float2(posWS.x, posWS.z) * WaterLine_WearTiling_Top).y;

            float blendSides = (absNR - 0.5) * 0.5 + 0.5;
            float wlSamp = lerp(sZa, sZb, blendSides);
            float wlSamp2 = lerp(wlSamp, sGtop, abs(ndotUp));

            float wl2  = wl * 2.0 - 1.0;
            float wlMix = (1.0 - wlSamp2) * saturate(wl2) + saturate(wl * 2.0) * wlSamp2;
            wlFrac = wlMix;
        }

        float wlBlend3 = (wlFrac - wl) * WaterLine_WearBlend + wl;
        float wlOff    = wlBlend3 - 0.5;
        float wlConLg  = log2(max(WaterLine_Contrast, 1e-6)) * 10.0;
        float wlPow2   = exp2(wlConLg) * 990.0 + 10.0;
        float wlExpV   = exp2(wlOff * -1.44270 * wlPow2);
        float wlSig    = saturate(1.0 / (wlExpV + 1.0));
        float wlFV     = lerp(wlBlend3, wlSig, WaterLine_Contrast);
        wlF = (float3)wlFV;
    }

    // ---- Normal blending (wearBlend between nBase and nExtra) ----
    // Apply WearMap1.gb scaled offset to nExtra
    float2 wearNOffset = saturate((wearSample.yz * 1.25 - 0.625) * wearNInten);
    float3 nExtraOff   = normalize(float3(nExtra.x + wearNOffset.x,
                                          nExtra.y + wearNOffset.y,
                                          nExtra.z));
    // Blend normal offsets by WearMap_Normal_BlendMode1
    float3 nExtraDiff  = nExtraOff - nExtra;
    float3 nExtraMod   = nExtra + nExtraDiff * WearMap_Normal_BlendMode1;

    // Blend nExtraMod vs nBase by wearBlend (two paths: simple lerp and normalized-lerp, blended by NormalBlend_Balance1)
    float3 nSimple = float3(lerp(nBase.xy, nExtraMod.xy, wearBlend), nBase.z);
    float3 nNorm   = normalize(float3(nExtraMod.xy * wearBlend + nBase.xy,
                                      nBase.z));
    float3 nBlend  = float3(lerp(nNorm.x, nSimple.x, NormalBlend_Balance1),
                             lerp(nNorm.y, nSimple.y, NormalBlend_Balance1),
                             lerp(nNorm.z, nSimple.z, NormalBlend_Balance1));
    float3 nFinal  = nBlend; // (_1049,_1050,_1051) tangent-space

    // ---- WaterLine normal weakening ----
    // _1196 = WaterLine_NormalWeaken - WaterLine_NormalWeaken_Occluded
    // _1198 = occValue * _1196 + WaterLine_NormalWeaken_Occluded  → lerp by occValue
    // scale nFinal toward view normal by wlF * nWeaken
    float nWeaken = Enable_WaterLine > 0.0
                    ? lerp(WaterLine_NormalWeaken_Occluded, WaterLine_NormalWeaken, occValue)
                    : 0.0;
    float3 nFinalWL = nFinal - float3(nFinal.x - 0.0, nFinal.y - 0.0, nFinal.z - 1.0) * (wlF * nWeaken);

    // ---- TBN → world space normal ----
    // nFinalWL is tangent-space; transform to world space
    float3 worldNormal = normalize(T * nFinalWL.x + B * nFinalWL.y + N * nFinalWL.z);

    // ---- Roughness and translucency final blend ----
    float translucency = lerp(MaterialTranslucency, MaterialTranslucency1, wearBlend);
    float roughnessFinal = max(0.0, rghFinal);

    // ---- WaterLine color/roughness blend ----
    if (Enable_WaterLine > 0.0)
    {
        // Roughness lerp: lerp(rghFinal, wl_rgh_occluded_blend, wlRoughnessBlend * wlF)
        float wlRghBase  = lerp(WaterLine_Roughness_Occluded, WaterLine_Roughness, occValue);
        float wlRghAlpha = saturate(WaterLine_RoughnessBlend * wlF.x);
        roughnessFinal   = lerp(roughnessFinal, wlRghBase, wlRghAlpha);

        // Color: edgeColor override + main color blend
        float3 wlEdgeColor = lerp(WaterLine_EdgeColor.xyz,
                                  WaterLine_EdgeColor.xyz * finalColor,
                                  WaterLine_EdgeColorOverride);
        // wlF squared blend for edge, linear for base water color
        float3 wlEdgeBlend = wlEdgeColor  * wlF * wlF;
        float3 wlBasBlend  = WaterLine_Color.xyz * wlF;
        float3 wlColorDiff = wlBasBlend - wlEdgeBlend - finalColor;
        finalColor = finalColor + wlColorDiff * wlF;
    }

    // ---- Encode outputs ----

    // --- Target1: color RGB + packed roughness/translucency ---
    // roughness is encoded: sat(roughnessFinal * 0.04 / sqrt(roughnessFinal)) style or direct
    // Looking at original: _1235 from label13/14:
    //   label13 (metFinal > 0 OR roughness <= 0.0333): roughness = max(min(_1217*0.04, 0.08), metFinal)
    //   label14: round-quantized roughness + cavity encode
    // _1217 = lerp(_655=NRCBase.b, _577=NRC1.b, wearBlend) = cavity blended
    float cavityBlended = lerp(nrcBase.z, nrcExtra.z, wearBlend);

    float roughEncoded;
    bool  useSimpleRough = (metFinal > 0.0) || (roughnessFinal <= 0.0333333);
    if (useSimpleRough)
    {
        float roughQ = min(0.08, cavityBlended * 0.04);
        roughEncoded = max(roughQ, metFinal);
    }
    else
    {
        float rQ = floor(roughnessFinal * 15.49 + 0.5) * 0.0627451;
        float cQ = saturate(floor(saturate(cavityBlended * 0.5) * 15.49 + 0.5) * 0.00392157);
        roughEncoded = rQ + cQ;
    }

    // _1236 = 0.666667 (metFinal > 0 → label13) else 0 (label14)
    float gbufType = useSimpleRough ? 0.666667 : 0.0;

    // --- Target2.xy: world-space normal encoded as octahedral in [0,1] ---
    // _1208,_1209,_1210 = worldNormal after waterline weaken but before TBN — actually
    // the original uses nFinalWL then transforms. We use worldNormal.
    // Octahedral: project onto L1 sphere
    float normL1     = abs(worldNormal.x) + abs(worldNormal.y) + abs(worldNormal.z);
    float3 octN      = worldNormal / normL1;
    float2 octXZ;
    // If worldNormal.y <= 0: fold octahedral
    {
        float ox = octN.x;
        float oz = octN.z;
        float2 folded = float2((1.0 - abs(oz)) * (ox >= 0.0 ?  1.0 : -1.0),
                               (1.0 - abs(ox)) * (oz >= 0.0 ?  1.0 : -1.0));
        octXZ = worldNormal.y <= 0.0 ? folded : float2(ox, oz);
    }
    float2 normalEnc = octXZ * 0.5 + 0.5;

    OUT.Target0 = float4(finalColor, roughEncoded);
    OUT.Target1 = float4(normalEnc, metFinal, gbufType);
    OUT.Target2 = float4(0, 0, occValue, 1.0);

    return OUT;
}