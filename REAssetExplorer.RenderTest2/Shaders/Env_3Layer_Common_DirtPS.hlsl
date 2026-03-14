Texture2D NormalRoughnessCavityMap       : register(t0);
Texture2D BaseAlphaMap                   : register(t1);
Texture2D LayerMaskOcclusionMap          : register(t2);
Texture2D BaseDielectricMapBase          : register(t3);
Texture2D NormalRoughnessCavityMapBase   : register(t4);
Texture2D BaseDielectricMap              : register(t5);
Texture2D NormalRoughnessCavityMap1      : register(t6);
Texture2D WearMap1                       : register(t7);
Texture2D BaseDielectricMap2			 : register(t8);
Texture2D NormalRoughnessCavityMap2      : register(t9);
Texture2D WearMap2                       : register(t10);
Texture2D DirtWearMap                    : register(t11);
Texture2D ExtraWearMap                   : register(t12);

SamplerState BilinearWrap  : register(s0, space0);

cbuffer UserMaterial : register(b0)
{
    float4 BaseAlphaMap_Tiling_Offset;
    float4 BaseColor;
    float4 UV_Tiling_Offset;
    float4 BaseColor1;
    float4 UV_Tiling_Offset1;
    float4 WearMap_Tiling_Offset1;
    float4 BaseColor2;
    float4 UV_Tiling_Offset2;
    float4 WearMap_Tiling_Offset2;
    float4 DirtColor1;
    float4 DirtColor2;
    float4 DirtColor3;
    float4 DirtWearMap_Tiling_Offset;
    float4 WaterLine_Color;
    float4 WaterLine_EdgeColor;

    float Occlusion_Use_SecondaryUV;
    float MaterialColor_Use_SecondaryUV;
    float LayerMask_Use_SecondaryUV;
    float BaseLayer_Use_SecondaryUV;
    float ExtraLayer_Use_SecondaryUV;
    float DirtMap_Use_SecondaryUV;
    float UV_Switch;
    float MaterialColor_AddBlend;
    float MaterialColor_Override;
    float MaterialColor_Intensity;
    float MaterialColor_AddBlend1;
    float MaterialColor_Override1;
    float MaterialColor_Intensity1;
    float MaterialColor_AddBlend2;
    float MaterialColor_Override2;
    float MaterialColor_Intensity2;
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
    float Roughness2;
    float MaterialTranslucency2;
    float PhysicalMaterialIndex2;
    float UV_Tiling2;
    float Use_AdvancedUVSetting2;
    float NormalBlend_Balance2;
    float WearMap_Tiling2;
    float Use_AdvancedUVSetting_WearMap2;
    float WearMap_Inverse2;
    float WearMap_Blend2;
    float WearMap_Normal_BlendMode2;
    float WearMap_NormalIntensity2;
    float LayerMask_BrightnessMasking2;
    float LayerMask_Brightness2;
    float LayerMask_Contrast2;
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
	
	// Normalize input vectors
	float3 normalWS = normalize(IN.NormalWS);
	float3 tangentWS = normalize(IN.TangentWS);
	float3 binormalWS = normalize(cross(tangentWS, normalWS));
	
	// UV selection based on UV_Switch parameter
	float2 uv0 = IN.UV0;
	float2 uv1 = IN.UV1;
	float2 uvPrimary = lerp(uv0, uv1, UV_Switch);
	float2 uvSecondary = lerp(uv1, uv0, UV_Switch);
	
	// UV transformations for different maps
	float2 uvLayerMask = lerp(uvPrimary, uvSecondary, LayerMask_Use_SecondaryUV * Occlusion_Use_SecondaryUV);
	float2 uvBase = lerp(uvPrimary, uvSecondary, BaseLayer_Use_SecondaryUV);
	float2 uvExtra = lerp(uvPrimary, uvSecondary, ExtraLayer_Use_SecondaryUV);
	float2 uvMaterialColor = lerp(uvPrimary, uvSecondary, MaterialColor_Use_SecondaryUV);
	
	// Sample layer mask and occlusion
	float4 layerMaskSample = LayerMaskOcclusionMap.Sample(BilinearWrap, uvLayerMask);
	
	// Calculate occlusion with proper UV blending
	float occlusion;
	if (LayerMask_Use_SecondaryUV * Occlusion_Use_SecondaryUV > 0.0)
	{
		occlusion = layerMaskSample.a;
	}
	else
	{
		float4 layerMaskSample2 = LayerMaskOcclusionMap.Sample(BilinearWrap, uvSecondary);
		occlusion = lerp(layerMaskSample2.a, layerMaskSample.a, Occlusion_Use_SecondaryUV);
	}
	
	// Base layer UV calculations
	float2 baseUV = uvBase * lerp(UV_Tiling, UV_Tiling_Offset.xy, Use_AdvancedUVSetting) + lerp(0, UV_Tiling_Offset.zw, Use_AdvancedUVSetting);
	
	// Layer 1 UV calculations
	float wearTiling1 = lerp(UV_Tiling1, WearMap_Tiling1, Use_AdvancedUVSetting_WearMap1);
	float2 wearUV1 = uvExtra * lerp(WearMap_Tiling1, WearMap_Tiling_Offset1.xy, Use_AdvancedUVSetting_WearMap1) + lerp(0, WearMap_Tiling_Offset1.zw, Use_AdvancedUVSetting_WearMap1);
	
	// Sample wear map 1
	float4 wearMap1 = WearMap1.Sample(BilinearWrap, wearUV1);
	float wearIntensity1 = lerp(wearMap1.r, 1.0 - wearMap1.r, WearMap_Inverse1);
	
	// Calculate layer 1 mask
	float brightness1 = LayerMask_Brightness1 * 2.0 - 1.0;
	float layerMask1Raw = layerMaskSample.r + brightness1;
	float layerMask1Bright = saturate(layerMaskSample.r * 2.0) * brightness1;
	float layerMask1 = layerMask1Raw + (layerMask1Bright - brightness1) * LayerMask_BrightnessMasking1;
	float layerMask1WithWear = layerMask1 + wearIntensity1;
	
	float layer1Threshold;
	if (layerMask1WithWear > 1.0)
		layer1Threshold = 1.0;
	else if (layerMask1 > 0.0)
		layer1Threshold = layerMask1 / (1.0 - wearIntensity1);
	else
		layer1Threshold = 0.0;
	
	float contrast1Offset = LayerMask_Contrast1 * 0.5;
	float layer1Blend = saturate((layerMask1 - contrast1Offset + (layer1Threshold - layerMask1) * WearMap_Blend1) / max(0.0001, abs((1.0 - contrast1Offset) - contrast1Offset)));
	
	// Apply wear normal intensity
	float wearNormalMult1 = WearMap_NormalIntensity1 * 4.0 - 2.0;
	float2 wearNormal1 = float2(
		saturate((wearMap1.g * 1.25 - 0.625) * wearNormalMult1),
		saturate((wearMap1.b * 1.25 - 0.625) * wearNormalMult1)
	);
	
	// Sample base layer maps
	float4 baseColor0 = BaseDielectricMapBase.Sample(BilinearWrap, baseUV);
	baseColor0.rgb *= BaseColor.rgb;
	float4 baseNormal0 = NormalRoughnessCavityMapBase.Sample(BilinearWrap, baseUV);
	
	// Layer 1 UV and sampling
	float2 layer1UV = uvExtra * lerp(UV_Tiling1, UV_Tiling_Offset1.xy, Use_AdvancedUVSetting1) + lerp(0, UV_Tiling_Offset1.zw, Use_AdvancedUVSetting1);
	float4 layer1Color = BaseDielectricMap.Sample(BilinearWrap, layer1UV);
	layer1Color.rgb *= BaseColor1.rgb;
	float4 layer1Normal = NormalRoughnessCavityMap1.Sample(BilinearWrap, layer1UV);
	
	// Blend layer 1 colors
	float4 blendedColor1 = lerp(baseColor0, layer1Color, layer1Blend);
	
	// Decode normals from layer 1
	float2 normal1RG = layer1Normal.ga * 2.0 - 1.0;
	float2 normal1Sqr = normal1RG * normal1RG * sign(normal1RG);
	float normal1X = (normal1Sqr.x + normal1Sqr.y) * 0.5;
	float normal1Y = (normal1Sqr.x - normal1Sqr.y) * 0.5;
	float normal1Z = sqrt(max(0, 1.0 - abs(normal1X) - abs(normal1Y)));
	float3 layer1Norm = normalize(float3(normal1X, normal1Y, normal1Z));
	
	// Decode normals from base
	float2 normal0RG = baseNormal0.ga * 2.0 - 1.0;
	float2 normal0Sqr = normal0RG * normal0RG * sign(normal0RG);
	float normal0X = (normal0Sqr.x + normal0Sqr.y) * 0.5;
	float normal0Y = (normal0Sqr.x - normal0Sqr.y) * 0.5;
	float normal0Z = sqrt(max(0, 1.0 - abs(normal0X) - abs(normal0Y)));
	float3 layer0Norm = normalize(float3(normal0X, normal0Y, normal0Z));
	
	// Blend material properties for layer 1
	float alphaBlend1 = 1.0 - baseColor0.a;
	float matIntensity1 = lerp(MaterialColor_Intensity, MaterialColor_Intensity1, layer1Blend);
	float matAddBlend1 = lerp(MaterialColor_AddBlend, MaterialColor_AddBlend1, layer1Blend);
	float matOverride1 = lerp(MaterialColor_Override, MaterialColor_Override1, layer1Blend);
	
	float roughness1 = lerp(baseNormal0.r * Roughness, layer1Normal.r * Roughness1, layer1Blend);
	
	// Sample material color map
	float2 matColorUV = uvMaterialColor * BaseAlphaMap_Tiling_Offset.xy + BaseAlphaMap_Tiling_Offset.zw;
	float4 matColor = BaseAlphaMap.Sample(BilinearWrap, matColorUV);
	
	// Layer 2 setup
	float2 wearUV2 = uvExtra * lerp(WearMap_Tiling2, WearMap_Tiling_Offset2.xy, Use_AdvancedUVSetting_WearMap2) + lerp(0, WearMap_Tiling_Offset2.zw, Use_AdvancedUVSetting_WearMap2);
	float4 wearMap2 = WearMap2.Sample(BilinearWrap, wearUV2);
	float wearIntensity2 = lerp(wearMap2.r, 1.0 - wearMap2.r, WearMap_Inverse2);
	
	// Calculate layer 2 mask
	float brightness2 = LayerMask_Brightness2 * 2.0 - 1.0;
	float layerMask2Raw = layerMaskSample.g + brightness2;
	float layerMask2Bright = saturate(layerMaskSample.g * 2.0) * brightness2;
	float layerMask2 = layerMask2Raw + (layerMask2Bright - brightness2) * LayerMask_BrightnessMasking2;
	float layerMask2WithWear = layerMask2 + wearIntensity2;
	
	float layer2Threshold;
	if (layerMask2WithWear > 1.0)
		layer2Threshold = 1.0;
	else if (layerMask2 > 0.0)
		layer2Threshold = layerMask2 / (1.0 - wearIntensity2);
	else
		layer2Threshold = 0.0;
	
	float contrast2Offset = LayerMask_Contrast2 * 0.5;
	float layer2Blend = saturate((layerMask2 - contrast2Offset + (layer2Threshold - layerMask2) * WearMap_Blend2) / max(0.0001, abs((1.0 - contrast2Offset) - contrast2Offset)));
	
	// Apply wear normal intensity for layer 2
	float wearNormalMult2 = WearMap_NormalIntensity2 * 4.0 - 2.0;
	float2 wearNormal2 = float2(
		saturate((wearMap2.g * 1.25 - 0.625) * wearNormalMult2),
		saturate((wearMap2.b * 1.25 - 0.625) * wearNormalMult2)
	);
	
	// Layer 2 UV and sampling
	float2 layer2UV = uvExtra * lerp(UV_Tiling2, UV_Tiling_Offset2.xy, Use_AdvancedUVSetting2) + lerp(0, UV_Tiling_Offset2.zw, Use_AdvancedUVSetting2);
	float4 layer2Color = BaseDielectricMap2.Sample(BilinearWrap, layer2UV);
	layer2Color.rgb *= BaseColor2.rgb;
	float4 layer2Normal = NormalRoughnessCavityMap2.Sample(BilinearWrap, layer2UV);
	
	// Decode normals from layer 2
	float2 normal2RG = layer2Normal.ga * 2.0 - 1.0;
	float2 normal2Sqr = normal2RG * normal2RG * sign(normal2RG);
	float normal2X = (normal2Sqr.x + normal2Sqr.y) * 0.5;
	float normal2Y = (normal2Sqr.x - normal2Sqr.y) * 0.5;
	float normal2Z = sqrt(max(0, 1.0 - abs(normal2X) - abs(normal2Y)));
	float3 layer2Norm = normalize(float3(normal2X, normal2Y, normal2Z));
	
	// Blend layer 2
	float3 blendedColor2 = lerp(blendedColor1.rgb, layer2Color.rgb, layer2Blend);
	float alphaBlend2 = lerp(alphaBlend1, 1.0 - baseColor0.a - layer2Color.a, layer2Blend);
	
	// Blend material properties for layer 2
	float matIntensity2 = lerp(matIntensity1, MaterialColor_Intensity2, layer2Blend);
	float matAddBlend2 = lerp(matAddBlend1, MaterialColor_AddBlend2, layer2Blend);
	float matOverride2 = lerp(matOverride1, MaterialColor_Override2, layer2Blend);
	
	float roughness2 = lerp(roughness1, layer2Normal.r * Roughness2, layer2Blend);
	
	// Blend material color (complex blend between additive and multiplicative)
	float3 colorSum = matColor.rgb + blendedColor2;
	float3 colorMult = matColor.rgb * blendedColor2;
	float3 colorSumSat = saturate(colorSum);
	float3 colorDiff = (colorSumSat - colorMult) * matAddBlend2;
	float3 colorBlended = colorDiff + colorMult;
	
	// Apply override
	float3 finalColorBeforeDirt = lerp(colorBlended, matColor.rgb, matOverride2);
	
	// Apply intensity
	float3 finalColor = matColor.a * matIntensity2 * (finalColorBeforeDirt - blendedColor2) + blendedColor2;
	
	// Dirt system
	float dirtMask = 0.0;
	if (Dirt_Enable > 0.0)
	{
		float dirtTiling = lerp(DirtWearMap_Tiling, DirtWearMap_Tiling_Offset.xy, Use_AdvancedUVSetting_DirtWearMap);
		float2 dirtUV = lerp(uvPrimary, uvSecondary, DirtMap_Use_SecondaryUV) * lerp(DirtWearMap_Tiling, DirtWearMap_Tiling_Offset.xy, Use_AdvancedUVSetting_DirtWearMap) + lerp(0, DirtWearMap_Tiling_Offset.zw, Use_AdvancedUVSetting_DirtWearMap);
		float dirtWear = DirtWearMap.Sample(BilinearWrap, dirtUV).r;
		dirtWear = lerp(dirtWear, 1.0 - dirtWear, DirtWearMap_Inverse);
		
		// Dirt color gradient based on DirtColorControl
		float dirtCtrl = DirtColorControl + -0.5;
		float dirtGradient = exp2(log(max(dirtWear, 1e-6)) * (exp2(abs(dirtCtrl) * 20.0) * (DirtColorControl < 0.5 ? 0.0 : 1.0) + sqrt(max(exp2(abs(dirtCtrl) * 20.0) - sqrt(max(exp2(abs(dirtCtrl) * 20.0), 1e-6)), 1e-6) * 0.5)));
		float3 dirtColorBlend = lerp(DirtColor2.rgb, DirtColor3.rgb, saturate(dirtGradient * 2.0 - 1.0));
		dirtColorBlend = lerp(lerp(DirtColor1.rgb, DirtColor2.rgb, dirtGradient * 2.0), dirtColorBlend, dirtGradient >= 0.5);
		
		float brightness = DirtMask_Brightness * 2.0 - 1.0;
		float dirtMaskCalc = saturate(saturate(brightness + layerMaskSample.b * Dirt_Enable) * 2.0);
		float dirtAlpha = saturate(dirtMaskCalc * 2.0 - 1.0) * (1.0 - dirtWear) + saturate(dirtMaskCalc) * dirtWear;
		
		float contrastOffset = DirtMask_Contrast * 0.5;
		dirtMask = saturate((dirtAlpha - contrastOffset + dirtMaskCalc) / max(0.0001, abs((1.0 - contrastOffset) - contrastOffset)));
		
		// Blend dirt color (complex blend between additive and multiplicative)
		float3 dirtSum = dirtColorBlend + finalColor;
		float3 dirtMult = dirtColorBlend * finalColor;
		float3 dirtBlendAddMult = (dirtSum - dirtMult) * DirtColor_AddBlend + dirtMult;
		
		// Apply override
		float3 dirtOverride = (dirtColorBlend - dirtBlendAddMult) * DirtColor_Override;
		float3 dirtFinal = dirtBlendAddMult - finalColor + dirtOverride;
		
		// Apply dirt mask
		finalColor = dirtMask * dirtFinal + finalColor;
		roughness2 = dirtMask * (Dirt_Roughness * roughness2 - roughness2) + roughness2;
	}
	
	float metallic = saturate(alphaBlend2 * 1.087 - 0.087);
	metallic = dirtMask * (metallic - metallic + Dirt_Metallic * metallic) + metallic;
	
	// Waterline effect
	float3 waterlineColor = 0;
	if (Enable_WaterLine > 0.0)
	{
		float waterlineHeight = (IN.PositionWS.y - WaterLine_WorldHeight - WaterLine_BlurWidth) / max(0.0001, abs(-WaterLine_BlurWidth));
		waterlineHeight = saturate(waterlineHeight);
		
		float waterlineMask = frac(waterlineHeight);
		if (waterlineMask > 0.0)
		{
			float normalDotUp = abs(dot(normalWS, float3(0, 1, 0)));
			float topSideLerp = saturate((normalDotUp + -0.5) * -15.8217 + 1.0) / (1.0 + exp2((normalDotUp + -0.5) * -15.8217));
			topSideLerp = (topSideLerp - normalDotUp) * 0.5 + normalDotUp;
			
			float3 wearSampleTop = ExtraWearMap.Sample(BilinearWrap, float2(IN.PositionWS.x, IN.PositionWS.y) * WaterLine_WearTiling_Top).rgb;
			float3 wearSampleSide = ExtraWearMap.Sample(BilinearWrap, float2(IN.PositionWS.z, IN.PositionWS.y) * WaterLine_WearTiling_Side).rgb;
			float wearBlended = lerp(wearSampleTop.b, lerp(wearSampleTop.b, wearSampleSide.g, topSideLerp), abs(dot(normalWS, float3(1, 0, 0))));
			
			float waterlineBlend = saturate((1.0 - wearBlended) * saturate(waterlineHeight * 2.0 - 1.0) + saturate(waterlineHeight * 2.0) * wearBlended);
			waterlineBlend = saturate((waterlineBlend - waterlineHeight + 0.5) * -1.4427 * (exp2(log(max(WaterLine_Contrast, 1e-6)) * 10.0) * 990.0 + 10.0) + 1.0) / (1.0 + exp2((waterlineBlend - waterlineHeight + 0.5) * -1.4427 * (exp2(log(max(WaterLine_Contrast, 1e-6)) * 10.0) * 990.0 + 10.0)));
			waterlineBlend = saturate((waterlineBlend - waterlineHeight) * WaterLine_Contrast + waterlineHeight);
			
			waterlineColor = float3(waterlineBlend, waterlineBlend, waterlineBlend);
		}
		
		float3 edgeColor = lerp(WaterLine_Color.rgb, WaterLine_EdgeColor.rgb, WaterLine_EdgeColorOverride);
		float3 waterlineColorFinal = waterlineColor * waterlineColor * (edgeColor - WaterLine_Color.rgb) + finalColor;
		finalColor = waterlineColor * waterlineColorFinal + finalColor;
		
		float waterlineRoughness = lerp(WaterLine_Roughness_Occluded, WaterLine_Roughness, occlusion);
		roughness2 = WaterLine_RoughnessBlend * waterlineColor.r * (waterlineRoughness - roughness2) + roughness2;
	}
	
	// Normal blending with wear
	float3 normalBlend1 = normalize(layer1Norm + float3(wearNormal1.xy, 0));
	normalBlend1 = lerp(normalBlend1, layer0Norm, WearMap_Normal_BlendMode1);
	normalBlend1 = lerp(normalBlend1, layer0Norm, layer1Blend);
	normalBlend1 = normalize(lerp(normalBlend1, layer0Norm, NormalBlend_Balance1) + layer0Norm);
	
	float3 normalBlend2 = normalize(layer2Norm + float3(wearNormal2.xy, 0));
	normalBlend2 = lerp(normalBlend2, layer1Norm, WearMap_Normal_BlendMode2);
	normalBlend2 = lerp(normalBlend2, layer1Norm, layer2Blend);
	normalBlend2 = lerp(normalBlend2, normalBlend1, NormalBlend_Balance2);
	
	// Decode extra normal map
	float4 extraNormal = NormalRoughnessCavityMap.Sample(BilinearWrap, uv1);
	float2 extraNormalRG = extraNormal.ga * 2.0 - 1.0;
	float2 extraNormalSqr = extraNormalRG * extraNormalRG * sign(extraNormalRG);
	float extraNormalX = (extraNormalSqr.x + extraNormalSqr.y) * 0.5;
	float extraNormalY = (extraNormalSqr.x - extraNormalSqr.y) * 0.5;
	float extraNormalZ = sqrt(max(0, 1.0 - abs(extraNormalX) - abs(extraNormalY)));
	float3 extraNorm = normalize(float3(extraNormalX, extraNormalY, extraNormalZ));
	
	float3 finalNormal = normalize(normalBlend2 + extraNorm);
	
	// Transform normal to world space
	float3 finalNormalWS = normalize(finalNormal.x * tangentWS + finalNormal.y * binormalWS + finalNormal.z * normalWS);
	
	// Oil effect
	if (Enable_Oil > 0.0)
	{
		float oilWear = ExtraWearMap.Sample(BilinearWrap, uvBase * Oil_WearTiling).r;
		float oilBlend = (oilWear * 2.0 - 1.0) * Oil_WearBlend + Oil_Thickness;
		
		float3 viewDir = normalize(IN.PositionWS);
		float viewDotNormal = abs(dot(viewDir, finalNormalWS));
		float oilFresnel = viewDotNormal * oilBlend;
		
		float3 oilColorDark = finalColor * (1.0 - Oil_DarkColor);
		float3 oilIridescence = float3(
			(cos(oilFresnel * 24.85) * -0.5 + 0.5 - cos(oilFresnel * 24.85) * -0.5 * oilFresnel) * oilFresnel,
			(cos(oilFresnel * 30.45) * -0.5 + 0.5 - cos(oilFresnel * 30.45) * -0.5 * oilFresnel) * oilFresnel,
			(cos(oilFresnel * 35.0) * -0.5 + 0.5 - cos(oilFresnel * 35.0) * -0.5 * oilFresnel) * oilFresnel
		);
		
		finalColor = (oilColorDark * 2.0 - finalColor) * oilIridescence * oilIridescence * Oil_Intensity + finalColor;
		roughness2 = min(roughness2, Oil_Roughness);
	}
	
	// Waterline normal weakening
	if (Enable_WaterLine > 0.0)
	{
		float normalWeaken = lerp(WaterLine_NormalWeaken_Occluded, WaterLine_NormalWeaken, occlusion);
		float3 weakenedNormal = lerp(normalWS, finalNormalWS, normalWeaken);
		finalNormalWS = normalize(waterlineColor * (weakenedNormal - finalNormalWS) + finalNormalWS);
	}
	
	// Encode roughness and translucency
	float translucency = lerp(MaterialTranslucency, MaterialTranslucency1, layer1Blend);
	translucency = lerp(translucency, MaterialTranslucency2, layer2Blend);
	float cavityBlend = lerp(baseNormal0.g, layer1Normal.g, layer1Blend);
	cavityBlend = lerp(cavityBlend, layer2Normal.g, layer2Blend);
	
	float encodedRoughness;
	float encodedType;
	if (metallic > 0.0 || translucency <= 0.0333333)
	{
		encodedRoughness = max(metallic, min(0.08, cavityBlend * extraNormal.r * 0.04));
		encodedType = 0.666667;
	}
	else
	{
		encodedRoughness = round(roughness2 * 15.49 + 0.5) * 0.0627451 + round(saturate(cavityBlend * extraNormal.r * 0.5) * 15.49 + 0.5) * 0.00392157;
		encodedType = 0.0;
	}
	
	// Encode normal to octahedron
	float3 normalAbs = abs(finalNormalWS);
	float normalSum = normalAbs.x + normalAbs.y + normalAbs.z;
	float2 normalOct = finalNormalWS.xz / normalSum;
	
	if (finalNormalWS.y <= 0.0)
	{
		normalOct = (1.0 - abs(normalOct.yx)) * sign(normalOct);
	}
	
	float2 normalEncoded = normalOct * 0.5 + 0.5;
	
	// Calculate screen UVs (approximation since we don't have access to screen size)
	// In the original: screenUV.x = 1.0 - PositionCS.x * 2.0 * screenInverseSize.x + ...
	// We'll use a simplified version based on available data
	float2 screenUV = IN.UV0;
	
	OUT.Target0 = float4(finalColor, encodedRoughness);
	OUT.Target1 = float4(normalEncoded, roughness2, encodedType);
	OUT.Target2 = float4(screenUV, occlusion, 1.0);
	
	return OUT;
}
