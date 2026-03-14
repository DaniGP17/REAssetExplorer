Texture2D NormalRoughnessCavityMap       : register(t0);
Texture2D BaseAlphaMap                   : register(t1);
Texture2D LayerMaskOcclusionMap          : register(t2);
Texture2D BaseDielectricMapBase          : register(t3);
Texture2D NormalRoughnessCavityMapBase   : register(t4);
Texture2D BaseDielectricMap             : register(t5);
Texture2D NormalRoughnessCavityMap1      : register(t6);
Texture2D WearMap1                       : register(t7);
Texture2D DirtWearMap                    : register(t8);
Texture2D ExtraWearMap                   : register(t9);

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

	// Extract input parameters from new structure
	float _92 = 1.0; // Perspective W not available, using 1.0
	float _93 = IN.PositionWS.y;
	float _94 = IN.PositionWS.z;
	float _95 = 0.0; // Parallax UV offset X not available
	float _96 = 0.0; // Parallax UV offset Y not available
	float _97 = IN.TangentWS.y;
	float _98 = IN.TangentWS.z;
	float _99 = 1.0; // Handedness (not needed, binormal provided directly)
	float _100 = IN.PositionWS.x;
	float _101 = IN.UV0.y;  // UV secondary Y (swap: was UV1.y)
	float _102 = IN.UV1.x;  // UV primary X (swap: was UV0.x)
	float _103 = IN.UV1.y;  // UV primary Y (swap: was UV0.y)
	float _104 = IN.TangentWS.x;
	float _105 = IN.NormalWS.x;
	float _106 = IN.NormalWS.y;
	float _107 = IN.NormalWS.z;
	float _108 = IN.UV0.x;  // UV secondary X (swap: was UV1.x)
	float _109 = IN.PositionCS.x;
	float _110 = IN.PositionCS.y;
	
	// Normalize first normal vector
	float _111 = dot(float3(_105, _106, _107), float3(_105, _106, _107));
	float _112 = rsqrt(_111);
	float _113 = _112 * _105;
	float _114 = _112 * _106;
	float _115 = _112 * _107;
	
	// Normalize second tangent vector
	float _116 = dot(float3(_104, _97, _98), float3(_104, _97, _98));
	float _117 = rsqrt(_116);
	float _118 = _117 * _104;
	float _119 = _117 * _97;
	float _120 = _117 * _98;
	
	// Use binormal directly from input (no computation needed)
	float _134 = IN.BinormalWS.x;
	float _135 = IN.BinormalWS.y;
	float _136 = IN.BinormalWS.z;
	
	// Screen space calculations (removed screenInverseSize dependency)
	float _138 = 0.0;
	float _139 = 0.0;
	float _140 = _109 * 2.0;
	float _141 = _140 * _138;
	float _142 = _110 * 2.0;
	float _143 = _142 * _139;
	
	// Camera position (removed transposeViewInvMat dependency)
	float _145 = 0.0;
	float _147 = 0.0;
	float _149 = 0.0;
	
	// View direction calculation
	float _150 = _100 - _145;
	float _151 = _93 - _147;
	float _152 = _94 - _149;
	float _153 = dot(float3(_150, _151, _152), float3(_150, _151, _152));
	float _154 = rsqrt(_153);
	float _155 = _154 * _150;
	float _156 = _154 * _151;
	float _157 = _152 * _154;
	
	// Perspective correction
	float _158 = _95 / _92;
	float _159 = _96 / _92;
	float _160 = 1.0 - _141;
	float _161 = _160 + _158;
	float _162 = _143 + -1.0;
	float _163 = _162 + _159;
	
	// Material properties from UserMaterial cbuffer
	float _334 = BaseAlphaMap_Tiling_Offset.x;
	float _335 = BaseAlphaMap_Tiling_Offset.y;
	float _336 = BaseAlphaMap_Tiling_Offset.z;
	float _337 = BaseAlphaMap_Tiling_Offset.w;
	float _338 = BaseColor.x;
	float _339 = BaseColor.y;
	float _340 = BaseColor.z;
	float _341 = UV_Tiling_Offset.x;
	float _342 = UV_Tiling_Offset.y;
	float _343 = UV_Tiling_Offset.z;
	float _344 = UV_Tiling_Offset.w;
	float _345 = BaseColor1.x;
	float _346 = BaseColor1.y;
	float _347 = BaseColor1.z;
	float _348 = UV_Tiling_Offset1.x;
	float _349 = UV_Tiling_Offset1.y;
	float _350 = UV_Tiling_Offset1.z;
	float _351 = UV_Tiling_Offset1.w;
	float _352 = WearMap_Tiling_Offset1.x;
	float _353 = WearMap_Tiling_Offset1.y;
	float _354 = WearMap_Tiling_Offset1.z;
	float _355 = WearMap_Tiling_Offset1.w;
	float _356 = DirtColor1.x;
	float _357 = DirtColor1.y;
	float _358 = DirtColor1.z;
	float _359 = DirtColor2.x;
	float _360 = DirtColor2.y;
	float _361 = DirtColor2.z;
	float _362 = DirtColor3.x;
	float _363 = DirtColor3.y;
	float _364 = DirtColor3.z;
	float _365 = DirtWearMap_Tiling_Offset.x;
	float _366 = DirtWearMap_Tiling_Offset.y;
	float _367 = DirtWearMap_Tiling_Offset.z;
	float _368 = DirtWearMap_Tiling_Offset.w;
	float _369 = WaterLine_Color.x;
	float _370 = WaterLine_Color.y;
	float _371 = WaterLine_Color.z;
	float _372 = WaterLine_EdgeColor.x;
	float _373 = WaterLine_EdgeColor.y;
	float _374 = WaterLine_EdgeColor.z;
	float _375 = Occlusion_Use_SecondaryUV;
	float _376 = MaterialColor_Use_SecondaryUV;
	float _377 = LayerMask_Use_SecondaryUV;
	float _378 = BaseLayer_Use_SecondaryUV;
	float _379 = ExtraLayer_Use_SecondaryUV;
	float _380 = DirtMap_Use_SecondaryUV;
	float _381 = UV_Switch;
	float _382 = MaterialColor_AddBlend;
	float _383 = MaterialColor_Override;
	float _384 = MaterialColor_Intensity;
	float _385 = MaterialColor_AddBlend1;
	float _386 = MaterialColor_Override1;
	float _387 = MaterialColor_Intensity1;
	float _388 = Roughness;
	float _389 = MaterialTranslucency;
	float _390 = UV_Tiling;
	float _391 = Use_AdvancedUVSetting;
	float _392 = Roughness1;
	float _393 = MaterialTranslucency1;
	float _394 = UV_Tiling1;
	float _395 = Use_AdvancedUVSetting1;
	float _396 = NormalBlend_Balance1;
	float _397 = WearMap_Tiling1;
	float _398 = Use_AdvancedUVSetting_WearMap1;
	float _399 = WearMap_Inverse1;
	float _400 = WearMap_Blend1;
	float _401 = WearMap_Normal_BlendMode1;
	float _402 = WearMap_NormalIntensity1;
	float _403 = LayerMask_BrightnessMasking1;
	float _404 = LayerMask_Brightness;
	float _405 = LayerMask_Contrast1;
	float _406 = Dirt_Enable;
	float _407 = DirtWearMap_Inverse;
	float _408 = DirtMask_Brightness;
	float _409 = DirtMask_Contrast;
	float _410 = DirtColor_AddBlend;
	float _411 = DirtColor_Override;
	float _412 = DirtColorControl;
	float _413 = Dirt_Metallic;
	float _414 = Dirt_Roughness;
	float _415 = DirtWearMap_Tiling;
	float _416 = Use_AdvancedUVSetting_DirtWearMap;
	float _417 = Enable_Oil;
	float _418 = Oil_Intensity;
	float _419 = Oil_Thickness;
	float _420 = Oil_WearTiling;
	float _421 = Oil_WearBlend;
	float _422 = Oil_DarkColor;
	float _423 = Oil_Roughness;
	float _424 = Enable_WaterLine;
	float _425 = WaterLine_WorldHeight;
	float _426 = WaterLine_BlurWidth;
	float _427 = -0.0 - _426;
	float _428 = WaterLine_Contrast;
	float _429 = WaterLine_WearBlend;
	float _430 = WaterLine_WearTiling_Top;
	float _431 = WaterLine_WearTiling_Side;
	float _432 = WaterLine_EdgeColorOverride;
	float _433 = WaterLine_RoughnessBlend;
	float _434 = WaterLine_Roughness;
	float _435 = WaterLine_Roughness_Occluded;
	float _436 = WaterLine_NormalWeaken;
	float _437 = WaterLine_NormalWeaken_Occluded;
	
	// Texture indices (default to 0 - no bindless material system)
	int _440 = 0;
	int _441 = 0;
	int _442 = 0;
	int _443 = 0;
	int _446 = 0;
	int _447 = 0;
	int _448 = 0;
	int _449 = 0;
	int _452 = 0;
	int _453 = 0;
	
	// Texture indices (bindless)
	int _454 = _440 + 0; // _455 -> NormalRoughnessCavityMap
	int _456 = _441 + 0; // _457 -> BaseAlphaMap
	int _458 = _442 + 0; // _459 -> LayerMaskOcclusionMap
	int _460 = _443 + 0; // _461 -> BaseDielectricMapBase
	int _462 = _446 + 0; // _463 -> NormalRoughnessCavityMapBase
	int _464 = _447 + 0; // _465 -> BaseDielectricMap
	int _466 = _448 + 0; // _467 -> NormalRoughnessCavityMap1
	int _468 = _449 + 0; // _469 -> WearMap1
	int _470 = _452 + 0; // _471 -> DirtWearMap
	int _472 = _453 + 0; // _473 -> ExtraWearMap
	
	// UV calculations
	float _474 = _108 - _102;
	float _475 = _101 - _103;
	float _476 = _381 * _474;
	float _477 = _381 * _475;
	float _478 = _476 + _102;
	float _479 = _477 + _103;
	float _480 = _102 - _108;
	float _481 = _103 - _101;
	float _482 = _381 * _480;
	float _483 = _381 * _481;
	float _484 = _482 + _108;
	float _485 = _483 + _101;
	
	// Parallax setup
	float _486 = _377 * _375;
	
	// Checkerboard gradients
	float _487 = ddy_coarse(_478);
	float _488 = ddy_coarse(_479);
	float _489 = ddx_coarse(_478);
	float _490 = ddx_coarse(_479);
	
	float _492 = 1.0;
	float _493 = _492 * _489;
	float _494 = _492 * _490;
	float _495 = 1.0;
	float _496 = _495 * _487;
	float _497 = _495 * _488;
	
	// Sample first texture (label0) - LayerMaskOcclusionMap
	float4 _498 = LayerMaskOcclusionMap.SampleGrad(BilinearWrap, float2(_478, _479), float2(_493, _494), float2(_496, _497));
	float _499 = _498.x;
	float _500 = _498.z;
	float _501 = _498.w;
	
	// Conditional branch for parallax
	float _515, _516, _517;
	bool _502 = (_486 > 0.0);
	if (!_502)
	{
		// label1
		float _503 = ddy_coarse(_484);
		float _504 = ddy_coarse(_485);
		float _505 = ddx_coarse(_484);
		float _506 = ddx_coarse(_485);
		float _507 = _492 * _505;
		float _508 = _492 * _506;
		float _509 = _495 * _503;
		float _510 = _495 * _504;
		float4 _511 = LayerMaskOcclusionMap.SampleGrad(BilinearWrap, float2(_484, _485), float2(_507, _508), float2(_509, _510)); // LayerMaskOcclusionMap
		_515 = _511.x;
		_516 = _511.z;
		_517 = _511.w;
	}
	else
	{
		_515 = _499;
		_516 = _500;
		_517 = _501;
	}
	
	// label2 - Parallax interpolation
	float _518 = _499 - _515;
	float _519 = _500 - _516;
	float _520 = _518 * _377;
	float _521 = _519 * _377;
	float _522 = _520 + _515;
	float _523 = _521 + _516;
	float _524 = _478 - _484;
	float _525 = _479 - _485;
	float _526 = _524 * _378;
	float _527 = _525 * _378;
	float _528 = _526 + _484;
	float _529 = _527 + _485;
	float _530 = _524 * _379;
	float _531 = _525 * _379;
	float _532 = _530 + _484;
	float _533 = _531 + _485;
	
	// UV transform calculations
	float _534 = _341 - _390;
	float _535 = _342 - _390;
	float _536 = _534 * _391;
	float _537 = _535 * _391;
	float _538 = _391 * _343;
	float _539 = _391 * _344;
	float _540 = _536 + _390;
	float _541 = _537 + _390;
	float _542 = _352 - _397;
	float _543 = _353 - _397;
	float _544 = _542 * _398;
	float _545 = _543 * _398;
	float _546 = _398 * _354;
	float _547 = _398 * _355;
	float _548 = _544 + _397;
	float _549 = _545 + _397;
	float _550 = _402 * 4.0;
	float _551 = _550 + -2.0;
	float _552 = _348 - _394;
	float _553 = _349 - _394;
	float _554 = _552 * _395;
	float _555 = _553 * _395;
	float _556 = _395 * _350;
	float _557 = _395 * _351;
	float _558 = _554 + _394;
	float _559 = _555 + _394;
	float _560 = _524 * _376;
	float _561 = _525 * _376;
	float _562 = _560 + _484;
	float _563 = _561 + _485;
	
	// Sample detail normal (WearMap1)
	float _564 = _532 * _548;
	float _565 = _533 * _549;
	float _566 = _564 + _546;
	float _567 = _565 + _547;
	float _568 = ddy_coarse(_566);
	float _569 = ddy_coarse(_567);
	float _570 = ddx_coarse(_566);
	float _571 = ddx_coarse(_567);
	
	float _573 = 1.0;
	float _574 = _573 * _570;
	float _575 = _573 * _571;
	float _576 = 1.0;
	float _577 = _576 * _568;
	float _578 = _576 * _569;
	float4 _579 = WearMap1.SampleGrad(BilinearWrap, float2(_566, _567), float2(_574, _575), float2(_577, _578)); // WearMap1
	float _580 = _579.x;
	float _581 = _579.y;
	float _582 = _579.z;
	
	// Complex parallax calculations
	float _583 = _404 * 2.0;
	float _584 = _583 + -1.0;
	float _585 = _522 * 2.0;
	float _586 = _581 * 1.25;
	float _587 = _586 + -0.625;
	float _588 = _582 * 1.25;
	float _589 = _588 + -0.625;
	float _590 = _522 + _584;
	float _591 = saturate(_585);
	float _592 = _591 * _584;
	float _593 = _580 * -2.0;
	float _594 = _593 + 1.0;
	float _595 = _594 * _399;
	float _596 = _595 + _580;
	float _597 = _592 - _584;
	float _598 = _597 * _403;
	float _599 = _590 + _598;
	float _600 = _599 + _596;
	bool _601 = (_600 > 1.0);
	bool _602 = (_599 > 0.0);
	float _603 = 1.0 - _596;
	float _604 = _599 / _603;
	float _605 = _602 ? _604 : 0.0;
	float _606 = _601 ? 1.0 : _605;
	float _607 = _405 * 0.5;
	float _608 = 1.0 - _607;
	float _609 = _606 - _599;
	float _610 = _609 * _400;
	float _611 = _599 - _607;
	float _612 = _611 + _610;
	float _613 = _608 - _607;
	bool _614 = (_613 < 0.0);
	float _615 = abs(_613);
	float _616 = max(0.0001, _615);
	float _617 = _614 ? -1.0 : 1.0;
	float _618 = _616 * _617;
	float _619 = _612 / _618;
	float _620 = saturate(_619);
	float _621 = _587 * _551;
	float _622 = _589 * _551;
	float _623 = saturate(_621);
	float _624 = saturate(_622);
	float _625 = saturate(_620);
	
	// Sample multiple textures
	float _626 = _532 * _558;
	float _627 = _533 * _559;
	float _628 = _626 + _556;
	float _629 = _627 + _557;
	float _630 = _528 * _540;
	float _631 = _529 * _541;
	float _632 = _630 + _538;
	float _633 = _631 + _539;
	
	float _634 = ddy_coarse(_628);
	float _635 = ddy_coarse(_629);
	float _636 = ddx_coarse(_628);
	float _637 = ddx_coarse(_629);
	
	float _639 = 1.0;
	float _640 = _639 * _636;
	float _641 = _639 * _637;
	float _642 = 1.0;
	float _643 = _642 * _634;
	float _644 = _642 * _635;
	float4 _645 = BaseDielectricMap.SampleGrad(BilinearWrap, float2(_628, _629), float2(_640, _641), float2(_643, _644)); // BaseDielectricMap
	float _646 = _645.x;
	float _647 = _645.y;
	float _648 = _645.z;
	float _649 = _645.w;
	
	float _650 = ddy_coarse(_632);
	float _651 = ddy_coarse(_633);
	float _652 = ddx_coarse(_632);
	float _653 = ddx_coarse(_633);
	
	float _655 = 1.0;
	float _656 = _655 * _652;
	float _657 = _655 * _653;
	float _658 = 1.0;
	float _659 = _658 * _650;
	float _660 = _658 * _651;
	float4 _661 = BaseDielectricMapBase.SampleGrad(BilinearWrap, float2(_632, _633), float2(_656, _657), float2(_659, _660)); // BaseDielectricMapBase
	float _662 = _661.x;
	float _663 = _661.y;
	float _664 = _661.z;
	float _665 = _661.w;
	
	float _666 = ddy_coarse(_632);
	float _667 = ddy_coarse(_633);
	float _668 = ddx_coarse(_632);
	float _669 = ddx_coarse(_633);
	
	float _671 = 1.0;
	float _672 = _671 * _668;
	float _673 = _671 * _669;
	float _674 = 1.0;
	float _675 = _674 * _666;
	float _676 = _674 * _667;
	float4 _677 = NormalRoughnessCavityMapBase.SampleGrad(BilinearWrap, float2(_632, _633), float2(_672, _673), float2(_675, _676)); // NormalRoughnessCavityMapBase
	float _678 = _677.x;
	float _679 = _677.y;
	float _680 = _677.z;
	float _681 = _677.w;
	
	float _682 = ddy_coarse(_628);
	float _683 = ddy_coarse(_629);
	float _684 = ddx_coarse(_628);
	float _685 = ddx_coarse(_629);
	
	float _687 = 1.0;
	float _688 = _687 * _684;
	float _689 = _687 * _685;
	float _690 = 1.0;
	float _691 = _690 * _682;
	float _692 = _690 * _683;
	float4 _693 = NormalRoughnessCavityMap1.SampleGrad(BilinearWrap, float2(_628, _629), float2(_688, _689), float2(_691, _692)); // NormalRoughnessCavityMap1
	float _694 = _693.x;
	float _695 = _693.y;
	float _696 = _693.z;
	float _697 = _693.w;
	
	// Albedo and mask calculations
	float _698 = _646 * _345;
	float _699 = _647 * _346;
	float _700 = _648 * _347;
	float _701 = _662 * _338;
	float _702 = _663 * _339;
	float _703 = _664 * _340;
	float _704 = 1.0 - _665;
	float _705 = _562 * _334;
	float _706 = _563 * _335;
	float _707 = _705 + _336;
	float _708 = _706 + _337;
	
	// Normal map decoding - first normal
	float _709 = _679 * 2.0;
	float _710 = _681 * 2.0;
	float _711 = _709 + -1.0;
	float _712 = _710 + -1.0;
	float _713 = _711 * _711;
	bool _714 = (_711 > 0.0);
	float _715 = -0.0 - _713;
	float _716 = _714 ? _713 : _715;
	float _717 = _712 * _712;
	bool _718 = (_712 > 0.0);
	float _719 = -0.0 - _717;
	float _720 = _718 ? _717 : _719;
	float _721 = _716 + _720;
	float _722 = _721 * 0.5;
	float _723 = _716 - _720;
	float _724 = _723 * 0.5;
	float _725 = abs(_722);
	float _726 = 1.0 - _725;
	float _727 = abs(_724);
	float _728 = _726 - _727;
	float _729 = dot(float3(_722, _724, _728), float3(_722, _724, _728));
	float _730 = rsqrt(_729);
	float _731 = _730 * _722;
	float _732 = _730 * _724;
	float _733 = _730 * _728;
	
	// Normal map decoding - second normal
	float _734 = _695 * 2.0;
	float _735 = _697 * 2.0;
	float _736 = _734 + -1.0;
	float _737 = _735 + -1.0;
	float _738 = _736 * _736;
	bool _739 = (_736 > 0.0);
	float _740 = -0.0 - _738;
	float _741 = _739 ? _738 : _740;
	float _742 = _737 * _737;
	bool _743 = (_737 > 0.0);
	float _744 = -0.0 - _742;
	float _745 = _743 ? _742 : _744;
	float _746 = _741 + _745;
	float _747 = _746 * 0.5;
	float _748 = _741 - _745;
	float _749 = _748 * 0.5;
	float _750 = abs(_747);
	float _751 = 1.0 - _750;
	float _752 = abs(_749);
	float _753 = _751 - _752;
	float _754 = dot(float3(_747, _749, _753), float3(_747, _749, _753));
	float _755 = rsqrt(_754);
	float _756 = _755 * _747;
	float _757 = _755 * _749;
	float _758 = _755 * _753;
	
	float _759 = 1.0 - _649;
	float _760 = _694 * _392;
	float _761 = _678 * _388;
	
	// Sample additional texture (BaseAlphaMap)
	float _762 = ddy_coarse(_707);
	float _763 = ddy_coarse(_708);
	float _764 = ddx_coarse(_707);
	float _765 = ddx_coarse(_708);
	
	float _767 = 1.0;
	float _768 = _767 * _764;
	float _769 = _767 * _765;
	float _770 = 1.0;
	float _771 = _770 * _762;
	float _772 = _770 * _763;
	float4 _773 = BaseAlphaMap.SampleGrad(BilinearWrap, float2(_707, _708), float2(_768, _769), float2(_771, _772)); // BaseAlphaMap
	float _774 = _773.x;
	float _775 = _773.y;
	float _776 = _773.z;
	float _777 = _773.w;
	
	// Material blending
	float _778 = _387 - _384;
	float _779 = _385 - _382;
	float _780 = _386 - _383;
	float _781 = _625 * _778;
	float _782 = _625 * _779;
	float _783 = _625 * _780;
	float _784 = _781 + _384;
	float _785 = _782 + _382;
	float _786 = _783 + _383;
	float _787 = _698 - _701;
	float _788 = _699 - _702;
	float _789 = _700 - _703;
	float _790 = _787 * _625;
	float _791 = _788 * _625;
	float _792 = _789 * _625;
	float _793 = _790 + _701;
	float _794 = _791 + _702;
	float _795 = _792 + _703;
	float _796 = _759 - _704;
	float _797 = _796 * _625;
	float _798 = _797 + _704;
	
	// Height fade calculations
	float _799 = _93 - _425;
	float _800 = _799 - _426;
	bool _801 = (_426 > -0.0);
	float _802 = abs(_427);
	float _803 = max(0.0001, _802);
	float _804 = _801 ? -1.0 : 1.0;
	float _805 = _803 * _804;
	float _806 = _800 / _805;
	float _807 = saturate(_806);
	
	float _808 = _760 - _761;
	float _809 = _808 * _625;
	float _810 = _809 + _761;
	float _811 = _774 + _793;
	float _812 = _775 + _794;
	float _813 = _776 + _795;
	float _814 = _774 * _793;
	float _815 = _775 * _794;
	float _816 = _776 * _795;
	float _817 = saturate(_811);
	float _818 = saturate(_812);
	float _819 = saturate(_813);
	float _820 = _817 - _814;
	float _821 = _818 - _815;
	float _822 = _819 - _816;
	float _823 = _820 * _785;
	float _824 = _821 * _785;
	float _825 = _822 * _785;
	float _826 = _823 + _814;
	float _827 = _824 + _815;
	float _828 = _825 + _816;
	float _829 = _777 * _784;
	float _830 = _774 - _826;
	float _831 = _775 - _827;
	float _832 = _776 - _828;
	float _833 = _830 * _786;
	float _834 = _831 * _786;
	float _835 = _832 * _786;
	float _836 = _826 - _793;
	float _837 = _836 + _833;
	float _838 = _827 - _794;
	float _839 = _838 + _834;
	float _840 = _828 - _795;
	float _841 = _840 + _835;
	float _842 = _829 * _837;
	float _843 = _829 * _839;
	float _844 = _829 * _841;
	float _845 = _842 + _793;
	float _846 = _843 + _794;
	float _847 = _844 + _795;
	
	// UV transform for another set
	float _848 = _365 - _415;
	float _849 = _366 - _415;
	float _850 = _416 * _848;
	float _851 = _416 * _849;
	float _852 = _416 * _367;
	float _853 = _416 * _368;
	float _854 = _850 + _415;
	float _855 = _851 + _415;
	
	float _856 = _798 * 1.087;
	float _857 = _856 + -0.087;
	float _858 = saturate(_857);
	float _859 = _501 - _517;
	float _860 = _859 * _375;
	float _861 = _860 + _517;
	
	// Conditional AO map (label3)
	float _862 = _408 * _406;
	bool _863 = (_862 > 0.0);
	float _885;
	if (_863)
	{
		// label3
		float _864 = _380 * _480;
		float _865 = _380 * _481;
		float _866 = _864 + _108;
		float _867 = _865 + _101;
		float _868 = _866 * _854;
		float _869 = _867 * _855;
		float _870 = _868 + _852;
		float _871 = _869 + _853;
		float _872 = ddy_coarse(_870);
		float _873 = ddy_coarse(_871);
		float _874 = ddx_coarse(_870);
		float _875 = ddx_coarse(_871);
		
		float _877 = 1.0;
		float _878 = _877 * _874;
		float _879 = _877 * _875;
		float _880 = 1.0;
		float _881 = _880 * _872;
		float _882 = _880 * _873;
		float4 _883 = DirtWearMap.SampleGrad(BilinearWrap, float2(_870, _871), float2(_878, _879), float2(_881, _882)); // DirtWearMap
		_885 = _883.x;
	}
	else
	{
		_885 = 0.0;
	}
	
	// label4 - AO processing
	float _886 = _885 * -2.0;
	float _887 = _886 + 1.0;
	float _888 = _887 * _407;
	float _889 = _888 + _885;
	float _890 = _412 + -0.5;
	float _891 = abs(_890);
	float _892 = _891 * 20.0;
	float _893 = exp2(_892);
	float _894 = 1.0 / _893;
	bool _895 = (_412 < 0.5);
	float _896 = _895 ? 0.0 : 1.0;
	float _897 = max(_894, 1e-06);
	float _898 = log2(_897);
	float _899 = _898 * 0.5;
	float _900 = exp2(_899);
	float _901 = _893 - _900;
	float _902 = _901 * _896;
	float _903 = _902 + _900;
	float _904 = max(_889, 1e-06);
	float _905 = log2(_904);
	float _906 = _905 * _903;
	float _907 = exp2(_906);
	float _908 = _907 * 2.0;
	float _909 = _908 + -1.0;
	
	// Shadow color blending
	float _910 = _362 - _359;
	float _911 = _363 - _360;
	float _912 = _364 - _361;
	float _913 = _909 * _910;
	float _914 = _909 * _911;
	float _915 = _909 * _912;
	float _916 = _913 + _359;
	float _917 = _914 + _360;
	float _918 = _915 + _361;
	bool _919 = (_907 < 0.5);
	float _920 = _919 ? 0.0 : 1.0;
	float _921 = _359 - _356;
	float _922 = _360 - _357;
	float _923 = _361 - _358;
	float _924 = _908 * _921;
	float _925 = _908 * _922;
	float _926 = _908 * _923;
	float _927 = _924 + _356;
	float _928 = _925 + _357;
	float _929 = _926 + _358;
	float _930 = _916 - _927;
	float _931 = _917 - _928;
	float _932 = _918 - _929;
	float _933 = _930 * _920;
	float _934 = _931 * _920;
	float _935 = _932 * _920;
	float _936 = _933 + _927;
	float _937 = _934 + _928;
	float _938 = _935 + _929;
	
	float _939 = _523 * _406;
	float _940 = _408 * 2.0;
	float _941 = _940 + -1.0;
	float _942 = _936 + _845;
	float _943 = _937 + _846;
	float _944 = _938 + _847;
	float _945 = _936 * _845;
	float _946 = _937 * _846;
	float _947 = _938 * _847;
	float _948 = _941 + _939;
	float _949 = saturate(_948);
	float _950 = _949 * 2.0;
	float _951 = _950 + -1.0;
	float _952 = saturate(_951);
	float _953 = 1.0 - _889;
	float _954 = saturate(_950);
	float _955 = _952 * _953;
	float _956 = _954 * _889;
	float _957 = _942 - _945;
	float _958 = _943 - _946;
	float _959 = _944 - _947;
	float _960 = _957 * _410;
	float _961 = _958 * _410;
	float _962 = _959 * _410;
	float _963 = _960 + _945;
	float _964 = _961 + _946;
	float _965 = _962 + _947;
	float _966 = _409 * 0.5;
	float _967 = 1.0 - _966;
	float _968 = _936 - _963;
	float _969 = _937 - _964;
	float _970 = _938 - _965;
	float _971 = _968 * _411;
	float _972 = _969 * _411;
	float _973 = _970 * _411;
	float _974 = _955 - _966;
	float _975 = _974 + _956;
	float _976 = _967 - _966;
	bool _977 = (_976 < 0.0);
	float _978 = abs(_976);
	float _979 = max(0.0001, _978);
	float _980 = _977 ? -1.0 : 1.0;
	float _981 = _979 * _980;
	float _982 = _975 / _981;
	float _983 = saturate(_982);
	float _984 = _963 - _845;
	float _985 = _984 + _971;
	float _986 = _964 - _846;
	float _987 = _986 + _972;
	float _988 = _965 - _847;
	float _989 = _988 + _973;
	float _990 = _983 * _985;
	float _991 = _983 * _987;
	float _992 = _983 * _989;
	float _993 = _990 + _845;
	float _994 = _991 + _846;
	float _995 = _992 + _847;
	
	float _996 = _810 * _414;
	float _997 = _996 - _810;
	float _998 = _983 * _997;
	float _999 = _998 + _810;
	float _1000 = _858 * _413;
	float _1001 = _1000 - _858;
	float _1002 = _983 * _1001;
	float _1003 = _1002 + _858;
	
	// Conditional height blend fade (label5-7)
	bool _1004 = (_424 > 0.0);
	float _1114, _1115, _1116;
	if (_1004)
	{
		// label5
		float _1005 = frac(_807);
		bool _1006 = (_1005 > 0.0);
		float _1068;
		if (_1006)
		{
			// label6
			float _1007 = dot(float3(_113, _114, _115), float3(1.0, 0.0, 0.0));
			float _1008 = abs(_1007);
			float _1009 = _1008 + -0.5;
			float _1010 = _1009 * -15.8217;
			float _1011 = exp2(_1010);
			float _1012 = _1011 + 1.0;
			float _1013 = 1.0 / _1012;
			float _1014 = _431 * _100;
			float _1015 = _431 * _93;
			float _1016 = _431 * _94;
			float _1017 = saturate(_1013);
			float _1018 = ddy_coarse(_1014);
			float _1019 = ddy_coarse(_1015);
			float _1020 = ddx_coarse(_1014);
			float _1021 = ddx_coarse(_1015);
			
			float _1023 = 1.0;
			float _1024 = _1023 * _1020;
			float _1025 = _1023 * _1021;
			float _1026 = 1.0;
			float _1027 = _1026 * _1018;
			float _1028 = _1026 * _1019;
			float4 _1029 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_1014, _1015), float2(_1024, _1025), float2(_1027, _1028)); // ExtraWearMap
			float _1030 = _1029.z;
			float _1031 = _1017 - _1008;
			float _1032 = _1031 * 0.5;
			float _1033 = _1032 + _1008;
			float _1034 = dot(float3(_113, _114, _115), float3(0.0, 1.0, 0.0));
			float _1035 = ddy_coarse(_1016);
			float _1036 = ddx_coarse(_1016);
			float _1037 = _1023 * _1036;
			float _1038 = _1026 * _1035;
			float4 _1039 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_1016, _1015), float2(_1037, _1025), float2(_1038, _1028)); // ExtraWearMap
			float _1040 = _1039.z;
			float _1041 = _430 * _100;
			float _1042 = _430 * _94;
			float _1043 = abs(_1034);
			float _1044 = ddy_coarse(_1041);
			float _1045 = ddy_coarse(_1042);
			float _1046 = ddx_coarse(_1041);
			float _1047 = ddx_coarse(_1042);
			float _1048 = _1023 * _1046;
			float _1049 = _1023 * _1047;
			float _1050 = _1026 * _1044;
			float _1051 = _1026 * _1045;
			float4 _1052 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_1041, _1042), float2(_1048, _1049), float2(_1050, _1051)); // ExtraWearMap
			float _1053 = _1052.y;
			float _1054 = _1040 - _1030;
			float _1055 = _1054 * _1033;
			float _1056 = _1055 + _1030;
			float _1057 = _1053 - _1056;
			float _1058 = _1057 * _1043;
			float _1059 = _1058 + _1056;
			float _1060 = _807 * 2.0;
			float _1061 = _1060 + -1.0;
			float _1062 = saturate(_1061);
			float _1063 = 1.0 - _1059;
			float _1064 = saturate(_1060);
			float _1065 = _1063 * _1062;
			float _1066 = _1064 * _1059;
			_1068 = _1065 + _1066;
		}
		else
		{
			_1068 = _807;
		}
		
		// label7
		float _1069 = _1068 - _807;
		float _1070 = _1068 - _807;
		float _1071 = _1068 - _807;
		float _1072 = _1069 * _429;
		float _1073 = _1070 * _429;
		float _1074 = _1071 * _429;
		float _1075 = _1072 + _807;
		float _1076 = _1073 + _807;
		float _1077 = _1074 + _807;
		float _1078 = _1075 + -0.5;
		float _1079 = _1076 + -0.5;
		float _1080 = _1077 + -0.5;
		float _1081 = max(_428, 1e-06);
		float _1082 = log2(_1081);
		float _1083 = _1082 * 10.0;
		float _1084 = exp2(_1083);
		float _1085 = _1084 * 990.0;
		float _1086 = _1085 + 10.0;
		float _1087 = _1078 * -1.4427;
		float _1088 = _1087 * _1086;
		float _1089 = _1079 * -1.4427;
		float _1090 = _1089 * _1086;
		float _1091 = _1080 * -1.4427;
		float _1092 = _1091 * _1086;
		float _1093 = exp2(_1088);
		float _1094 = exp2(_1090);
		float _1095 = exp2(_1092);
		float _1096 = _1093 + 1.0;
		float _1097 = _1094 + 1.0;
		float _1098 = _1095 + 1.0;
		float _1099 = 1.0 / _1096;
		float _1100 = 1.0 / _1097;
		float _1101 = 1.0 / _1098;
		float _1102 = saturate(_1099);
		float _1103 = saturate(_1100);
		float _1104 = saturate(_1101);
		float _1105 = _1102 - _1075;
		float _1106 = _1103 - _1076;
		float _1107 = _1104 - _1077;
		float _1108 = _1105 * _428;
		float _1109 = _1106 * _428;
		float _1110 = _1107 * _428;
		_1114 = _1108 + _1075;
		_1115 = _1109 + _1076;
		_1116 = _1110 + _1077;
	}
	else
	{
		_1114 = 0.0;
		_1115 = 0.0;
		_1116 = 0.0;
	}
	
	// label8 - Sample base color (NormalRoughnessCavityMap)
	float _1117 = ddy_coarse(_108);
	float _1118 = ddy_coarse(_101);
	float _1119 = ddx_coarse(_108);
	float _1120 = ddx_coarse(_101);
	
	float _1122 = 1.0;
	float _1123 = _1122 * _1119;
	float _1124 = _1122 * _1120;
	float _1125 = 1.0;
	float _1126 = _1125 * _1117;
	float _1127 = _1125 * _1118;
	float4 _1128 = NormalRoughnessCavityMap.SampleGrad(BilinearWrap, float2(_108, _101), float2(_1123, _1124), float2(_1126, _1127)); // NormalRoughnessCavityMap
	float _1129 = _1128.y;
	float _1130 = _1128.z;
	float _1131 = _1128.w;
	
	// Normal blending
	float _1132 = _756 + _623;
	float _1133 = _757 + _624;
	float _1134 = dot(float4(_1132, _1133, _758, 0.0), float4(_1132, _1133, _758, 0.0));
	float _1135 = rsqrt(_1134);
	float _1136 = _1135 * _1132;
	float _1137 = _1135 * _1133;
	float _1138 = _1136 - _756;
	float _1139 = _1137 - _757;
	float _1140 = _1138 * _401;
	float _1141 = _1139 * _401;
	float _1142 = _1140 + _756;
	float _1143 = _1141 + _757;
	float _1144 = _1142 * _625;
	float _1145 = _1143 * _625;
	float _1146 = _1142 - _731;
	float _1147 = _1143 - _732;
	float _1148 = _1146 * _625;
	float _1149 = _1147 * _625;
	float _1150 = _1148 + _731;
	float _1151 = _1149 + _732;
	float _1152 = _1144 + _731;
	float _1153 = _1145 + _732;
	float _1154 = dot(float4(_1152, _1153, _733, 0.0), float4(_1152, _1153, _733, 0.0));
	float _1155 = rsqrt(_1154);
	float _1156 = _1152 * _1155;
	float _1157 = _1153 * _1155;
	float _1158 = _1150 - _1156;
	float _1159 = _1151 - _1157;
	float _1160 = _1158 * _396;
	float _1161 = _1159 * _396;
	
	// Detail normal map decoding
	float _1162 = _1129 * 2.0;
	float _1163 = _1131 * 2.0;
	float _1164 = _1162 + -1.0;
	float _1165 = _1163 + -1.0;
	float _1166 = _1164 * _1164;
	bool _1167 = (_1164 > 0.0);
	float _1168 = -0.0 - _1166;
	float _1169 = _1167 ? _1166 : _1168;
	float _1170 = _1165 * _1165;
	bool _1171 = (_1165 > 0.0);
	float _1172 = -0.0 - _1170;
	float _1173 = _1171 ? _1170 : _1172;
	float _1174 = _1169 + _1173;
	float _1175 = _1174 * 0.5;
	float _1176 = _1169 - _1173;
	float _1177 = _1176 * 0.5;
	float _1178 = abs(_1175);
	float _1179 = 1.0 - _1178;
	float _1180 = abs(_1177);
	float _1181 = _1179 - _1180;
	float _1182 = dot(float3(_1175, _1177, _1181), float3(_1175, _1177, _1181));
	float _1183 = rsqrt(_1182);
	float _1184 = _1183 * _1175;
	float _1185 = _1183 * _1177;
	float _1186 = _1183 * _1181;
	float _1187 = _1184 + _1156;
	float _1188 = _1187 + _1160;
	float _1189 = _1185 + _1157;
	float _1190 = _1189 + _1161;
	float _1191 = dot(float4(_1188, _1190, _1186, 0.0), float4(_1188, _1190, _1186, 0.0));
	float _1192 = rsqrt(_1191);
	float _1193 = _1188 * _1192;
	float _1194 = _1190 * _1192;
	float _1195 = _1192 * _1186;
	
	// Conditional wetness (label9-10)
	float _1239, _1240, _1241, _1242;
	if (_1004)
	{
		// label9
		float _1196 = _993 * _372;
		float _1197 = _994 * _373;
		float _1198 = _995 * _374;
		float _1199 = _434 - _435;
		float _1200 = _861 * _1199;
		float _1201 = _1200 + _435;
		float _1202 = _993 * _369;
		float _1203 = _994 * _370;
		float _1204 = _995 * _371;
		float _1205 = _372 - _1196;
		float _1206 = _373 - _1197;
		float _1207 = _374 - _1198;
		float _1208 = _1205 * _432;
		float _1209 = _1206 * _432;
		float _1210 = _1207 * _432;
		float _1211 = _1208 + _1196;
		float _1212 = _1209 + _1197;
		float _1213 = _1210 + _1198;
		float _1214 = _1201 - _999;
		float _1215 = _1214 * _433;
		float _1216 = _1215 * _1114;
		float _1217 = _1216 + _999;
		float _1218 = _1202 - _1211;
		float _1219 = _1203 - _1212;
		float _1220 = _1204 - _1213;
		float _1221 = _1114 * _1114;
		float _1222 = _1221 * _1218;
		float _1223 = _1115 * _1115;
		float _1224 = _1223 * _1219;
		float _1225 = _1116 * _1116;
		float _1226 = _1225 * _1220;
		float _1227 = _1211 - _993;
		float _1228 = _1227 + _1222;
		float _1229 = _1212 - _994;
		float _1230 = _1229 + _1224;
		float _1231 = _1213 - _995;
		float _1232 = _1231 + _1226;
		float _1233 = _1228 * _1114;
		float _1234 = _1230 * _1115;
		float _1235 = _1232 * _1116;
		_1239 = _1233 + _993;
		_1240 = _1234 + _994;
		_1241 = _1235 + _995;
		_1242 = _1217;
	}
	else
	{
		_1239 = _993;
		_1240 = _994;
		_1241 = _995;
		_1242 = _999;
	}
	
	// label10 - Transform normal to world space
	float _1243 = dot(float3(_118, _119, _120), float3(_118, _119, _120));
	float _1244 = rsqrt(_1243);
	float _1245 = dot(float3(_134, _135, _136), float3(_134, _135, _136));
	float _1246 = rsqrt(_1245);
	float _1247 = _1246 * _134;
	float _1248 = _1246 * _135;
	float _1249 = _1246 * _136;
	float _1250 = dot(float3(_113, _114, _115), float3(_113, _114, _115));
	float _1251 = rsqrt(_1250);
	float _1252 = _1251 * _113;
	float _1253 = _1251 * _114;
	float _1254 = _1251 * _115;
	float _1255 = _1193 * _118;
	float _1256 = _1255 * _1244;
	float _1257 = mad(_1194, _1247, _1256);
	float _1258 = mad(_1195, _1252, _1257);
	float _1259 = _1193 * _119;
	float _1260 = _1259 * _1244;
	float _1261 = mad(_1194, _1248, _1260);
	float _1262 = mad(_1195, _1253, _1261);
	float _1263 = _1193 * _120;
	float _1264 = _1263 * _1244;
	float _1265 = mad(_1194, _1249, _1264);
	float _1266 = mad(_1195, _1254, _1265);
	float _1267 = dot(float3(_1258, _1262, _1266), float3(_1258, _1262, _1266));
	float _1268 = rsqrt(_1267);
	float _1269 = _1268 * _1258;
	float _1270 = _1268 * _1262;
	float _1271 = _1268 * _1266;
	
	// Conditional glitter (label11-12)
	bool _1272 = (_417 > 0.0);
	float _1333, _1334, _1335, _1336;
	if (_1272)
	{
		// label11
		float _1273 = _528 * _420;
		float _1274 = _529 * _420;
		float _1275 = ddy_coarse(_1273);
		float _1276 = ddy_coarse(_1274);
		float _1277 = ddx_coarse(_1273);
		float _1278 = ddx_coarse(_1274);
		float _1279 = _1122 * _1277;
		float _1280 = _1122 * _1278;
		float _1281 = _1125 * _1275;
		float _1282 = _1125 * _1276;
		float4 _1283 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_1273, _1274), float2(_1279, _1280), float2(_1281, _1282)); // ExtraWearMap (glitter)
		float _1284 = _1283.x;
		float _1285 = _1284 * 2.0;
		float _1286 = _1285 + -1.0;
		float _1287 = 1.0 - _422;
		float _1288 = _1286 * _421;
		float _1289 = _1288 + _419;
		float _1290 = min(_1242, _423);
		float _1291 = _1239 * _1287;
		float _1292 = _1240 * _1287;
		float _1293 = _1241 * _1287;
		float _1294 = dot(float3(_155, _156, _157), float3(_1269, _1270, _1271));
		float _1295 = abs(_1294);
		float _1296 = _1295 * _1289;
		float _1297 = _1296 * 35.0;
		float _1298 = _1296 * 24.85;
		float _1299 = _1296 * 30.45;
		float _1300 = cos(_1298);
		float _1301 = cos(_1299);
		float _1302 = cos(_1297);
		float _1303 = _1300 * -0.5;
		float _1304 = _1301 * -0.5;
		float _1305 = _1302 * -0.5;
		float _1306 = _1303 + 0.5;
		float _1307 = _1304 + 0.5;
		float _1308 = _1305 + 0.5;
		float _1309 = _1303 * _1296;
		float _1310 = _1304 * _1296;
		float _1311 = _1305 * _1296;
		float _1312 = _1306 - _1309;
		float _1313 = _1307 - _1310;
		float _1314 = _1308 - _1311;
		float _1315 = _1291 * 2.0;
		float _1316 = _1312 * _1312;
		float _1317 = _1316 * _1315;
		float _1318 = _1292 * 2.0;
		float _1319 = _1313 * _1313;
		float _1320 = _1319 * _1318;
		float _1321 = _1293 * 2.0;
		float _1322 = _1314 * _1314;
		float _1323 = _1322 * _1321;
		float _1324 = _1317 - _1291;
		float _1325 = _1320 - _1292;
		float _1326 = _1323 - _1293;
		float _1327 = _1324 * _418;
		float _1328 = _1325 * _418;
		float _1329 = _1326 * _418;
		_1333 = _1327 + _1291;
		_1334 = _1328 + _1292;
		_1335 = _1329 + _1293;
		_1336 = _1290;
	}
	else
	{
		_1333 = _1239;
		_1334 = _1240;
		_1335 = _1241;
		_1336 = _1242;
	}
	
	// label12 - Final material calculations
	float _1337 = _436 - _437;
	float _1338 = _861 * _1337;
	float _1339 = _1338 + _437;
	float _1340 = _1114 * _1339;
	float _1341 = _1115 * _1339;
	float _1342 = _1116 * _1339;
	float _1343 = _113 - _1269;
	float _1344 = _114 - _1270;
	float _1345 = _115 - _1271;
	float _1346 = _1340 * _1343;
	float _1347 = _1341 * _1344;
	float _1348 = _1342 * _1345;
	float _1349 = _1346 + _1269;
	float _1350 = _1347 + _1270;
	float _1351 = _1348 + _1271;
	
	float _1352 = _393 - _389;
	float _1353 = _625 * _1352;
	float _1354 = _1353 + _389;
	float _1355 = max(0.0, _1354);
	float _1356 = _696 - _680;
	float _1357 = _1356 * _625;
	float _1358 = _1357 + _680;
	float _1359 = _1130 * _1358;
	
	// Conditional encoding (label13-15)
	bool _1360 = (_1003 > 0.0);
	bool _1361 = (_1355 <= 0.0333333);
	bool _1362 = _1360 || _1361;
	float _1377, _1378;
	if (_1362)
	{
		// label13
		float _1363 = _1359 * 0.04;
		float _1364 = min(0.08, _1363);
		_1377 = max(_1364, _1003);
		_1378 = 0.666667;
	}
	else
	{
		// label14
		float _1366 = _1355 * 15.49;
		float _1367 = _1366 + 0.5;
		float _1368 = round(_1367);
		float _1369 = _1368 * 0.0627451;
		float _1370 = _1359 * 0.5;
		float _1371 = saturate(_1370);
		float _1372 = _1371 * 15.49;
		float _1373 = _1372 + 0.5;
		float _1374 = round(_1373);
		float _1375 = _1374 * 0.00392157;
		_1377 = _1375 + _1369;
		_1378 = 0.0;
	}
	
	// label15 - Octahedral encoding for normals
	float _1379 = abs(_1349);
	float _1380 = abs(_1351);
	float _1381 = _1380 + _1379;
	float _1382 = abs(_1350);
	float _1383 = _1381 + _1382;
	float _1384 = 1.0 / _1383;
	float _1385 = _1384 * _1349;
	float _1386 = _1384 * _1351;
	bool _1387 = (_1350 <= 0.0);
	float _1388 = abs(_1386);
	float _1389 = abs(_1385);
	float _1390 = 1.0 - _1388;
	float _1391 = 1.0 - _1389;
	bool _1392 = (_1385 >= 0.0);
	float _1393 = -0.0 - _1390;
	float _1394 = _1392 ? _1390 : _1393;
	bool _1395 = (_1386 >= 0.0);
	float _1396 = -0.0 - _1391;
	float _1397 = _1395 ? _1391 : _1396;
	float _1398 = _1387 ? _1394 : _1385;
	float _1399 = _1387 ? _1397 : _1386;
	float _1400 = _1398 * 0.5;
	float _1401 = _1399 * 0.5;
	float _1402 = _1400 + 0.5;
	float _1403 = _1401 + 0.5;
	
	float _1406 = 0.333333;
	float _1407 = _1406 + _1378;
	
	OUT.Target0.x = _1333;
	OUT.Target0.y = _1334;
	OUT.Target0.z = _1335;
	OUT.Target0.w = _1377;
	OUT.Target1.x = _1402;
	OUT.Target1.y = _1403;
	OUT.Target1.z = _1336;
	OUT.Target1.w = _1407;
	OUT.Target2.x = _161;
	OUT.Target2.y = _163;
	OUT.Target2.z = _861;
	OUT.Target2.w = 1.0;
	
	return OUT;
}
