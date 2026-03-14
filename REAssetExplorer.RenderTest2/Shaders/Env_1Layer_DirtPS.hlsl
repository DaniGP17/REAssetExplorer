Texture2D LayerMaskOcclusionMap          : register(t0);
Texture2D BaseDielectricMapBase          : register(t1);
Texture2D NormalRoughnessCavityMapBase   : register(t2);
Texture2D DirtWearMap                    : register(t3);
Texture2D ExtraWearMap                   : register(t4);

SamplerState BilinearWrap  : register(s0, space0);

cbuffer UserMaterial : register(b0)
{
    float4 BaseAlphaMap_Tiling_Offset;
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
    float LayerMask_Brightness;
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

	// Normalize input vectors
	float3 Normal = normalize(IN.NormalWS);
	float3 Tangent = normalize(IN.TangentWS);
	float3 Binormal = normalize(IN.BinormalWS);
	
	// Recalculate binormal with proper orientation
	float3 BiTangent = cross(Tangent, Normal);
	if (dot(BiTangent, Binormal) < 0.0)
	{
		BiTangent = -BiTangent;
	}
	
	// UV selection logic
	float2 UV_Primary = IN.UV0;
	float2 UV_Secondary = IN.UV1;
	
	// Mix UVs based on UV_Switch
	float2 UV_Mixed = lerp(UV_Primary, UV_Secondary, UV_Switch);
	float2 UV_Base = lerp(UV_Mixed, UV_Secondary, BaseLayer_Use_SecondaryUV);
	
	// Apply UV tiling and offset with advanced settings
	float2 TilingValue = Use_AdvancedUVSetting > 0.0 ? UV_Tiling_Offset.xy : float2(UV_Tiling, UV_Tiling);
	float2 OffsetValue = Use_AdvancedUVSetting > 0.0 ? UV_Tiling_Offset.zw : float2(0, 0);
	float2 UV_Final = UV_Base * TilingValue + OffsetValue;
	
	// Calculate gradients for texture sampling
	float2 UV_ddx = ddx(UV_Final);
	float2 UV_ddy = ddy(UV_Final);
	
	// Sample base maps
	float4 BaseDielectricSample = BaseDielectricMapBase.SampleGrad(BilinearWrap, UV_Final, UV_ddx, UV_ddy);
	float4 NormalRoughnessSample = NormalRoughnessCavityMapBase.SampleGrad(BilinearWrap, UV_Final, UV_ddx, UV_ddy);
	
	// Extract base color
	float3 BaseColorFinal = BaseDielectricSample.rgb * BaseColor.rgb;
	
	// Decode normal from normal map
	float2 NormalXY = NormalRoughnessSample.ag * 2.0 - 1.0;
	float NormalZ = sqrt(saturate(1.0 - dot(NormalXY, NormalXY)));
	float3 TangentNormal = float3(NormalXY.x, NormalXY.y, NormalZ);
	
	// Extract roughness and cavity
	float BaseRoughness = NormalRoughnessSample.r * Roughness;
	float BaseCavity = BaseDielectricSample.a;
	
	// Sample occlusion map with UV selection
	float2 UV_Occlusion = lerp(UV_Final, UV_Secondary, Occlusion_Use_SecondaryUV);
	float4 OcclusionSample = LayerMaskOcclusionMap.SampleGrad(BilinearWrap, UV_Occlusion, ddx(UV_Occlusion), ddy(UV_Occlusion));
	float Occlusion = OcclusionSample.b;
	float LayerMask = lerp(OcclusionSample.b, OcclusionSample.a, LayerMask_Use_SecondaryUV);
	
	// Calculate adjusted cavity/occlusion value
	float CavityAdjusted = (1.0 - BaseCavity) * 1.087 - 0.087;
	CavityAdjusted = saturate(CavityAdjusted);
	
	// ========== DIRT SYSTEM ==========
	float3 DirtColorResult = BaseColorFinal;
	float DirtRoughness = BaseRoughness;
	float DirtMetallic = CavityAdjusted;
	float DirtBlendFactor = 0.0;
	
	if (Dirt_Enable * DirtMask_Brightness > 0.0)
	{
		// Calculate dirt UVs
		float2 UV_Dirt = lerp(UV_Primary, UV_Secondary, DirtMap_Use_SecondaryUV);
		float2 DirtTiling = Use_AdvancedUVSetting_DirtWearMap > 0.0 ? DirtWearMap_Tiling_Offset.xy : float2(DirtWearMap_Tiling, DirtWearMap_Tiling);
		float2 DirtOffset = Use_AdvancedUVSetting_DirtWearMap > 0.0 ? DirtWearMap_Tiling_Offset.zw : float2(0, 0);
		float2 UV_DirtFinal = UV_Dirt * DirtTiling + DirtOffset;
		
		// Sample dirt wear map
		float DirtWear = DirtWearMap.Sample(BilinearWrap, UV_DirtFinal).r;
		
		// Invert dirt wear if needed
		DirtWear = lerp(DirtWear, 1.0 - DirtWear, DirtWearMap_Inverse);
		
		// Calculate dirt gradient based on DirtColorControl
		float DirtGradientPower = pow(2.0, abs(DirtColorControl - 0.5) * 20.0);
		float DirtGradientBias = DirtColorControl < 0.5 ? 0.0 : 1.0;
		float DirtGradientOffset = (DirtGradientPower - sqrt(DirtGradientPower)) * DirtGradientBias + sqrt(DirtGradientPower);
		float DirtGradient = pow(DirtWear, DirtGradientOffset);
		
		// Interpolate between dirt colors
		float3 DirtColor12 = lerp(DirtColor1.rgb, DirtColor2.rgb, saturate(DirtGradient * 2.0));
		float3 DirtColor23 = lerp(DirtColor2.rgb, DirtColor3.rgb, saturate(DirtGradient * 2.0 - 1.0));
		float3 DirtColorFinal = lerp(DirtColor12, DirtColor23, step(0.5, DirtGradient));
		
		// Calculate dirt mask
		float DirtMask = saturate((DirtMask_Brightness * 2.0 - 1.0) + Occlusion * Dirt_Enable);
		float DirtMaskProcessed = saturate(DirtMask * 2.0 - 1.0);
		float DirtMaskSoft = saturate(DirtMask * 2.0);
		DirtBlendFactor = (DirtMaskProcessed * (1.0 - DirtWear) + DirtMaskSoft * DirtWear);
		
		// Apply contrast to dirt mask
		float ContrastPivot = 0.5 + DirtMask_Contrast * 0.5;
		float ContrastRange = abs(1.0 - ContrastPivot - ContrastPivot);
		float ContrastFactor = max(0.0001, ContrastRange);
		DirtBlendFactor = saturate((DirtBlendFactor - ContrastPivot) / ContrastFactor);
		
		// Blend dirt color (additive or override)
		float3 DirtColorBlended = lerp(DirtColorFinal * BaseColorFinal, DirtColorFinal + BaseColorFinal, DirtColor_AddBlend);
		DirtColorResult = lerp(BaseColorFinal, lerp(DirtColorFinal, DirtColorBlended, DirtColor_Override), DirtBlendFactor);
		
		// Blend dirt roughness and metallic
		DirtRoughness = lerp(BaseRoughness, BaseRoughness * Dirt_Roughness, DirtBlendFactor);
		DirtMetallic = lerp(CavityAdjusted, CavityAdjusted * Dirt_Metallic, DirtBlendFactor);
	}
	
	// ========== WATERLINE SYSTEM ==========
	float3 WaterLineBlend = float3(0, 0, 0);
	
	if (Enable_WaterLine > 0.0)
	{
		// Calculate distance from waterline
		float WaterLineDist = IN.PositionWS.y - WaterLine_WorldHeight;
		float WaterLineDistBlur = WaterLineDist - WaterLine_BlurWidth;
		
		// Calculate gradient
		float WaterLineGradient = saturate(WaterLineDistBlur / (abs(WaterLine_BlurWidth) > 0.0001 ? max(0.0001, abs(WaterLine_BlurWidth)) * (WaterLine_BlurWidth > 0.0 ? -1.0 : 1.0) : 0.0001));
		
		// Sample wear map for waterline with world space UVs
		float3 WorldPos = IN.PositionWS;
		float WearSide = ExtraWearMap.Sample(BilinearWrap, float2(WorldPos.x * WaterLine_WearTiling_Side, WorldPos.y * WaterLine_WearTiling_Side)).g;
		float WearTop = ExtraWearMap.Sample(BilinearWrap, float2(WorldPos.x * WaterLine_WearTiling_Top, WorldPos.z * WaterLine_WearTiling_Top)).b;
		float WearVertical = ExtraWearMap.Sample(BilinearWrap, float2(WorldPos.z * WaterLine_WearTiling_Side, WorldPos.y * WaterLine_WearTiling_Side)).b;
		
		// Blend wear maps based on normal direction
		float NormalDotUp = abs(dot(Normal, float3(1, 0, 0)));
		float NormalBlendFactor = saturate((saturate(NormalDotUp + 0.5) - NormalDotUp) * 0.5 + NormalDotUp);
		float WearBlendH = lerp(WearSide, WearVertical, NormalBlendFactor);
		
		float NormalDotSide = abs(dot(Normal, float3(0, 1, 0)));
		float WearFinal = lerp(WearBlendH, WearTop, NormalDotSide);
		
		// Apply wear to gradient
		float WaterLineGradientWear = lerp(WaterLineGradient, WearFinal, WaterLine_WearBlend);
		
		// Apply contrast
		float ContrastExp = pow(2.0, WaterLine_Contrast * 10.0) * 990.0 + 10.0;
		float3 WaterLineValue = 1.0 / (1.0 + exp2((WaterLineGradientWear - 0.5) * -1.4427 * ContrastExp));
		WaterLineValue = saturate(WaterLineValue);
		WaterLineValue = lerp(WaterLineGradientWear, WaterLineValue, WaterLine_Contrast);
		
		WaterLineBlend = WaterLineValue;
	}
	
	// Apply waterline to color
	float3 FinalColor = DirtColorResult;
	float FinalRoughness = DirtRoughness;
	
	if (Enable_WaterLine > 0.0)
	{
		// Calculate waterline colors
		float3 WaterLineEdgeColor = lerp(DirtColorResult * WaterLine_EdgeColor.rgb, WaterLine_EdgeColor.rgb, WaterLine_EdgeColorOverride);
		float3 WaterLineMainColor = DirtColorResult * WaterLine_Color.rgb;
		
		// Blend edge and main color
		float3 WaterLineColorBlend = lerp(WaterLineEdgeColor, WaterLineMainColor, WaterLineBlend * WaterLineBlend);
		FinalColor = lerp(DirtColorResult, WaterLineColorBlend, WaterLineBlend);
		
		// Blend roughness
		float WaterLineRoughnessValue = lerp(WaterLine_Roughness_Occluded, WaterLine_Roughness, LayerMask);
		FinalRoughness = lerp(DirtRoughness, lerp(DirtRoughness, WaterLineRoughnessValue, WaterLine_RoughnessBlend * WaterLineBlend.x), WaterLineBlend.x);
	}
	
	// ========== OIL SYSTEM ==========
	if (Enable_Oil > 0.0)
	{
		// Sample oil wear map
		float2 UV_Oil = UV_Base * Oil_WearTiling;
		float OilWear = ExtraWearMap.Sample(BilinearWrap, UV_Oil).r;
		OilWear = OilWear * 2.0 - 1.0;
		
		// Calculate oil thickness based on view angle
		// Using simplified view direction from world normal and position
		float3 WorldNormal = normalize(Tangent * TangentNormal.x + BiTangent * TangentNormal.y + Normal * TangentNormal.z);
		float3 ViewDir = normalize(-IN.PositionWS);
		float ViewDotNormal = abs(dot(ViewDir, WorldNormal));
		
		float OilThickness = (OilWear * Oil_WearBlend + Oil_Thickness) * ViewDotNormal;
		
		// Calculate oil interference colors
		float3 OilLuminance = float3(
			cos(OilThickness * 35.0) * -0.5 + 0.5,
			cos(OilThickness * 24.85) * -0.5 + 0.5,
			cos(OilThickness * 30.45) * -0.5 + 0.5
		);
		OilLuminance = OilLuminance - (OilLuminance * OilThickness);
		
		// Darken base color and apply oil
		float3 DarkenedColor = FinalColor * (1.0 - Oil_DarkColor);
		float3 OilColor = (DarkenedColor * 2.0) * (OilLuminance * OilLuminance) - DarkenedColor;
		FinalColor = lerp(DarkenedColor, OilColor * Oil_Intensity + DarkenedColor, Oil_Intensity);
		
		// Blend oil roughness
		FinalRoughness = min(FinalRoughness, Oil_Roughness);
	}
	
	// Apply normal weakening from waterline
	float NormalWeaken = 1.0;
	if (Enable_WaterLine > 0.0)
	{
		float WeakenAmount = lerp(WaterLine_NormalWeaken_Occluded, WaterLine_NormalWeaken, LayerMask);
		NormalWeaken = lerp(1.0, WeakenAmount, WaterLineBlend.x);
		TangentNormal = lerp(float3(0, 0, 1), TangentNormal, NormalWeaken);
	}
	
	// Transform normal to world space
	float3 WorldNormal = normalize(Tangent * TangentNormal.x + BiTangent * TangentNormal.y + Normal * TangentNormal.z);
	
	// Encode normal to octahedron format (matching Target.txt logic)
	float3 AbsNormal = abs(WorldNormal);
	float NormalSum = AbsNormal.x + AbsNormal.y + AbsNormal.z;
	float2 OctNormal = float2(WorldNormal.x, WorldNormal.z) / NormalSum;
	
	if (WorldNormal.y <= 0.0)
	{
		float AbsOctX = abs(OctNormal.x);
		float AbsOctY = abs(OctNormal.y);
		float SignX = OctNormal.x >= 0.0 ? 1.0 : -1.0;
		float SignY = OctNormal.y >= 0.0 ? 1.0 : -1.0;
		OctNormal.x = (1.0 - AbsOctY) * SignX;
		OctNormal.y = (1.0 - AbsOctX) * SignY;
	}
	
	float2 EncodedNormal = OctNormal * 0.5 + 0.5;
	
	// Encode material properties
	float EncodedRoughness;
	float EncodedAlpha;
	if (DirtMetallic > 0.0 || MaterialTranslucency <= 0.033333)
	{
		// Metallic or simple material (label13)
		float MetallicEncoded = max(0.08, max(NormalRoughnessSample.b * 0.04, DirtMetallic));
		EncodedRoughness = MetallicEncoded;
		EncodedAlpha = 0.666667;
	}
	else
	{
		// Translucent material (label14)
		float TranslucencyRounded = round(MaterialTranslucency * 15.49 + 0.5) * 0.0627451;
		float CavityRounded = round(saturate(NormalRoughnessSample.b * 0.5) * 15.49 + 0.5) * 0.00392157;
		EncodedRoughness = CavityRounded + TranslucencyRounded;
		EncodedAlpha = 0.0;
	}
	
	float MaterialType = 0.333333 + EncodedAlpha;
	
	// Calculate screen UVs from clip space position
	float2 ScreenUV = IN.PositionCS.xy / IN.PositionCS.w;
	ScreenUV.y = 1.0 - ScreenUV.y;
	
	// Output to render targets
	OUT.Target0 = float4(FinalColor, EncodedRoughness);
	OUT.Target1 = float4(EncodedNormal, FinalRoughness, MaterialType);
	OUT.Target2 = float4(ScreenUV, LayerMask, 1.0);

	return OUT;
}