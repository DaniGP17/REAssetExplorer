Texture2D NormalRoughnessCavityMap         : register(t0);
Texture2D BaseAlphaMap                     : register(t1);
Texture2D LayerMaskOcclusionMap            : register(t2);
Texture2D AlphaTranslucentOcclusionSSSMap  : register(t3);
Texture2D BaseDielectricMapBase            : register(t4);
Texture2D NormalRoughnessCavityMapBase     : register(t5);
Texture2D DirtWearMap                      : register(t6);

SamplerState BilinearWrap  : register(s0, space0);

cbuffer UserMaterial : register(b0)
{
	float4 BaseAlphaMap_Tiling_Offset;
	float4 BaseColor;
	float4 UV_Tiling_Offset;
	float4 DirtColor1;
	float4 DirtColor2;
	float4 DirtColor3;
	float4 DirtWearMap_Tiling_Offset;

	float Occlusion_Use_SecondaryUV;
	float MaterialColor_Use_SecondaryUV;
	float LayerMask_Use_SecondaryUV;
	float BaseLayer_Use_SecondaryUV;
	float DirtMap_Use_SecondaryUV;
	float AlphaTranslucent_Use_SecondaryUV;

	float MaterialColor_AddBlend;
	float MaterialColor_Override;
	float MaterialColor_Intensity;

	float AlphaThreshold;
	float AlphaContrast;

	float Roughness;
	float PhysicalMaterialIndex;

	float UV_Tiling;
	float Use_AdvancedUVSetting;

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
	
	// Compute UV coordinates based on secondary UV flags
	float2 BaseLayerUV = lerp(IN.UV0, IN.UV1, BaseLayer_Use_SecondaryUV);
	float2 MaterialColorUV = lerp(IN.UV0, IN.UV1, MaterialColor_Use_SecondaryUV);
	
	// Apply UV tiling and offset with advanced settings
	float2 TilingValue = Use_AdvancedUVSetting > 0.0 ? UV_Tiling_Offset.xy : float2(UV_Tiling, UV_Tiling);
	float2 OffsetValue = Use_AdvancedUVSetting > 0.0 ? UV_Tiling_Offset.zw : float2(0, 0);
	float2 BaseUV = BaseLayerUV * TilingValue + OffsetValue;
	
	// Sample base textures with automatic sampler
	float4 BaseDielectric = BaseDielectricMapBase.Sample(BilinearWrap, BaseUV);
	
	// Sample layer mask with correct UV selection
	float2 LayerMaskUV = lerp(IN.UV0, IN.UV1, LayerMask_Use_SecondaryUV);
	float LayerMaskValue = LayerMaskOcclusionMap.Sample(BilinearWrap, LayerMaskUV).b;
	
	// Sample base alpha map
	float2 BaseAlphaUV = MaterialColorUV * BaseAlphaMap_Tiling_Offset.xy + BaseAlphaMap_Tiling_Offset.zw;
	float4 BaseAlpha = BaseAlphaMap.Sample(BilinearWrap, BaseAlphaUV);
	
	// Sample normal map
	float4 NormalRoughnessBase = NormalRoughnessCavityMapBase.Sample(BilinearWrap, BaseUV);
	
	// Decode normal from texture (RG channels store XY, reconstruct Z)
	float2 NormalXY = NormalRoughnessBase.ag * 2.0 - 1.0;
	float NormalX_Signed = NormalXY.x * NormalXY.x * sign(NormalXY.x);
	float NormalY_Signed = NormalXY.y * NormalXY.y * sign(NormalXY.y);
	float NormalZ_Component = (NormalX_Signed + NormalY_Signed) * 0.5;
	float NormalZ_Tangent = (NormalX_Signed - NormalY_Signed) * 0.5;
	float NormalZ = sqrt(saturate(1.0 - abs(NormalZ_Component) - abs(NormalZ_Tangent)));
	float3 TangentNormal = normalize(float3(NormalZ_Component, NormalZ_Tangent, NormalZ));
	
	// Apply base color
	float3 BaseColorRGB = BaseDielectric.rgb * BaseColor.rgb;
	float Roughness_Value = NormalRoughnessBase.r * Roughness;
	
	// Material color blending
	float3 ColorSum = BaseAlpha.rgb + BaseColorRGB;
	float3 ColorProduct = BaseAlpha.rgb * BaseColorRGB;
	float3 BlendedColor = saturate(ColorSum) - ColorProduct;
	float3 AdditiveBlended = lerp(ColorProduct, BlendedColor, MaterialColor_AddBlend);
	float3 MaterialColor = lerp(BaseColorRGB, lerp(AdditiveBlended, BaseAlpha.rgb, MaterialColor_Override), BaseAlpha.a * MaterialColor_Intensity);
	
	// Dirt/Wear processing
	float3 FinalColor = MaterialColor;
	float FinalRoughness = Roughness_Value;
	
	if (DirtMask_Brightness * Dirt_Enable > 0.0)
	{
		// Sample dirt wear map
		float2 DirtUV_Tiling = Use_AdvancedUVSetting_DirtWearMap > 0.0 ? DirtWearMap_Tiling_Offset.xy : float2(DirtWearMap_Tiling, DirtWearMap_Tiling);
		float2 DirtUV_Offset = Use_AdvancedUVSetting_DirtWearMap > 0.0 ? DirtWearMap_Tiling_Offset.zw : float2(0, 0);
		float2 DirtMapUV_Base = lerp(IN.UV0, IN.UV1, DirtMap_Use_SecondaryUV);
		float2 DirtUV = DirtMapUV_Base * DirtUV_Tiling + DirtUV_Offset;
		
		float DirtMask = DirtWearMap.Sample(BilinearWrap, DirtUV).r;
		
		// Apply inverse if needed
		DirtMask = lerp(DirtMask, 1.0 - DirtMask, DirtWearMap_Inverse);
		
		// Apply dirt color control (gradient mapping between 3 colors)
		float ColorControl = DirtColorControl - 0.5;
		float ControlAbs = abs(ColorControl);
		float ControlExp = exp2(ControlAbs * 20.0);
		float ControlInv = 1.0 / max(ControlExp, 1e-6);
		float Pivot = ColorControl < 0.5 ? 0.0 : 1.0;
		float BlendFactor = sqrt(max(ControlInv, 1e-6));
		float GradientWeight = (ControlExp - BlendFactor) * Pivot + BlendFactor;
		
		float DirtValue = pow(max(DirtMask, 1e-6), GradientWeight);
		float DirtGradient = DirtValue * 2.0 - 1.0;
		
		// Blend between 3 dirt colors
		float3 Color12 = lerp(DirtColor1.rgb, DirtColor2.rgb, DirtGradient);
		float3 Color23 = lerp(DirtColor2.rgb, DirtColor3.rgb, DirtGradient);
		float3 DirtColor = lerp(Color12, Color23, step(0.5, DirtValue));
		
		// Apply dirt mask brightness and contrast
		float OcclusionAdjusted = LayerMaskValue * Dirt_Enable;
		float BrightnessAdjusted = DirtMask_Brightness * 2.0 - 1.0 + OcclusionAdjusted;
		float MaskValue = saturate(BrightnessAdjusted);
		float ContrastAdjusted = MaskValue * 2.0 - 1.0;
		float ContrastClamped = saturate(ContrastAdjusted);
		float InverseMask = 1.0 - DirtMask;
		float MaskClamped = saturate(MaskValue * 2.0);
		float DirtStrength = ContrastClamped * InverseMask + MaskClamped * DirtMask;
		
		// Apply contrast
		float ContrastCenter = 1.0 - DirtMask_Contrast * 0.5;
		float ContrastRange = ContrastCenter - DirtMask_Contrast * 0.5;
		float ContrastNormalized = (DirtStrength - DirtMask_Contrast * 0.5) / max(abs(ContrastRange), 0.0001);
		float DirtFinal = saturate(ContrastNormalized);
		
		// Blend dirt color with base
		float3 DirtColorSum = DirtColor + MaterialColor;
		float3 DirtColorProduct = DirtColor * MaterialColor;
		float3 DirtBlended = saturate(DirtColorSum) - DirtColorProduct;
		float3 DirtAdditive = lerp(DirtColorProduct, DirtBlended, DirtColor_AddBlend);
		float3 DirtOverride = lerp(DirtAdditive, DirtColor, DirtColor_Override);
		
		FinalColor = lerp(MaterialColor, DirtOverride, DirtFinal);
		
		// Apply dirt roughness
		float DirtRoughnessValue = Roughness_Value * Dirt_Roughness;
		FinalRoughness = lerp(Roughness_Value, DirtRoughnessValue, DirtFinal);
	}
	
	// Sample additional normal for detail
	float4 NormalDetail = NormalRoughnessCavityMap.Sample(BilinearWrap, IN.UV0);
	float2 DetailNormalXY = NormalDetail.ag * 2.0 - 1.0;
	float DetailX_Signed = DetailNormalXY.x * DetailNormalXY.x * sign(DetailNormalXY.x);
	float DetailY_Signed = DetailNormalXY.y * DetailNormalXY.y * sign(DetailNormalXY.y);
	float DetailZ_Component = (DetailX_Signed + DetailY_Signed) * 0.5;
	float DetailZ_Tangent = (DetailX_Signed - DetailY_Signed) * 0.5;
	float DetailZ = sqrt(saturate(1.0 - abs(DetailZ_Component) - abs(DetailZ_Tangent)));
	float3 DetailNormal = normalize(float3(DetailZ_Component, DetailZ_Tangent, DetailZ));
	
	// Blend normals
	float3 BlendedNormal = normalize(DetailNormal + TangentNormal);
	
	// Transform normal to world space
	float3 WorldNormal = normalize(
		BlendedNormal.x * Tangent +
		BlendedNormal.y * Binormal +
		BlendedNormal.z * Normal
	);
	
	// Sample alpha/translucent map
	float2 AlphaUV = lerp(IN.UV0, IN.UV1, AlphaTranslucent_Use_SecondaryUV);
	float AlphaValue = AlphaTranslucentOcclusionSSSMap.Sample(BilinearWrap, AlphaUV).r;
	
	// Apply alpha threshold and contrast
	float AlphaAdjusted = (1.0 - AlphaContrast) * 0.5 - AlphaThreshold + AlphaValue;
	float AlphaFinal = saturate(AlphaAdjusted / max(abs(1.0 - AlphaContrast), 0.0001));
	
	// Encode normal to octahedral format for output
	float NormalAbsSum = abs(WorldNormal.x) + abs(WorldNormal.z) + abs(WorldNormal.y);
	float NormalScale = 1.0 / NormalAbsSum;
	float OctX = NormalScale * WorldNormal.x;
	float OctZ = NormalScale * WorldNormal.z;
	
	float2 OctNormal;
	if (WorldNormal.y <= 0.0)
	{
		float SignX = OctX >= 0.0 ? 1.0 : -1.0;
		float SignZ = OctZ >= 0.0 ? 1.0 : -1.0;
		OctNormal = float2((1.0 - abs(OctZ)) * SignX, (1.0 - abs(OctX)) * SignZ);
	}
	else
	{
		OctNormal = float2(OctX, OctZ);
	}
	
	float2 EncodedNormal = OctNormal * 0.5 + 0.5;
	
	// Output to render targets
	OUT.Target0 = float4(FinalColor, AlphaFinal);
	OUT.Target1 = float4(FinalColor, AlphaFinal);
	OUT.Target2 = float4(EncodedNormal, FinalRoughness, AlphaFinal);
	
	return OUT;
}
