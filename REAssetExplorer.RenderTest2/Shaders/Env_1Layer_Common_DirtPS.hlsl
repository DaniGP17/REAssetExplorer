Texture2D NormalRoughnessCavityMap          : register(t0);
Texture2D BaseAlphaMap          : register(t1);
Texture2D LayerMaskOcclusionMap   : register(t2);
Texture2D BaseDielectricMapBase             : register(t3);
Texture2D NormalRoughnessCavityMapBase      : register(t4);
Texture2D DirtWearMap                       : register(t5);
Texture2D ExtraWearMap                   : register(t6);

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
    float4 WaterLine_Color;
    float4 WaterLine_EdgeColor;

    float Occlusion_Use_SecondaryUV;
    float MaterialColor_Use_SecondaryUV;
    float LayerMask_Use_SecondaryUV;
    float BaseLayer_Use_SecondaryUV;
    float DirtMap_Use_SecondaryUV;
    float UV_Switch;
    float MaterialColor_AddBlend;
    float MaterialColor_Override;
    float MaterialColor_Intensity;
    float Roughness;
    float MaterialTranslucency;
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

    // ---------------------------------------------------------------------------
    // Unpack interpolators
    // ---------------------------------------------------------------------------
    float3 NormalWS   = IN.NormalWS;
    float3 TangentWS  = IN.TangentWS;
    float3 BinormalWS = IN.BinormalWS;
    float2 UV0        = IN.UV0;
    float2 UV1        = IN.UV1;

    // Map to pseudocode variable names
    // INTERPOLATOR0 = float4(NormalWS, UV0.x)
    // INTERPOLATOR1 = float4(UV0.y, UV1.x, UV1.y, TangentWS.x)
    // INTERPOLATOR2 = float4(TangentWS.y, TangentWS.z, BinormalSign, PositionWS.x)
    // INTERPOLATOR3 = float4(PositionWS.y, PositionWS.z, PrevCS.x, PrevCS.y)
    // INTERPOLATOR4 = PrevCS.w  (not available -> use 1.0 for w)
    float _84 = 1.0;          // prevPosCS.w (unavailable, safe default)
    float _85 = IN.PositionWS.y;
    float _86 = IN.PositionWS.z;
    float _87 = 0.0;          // prevPosCS.x (unavailable)
    float _88 = 0.0;          // prevPosCS.y (unavailable)
    float _89 = TangentWS.y;
    float _90 = TangentWS.z;
    float _91 = BinormalWS.z; // bitangent sign stored in BinormalWS.z
    float _92 = IN.PositionWS.x;
    float _93 = UV0.y;
    float _94 = UV1.x;
    float _95 = UV1.y;
    float _96 = TangentWS.x;
    float _97 = NormalWS.x;
    float _98 = NormalWS.y;
    float _99 = NormalWS.z;
    float _100 = UV0.x;
    float _101 = IN.PositionCS.x;
    float _102 = IN.PositionCS.y;

    // ---------------------------------------------------------------------------
    // Normalize normal
    // ---------------------------------------------------------------------------
    float _103 = dot(float3(_97,_98,_99), float3(_97,_98,_99));
    float _104 = rsqrt(_103);
    float _105 = _104 * _97;
    float _106 = _104 * _98;
    float _107 = _104 * _99;

    // Normalize tangent
    float _108 = dot(float3(_96,_89,_90), float3(_96,_89,_90));
    float _109 = rsqrt(_108);
    float _110 = _109 * _96;
    float _111 = _109 * _89;
    float _112 = _109 * _90;

    // Build bitangent (cross product tangent x normal)
    float _113 = _112 * _106;
    float _114 = _111 * _107;
    float _115 = _113 - _114;
    float _116 = _110 * _107;
    float _117 = _112 * _105;
    float _118 = _116 - _117;
    float _119 = _111 * _105;
    float _120 = _110 * _106;
    float _121 = _119 - _120;

    // Flip bitangent based on sign
    bool  _122 = (_91 < 0.0);
    float _126 = _122 ? -_115 : _115;
    float _127 = _122 ? -_118 : _118;
    float _128 = _122 ? -_121 : _121;

    // ---------------------------------------------------------------------------
    // Screen UV for motion vector output (Target3.xy)
    // Using SV_Position and SceneInfo.screenInverseSize approximation
    // screenInverseSize not available -> derive from pixel position
    // ---------------------------------------------------------------------------
    // _130/_131 = screenInverseSize.xy (not in UserMaterial -> approximate with ddx/ddy)
    // The motion vector output is: _153 = 1 - scrU*2 + prevUV.x/_84
    //                              _155 = scrV*2 - 1 + prevUV.y/_84
    // Since prev frame data is unavailable, output 0.
    float _153 = 0.0;
    float _155 = 0.0;

    // ---------------------------------------------------------------------------
    // Camera/view position from UserMaterial is not available.
    // View direction for view-dir dependent effects uses PositionWS only.
    // _137/_139/_141 = camera world position (transposeViewInvMat row w) -> approx 0
    // ---------------------------------------------------------------------------
    float _142 = _92;
    float _143 = _85;
    float _144 = _86;
    float _145 = dot(float3(_142,_143,_144), float3(_142,_143,_144));
    float _146 = rsqrt(_145);
    float _147 = _146 * _142;
    float _148 = _146 * _143;
    float _149 = _144 * _146;

    // ---------------------------------------------------------------------------
    // UserMaterial parameters (mapped from BindlessBuffer)
    // ---------------------------------------------------------------------------
    float _281 = BaseAlphaMap_Tiling_Offset.x;
    float _282 = BaseAlphaMap_Tiling_Offset.y;
    float _283 = BaseAlphaMap_Tiling_Offset.z;
    float _284 = BaseAlphaMap_Tiling_Offset.w;
    float _285 = BaseColor.x;
    float _286 = BaseColor.y;
    float _287 = BaseColor.z;
    float _288 = UV_Tiling_Offset.x;
    float _289 = UV_Tiling_Offset.y;
    float _290 = UV_Tiling_Offset.z;
    float _291 = UV_Tiling_Offset.w;
    float _292 = DirtColor1.x;
    float _293 = DirtColor1.y;
    float _294 = DirtColor1.z;
    float _295 = DirtColor2.x;
    float _296 = DirtColor2.y;
    float _297 = DirtColor2.z;
    float _298 = DirtColor3.x;
    float _299 = DirtColor3.y;
    float _300 = DirtColor3.z;
    float _301 = DirtWearMap_Tiling_Offset.x;
    float _302 = DirtWearMap_Tiling_Offset.y;
    float _303 = DirtWearMap_Tiling_Offset.z;
    float _304 = DirtWearMap_Tiling_Offset.w;
    float _305 = WaterLine_Color.x;
    float _306 = WaterLine_Color.y;
    float _307 = WaterLine_Color.z;
    float _308 = WaterLine_EdgeColor.x;
    float _309 = WaterLine_EdgeColor.y;
    float _310 = WaterLine_EdgeColor.z;
    float _311 = Occlusion_Use_SecondaryUV;
    float _312 = MaterialColor_Use_SecondaryUV;
    float _313 = LayerMask_Use_SecondaryUV;
    float _314 = BaseLayer_Use_SecondaryUV;
    float _315 = DirtMap_Use_SecondaryUV;
    float _316 = UV_Switch;
    float _317 = MaterialColor_AddBlend;
    float _318 = MaterialColor_Override;
    float _319 = MaterialColor_Intensity;
    float _320 = Roughness;
    float _321 = MaterialTranslucency;
    float _322 = UV_Tiling;
    float _323 = Use_AdvancedUVSetting;
    float _324 = Dirt_Enable;
    float _325 = DirtWearMap_Inverse;
    float _326 = DirtMask_Brightness;
    float _327 = DirtMask_Contrast;
    float _328 = DirtColor_AddBlend;
    float _329 = DirtColor_Override;
    float _330 = DirtColorControl;
    float _331 = Dirt_Metallic;
    float _332 = Dirt_Roughness;
    float _333 = DirtWearMap_Tiling;
    float _334 = Use_AdvancedUVSetting_DirtWearMap;
    float _335 = Enable_Oil;
    float _336 = Oil_Intensity;
    float _337 = Oil_Thickness;
    float _338 = Oil_WearTiling;
    float _339 = Oil_WearBlend;
    float _340 = Oil_DarkColor;
    float _341 = Oil_Roughness;
    float _342 = Enable_WaterLine;
    float _343 = WaterLine_WorldHeight;
    float _344 = WaterLine_BlurWidth;
    float _345 = -_344;
    float _346 = WaterLine_Contrast;
    float _347 = WaterLine_WearBlend;
    float _348 = WaterLine_WearTiling_Top;
    float _349 = WaterLine_WearTiling_Side;
    float _350 = WaterLine_EdgeColorOverride;
    float _351 = WaterLine_RoughnessBlend;
    float _352 = WaterLine_Roughness;
    float _353 = WaterLine_Roughness_Occluded;
    float _354 = WaterLine_NormalWeaken;
    float _355 = WaterLine_NormalWeaken_Occluded;

    // ---------------------------------------------------------------------------
    // UV computation (UV switch + advanced tiling)
    // Primary UV blend: lerp(UV1, UV0, UV_Switch) for BaseLayer
    // ---------------------------------------------------------------------------
    float _381 = _100 - _94;
    float _382 = _93 - _95;
    float _383 = _316 * _381;
    float _384 = _316 * _382;
    float _385 = _383 + _94;   // blendedUV_A.x
    float _386 = _384 + _95;   // blendedUV_A.y
    float _387 = _94 - _100;
    float _388 = _95 - _93;
    float _389 = _316 * _387;
    float _390 = _316 * _388;
    float _391 = _389 + _100;  // blendedUV_B.x
    float _392 = _390 + _93;   // blendedUV_B.y
    // BaseLayer UV blend by BaseLayer_Use_SecondaryUV
    float _393 = _385 - _391;
    float _394 = _386 - _392;
    float _395 = _393 * _314;
    float _396 = _394 * _314;
    float _397 = _395 + _391;  // baseLayerUV.x
    float _398 = _396 + _392;  // baseLayerUV.y

    // Advanced UV tiling
    float _399 = _288 - _322;
    float _400 = _289 - _322;
    float _401 = _399 * _323;
    float _402 = _400 * _323;
    float _403 = _323 * _290;
    float _404 = _323 * _291;
    float _405 = _401 + _322;  // tiledOffset.x
    float _406 = _402 + _322;  // tiledOffset.y
    // MaterialColor UV blend
    float _407 = _393 * _312;
    float _408 = _394 * _312;
    float _409 = _407 + _391;  // matColorUV.x
    float _410 = _408 + _392;  // matColorUV.y
    // Final base UV = baseLayerUV * tiledOffset + advancedOffset
    float _411 = _397 * _405;
    float _412 = _398 * _406;
    float _413 = _411 + _403;  // mainUV.x
    float _414 = _412 + _404;  // mainUV.y

    // ---------------------------------------------------------------------------
    // Sample BaseDielectricMapBase (_374) with AutomaticWrap (use BilinearWrap)
    // _426: BaseDielectricMapBase.SampleGrad(mainUV)
    // ---------------------------------------------------------------------------
    float2 _ddx_main = ddx_coarse(float2(_413, _414));
    float2 _ddy_main = ddy_coarse(float2(_413, _414));
    float4 _426 = BaseDielectricMapBase.SampleGrad(BilinearWrap, float2(_413, _414), _ddx_main, _ddy_main);
    float _427 = _426.x;
    float _428 = _426.y;
    float _429 = _426.z;
    float _430 = _426.w;

    // Modulate by BaseColor
    float _431 = _427 * _285;
    float _432 = _428 * _286;
    float _433 = _429 * _287;

    // ---------------------------------------------------------------------------
    // Sample NormalRoughnessCavityMapBase (_376) with same UV
    // ---------------------------------------------------------------------------
    float4 _445 = NormalRoughnessCavityMapBase.SampleGrad(BilinearWrap, float2(_413, _414), _ddx_main, _ddy_main);
    float _446 = _445.x;  // roughness channel
    float _447 = _445.y;  // normal.x
    float _448 = _445.z;  // cavity
    float _449 = _445.w;  // normal.y

    // BaseAlphaMap UV = matColorUV * BaseAlphaMap_Tiling + Offset
    float _450 = _409 * _281;
    float _451 = _410 * _282;
    float _452 = _450 + _283;  // alphaUV.x
    float _453 = _451 + _284;  // alphaUV.y
    float _454 = 1.0 - _430;   // 1 - alpha

    // ---------------------------------------------------------------------------
    // Sample BaseAlphaMap (_370) with alphaUV
    // ---------------------------------------------------------------------------
    float2 _ddx_alpha = ddx_coarse(float2(_452, _453));
    float2 _ddy_alpha = ddy_coarse(float2(_452, _453));
    float4 _466 = BaseAlphaMap.SampleGrad(BilinearWrap, float2(_452, _453), _ddx_alpha, _ddy_alpha);
    float _467 = _466.x;
    float _468 = _466.y;
    float _469 = _466.z;
    float _470 = _466.w;

    // ---------------------------------------------------------------------------
    // Sample LayerMaskOcclusionMap (_372) with blendedUV_A (primary UV blend)
    // ---------------------------------------------------------------------------
    float2 _ddx_blendA = ddx_coarse(float2(_385, _386));
    float2 _ddy_blendA = ddy_coarse(float2(_385, _386));
    float4 _482 = LayerMaskOcclusionMap.SampleGrad(BilinearWrap, float2(_385, _386), _ddx_blendA, _ddy_blendA);
    float _483 = _482.z;  // occlusion primary channel
    float _484 = _482.w;  // layer mask channel

    // ---------------------------------------------------------------------------
    // Unpack base normal from NormalRoughnessCavityMapBase
    // ---------------------------------------------------------------------------
    float _485 = _313 * _311;  // LayerMask_Use_SecondaryUV * Occlusion_Use_SecondaryUV
    float _486 = _447 * 2.0;
    float _487 = _449 * 2.0;
    float _488 = _486 - 1.0;   // normalBase.x (decoded)
    float _489 = _487 - 1.0;   // normalBase.y (decoded)
    // Oct-encoded normal reconstruction
    float _490 = _488 * _488;
    float _491b = (_488 > 0.0);
    float _492 = -_490;
    float _493 = _491b ? _490 : _492;
    float _494 = _489 * _489;
    float _495b = (_489 > 0.0);
    float _496 = -_494;
    float _497 = _495b ? _494 : _496;
    float _498 = _493 + _497;
    float _499_v = _498 * 0.5;
    float _500 = _493 - _497;
    float _501_v = _500 * 0.5;
    float _502 = abs(_499_v);
    float _503 = 1.0 - _502;
    float _504 = abs(_501_v);
    float _505 = _503 - _504;
    float _506 = dot(float3(_499_v,_501_v,_505), float3(_499_v,_501_v,_505));
    float _507 = rsqrt(_506);
    float _508 = _507 * _499_v;  // tangent-space normal base.x
    float _509 = _507 * _501_v;  // tangent-space normal base.y
    float _510 = _446 * _320;    // roughness * Roughness param

    // ---------------------------------------------------------------------------
    // Occlusion UV switch (secondary UV for occlusion sample)
    // ---------------------------------------------------------------------------
    float _523, _524;
    if (_485 > 0.0)
    {
        _523 = _483;
        _524 = _484;
    }
    else
    {
        // Sample LayerMaskOcclusionMap at blendedUV_B
        float2 _ddx_B = ddx_coarse(float2(_391, _392));
        float2 _ddy_B = ddy_coarse(float2(_391, _392));
        float4 _520 = LayerMaskOcclusionMap.SampleGrad(BilinearWrap, float2(_391, _392), _ddx_B, _ddy_B);
        _523 = _520.z;
        _524 = _520.w;
    }

    // Blend occlusion with LayerMask_Use_SecondaryUV
    float _525 = _483 - _523;
    float _526 = _525 * _313;
    float _527 = _526 + _523;  // occlusionMask

    // ---------------------------------------------------------------------------
    // WaterLine height factor
    // ---------------------------------------------------------------------------
    float _528 = _85 - _343;   // worldY - WaterLine_WorldHeight
    float _529 = _528 - _344;
    bool  _530b = (_344 > 0.0);
    float _531 = abs(_345);
    float _532 = max(0.0001, _531);
    float _533 = _530b ? -1.0 : 1.0;
    float _534 = _532 * _533;
    float _535 = _529 / _534;
    float _536 = saturate(_535);  // waterLineFactor

    // ---------------------------------------------------------------------------
    // Base color blending with MaterialColor
    // ---------------------------------------------------------------------------
    float _537 = _467 + _431;
    float _538 = _468 + _432;
    float _539 = _469 + _433;
    float _540 = _467 * _431;
    float _541 = _468 * _432;
    float _542 = _469 * _433;
    float _543 = saturate(_537);
    float _544 = saturate(_538);
    float _545 = saturate(_539);
    // Screen blend
    float _546 = _543 - _540;
    float _547 = _544 - _541;
    float _548 = _545 - _542;
    float _549 = _546 * _317;  // * MaterialColor_AddBlend
    float _550 = _547 * _317;
    float _551 = _548 * _317;
    float _552 = _549 + _540;
    float _553 = _550 + _541;
    float _554 = _551 + _542;
    float _555 = _470 * _319;  // alpha * MaterialColor_Intensity
    float _556 = _467 - _552;
    float _557 = _468 - _553;
    float _558 = _469 - _554;
    float _559 = _556 * _318;  // * MaterialColor_Override
    float _560 = _557 * _318;
    float _561 = _558 * _318;
    float _562 = _552 - _431;
    float _563 = _562 + _559;
    float _564 = _553 - _432;
    float _565 = _564 + _560;
    float _566 = _554 - _433;
    float _567 = _566 + _561;
    float _568 = _555 * _563;
    float _569 = _555 * _565;
    float _570 = _555 * _567;
    float _571 = _568 + _431;  // baseColorBlended.x
    float _572 = _569 + _432;  // baseColorBlended.y
    float _573 = _570 + _433;  // baseColorBlended.z

    // ---------------------------------------------------------------------------
    // Dirt wear map UV
    // ---------------------------------------------------------------------------
    float _574 = _301 - _333;
    float _575 = _302 - _333;
    float _576 = _574 * _334;
    float _577 = _575 * _334;
    float _578 = _334 * _303;
    float _579 = _334 * _304;
    float _580 = _576 + _333;  // dirtTiledU
    float _581 = _577 + _333;  // dirtTiledV

    // Alpha remap for metallic base
    float _582 = _454 * 1.087;
    float _583 = _582 - 0.087;
    float _584 = saturate(_583);

    // Occlusion blend by Occlusion_Use_SecondaryUV
    float _585 = _484 - _524;
    float _586 = _585 * _311;
    float _587 = _586 + _524;  // occlusionFinal

    // ---------------------------------------------------------------------------
    // Dirt mask sample (conditional on DirtMask_Brightness * Dirt_Enable)
    // ---------------------------------------------------------------------------
    float _588 = _326 * _324;
    float _611;
    if (_588 > 0.0)
    {
        // DirtMap UV uses DirtMap_Use_SecondaryUV
        float _590 = _315 * _387;
        float _591 = _315 * _388;
        float _592 = _590 + _100;
        float _593 = _591 + _93;
        float _594 = _592 * _580;
        float _595 = _593 * _581;
        float _596 = _594 + _578;
        float _597 = _595 + _579;
        float2 _ddx_dirt = ddx_coarse(float2(_596, _597));
        float2 _ddy_dirt = ddy_coarse(float2(_596, _597));
        float4 _609 = DirtWearMap.SampleGrad(BilinearWrap, float2(_596, _597), _ddx_dirt, _ddy_dirt);
        _611 = _609.x;
    }
    else
    {
        _611 = 0.0;
    }

    // Invert dirt mask
    float _612 = _611 * -2.0;
    float _613 = _612 + 1.0;
    float _614 = _613 * _325;   // * DirtWearMap_Inverse
    float _615 = _614 + _611;

    // ---------------------------------------------------------------------------
    // Dirt color blend (trilinear: DirtColor1, DirtColor2, DirtColor3)
    // ---------------------------------------------------------------------------
    float _616 = _330 - 0.5;   // DirtColorControl - 0.5
    float _617 = abs(_616);
    float _618 = _617 * 20.0;
    float _619 = exp2(_618);
    float _620 = 1.0 / _619;
    bool  _621b = (_330 < 0.5);
    float _622 = _621b ? 0.0 : 1.0;
    float _623 = max(_620, 1e-6);
    float _624 = log2(_623);
    float _625 = _624 * 0.5;
    float _626 = exp2(_625);
    float _627 = _619 - _626;
    float _628 = _627 * _622;
    float _629 = _628 + _626;
    float _630 = max(_615, 1e-6);
    float _631 = log2(_630);
    float _632 = _631 * _629;
    float _633 = exp2(_632);
    float _634 = _633 * 2.0;
    float _635 = _634 - 1.0;
    // Lerp DirtColor2 -> DirtColor3
    float _636 = _298 - _295;
    float _637 = _299 - _296;
    float _638 = _300 - _297;
    float _639 = _635 * _636;
    float _640 = _635 * _637;
    float _641 = _635 * _638;
    float _642 = _639 + _295;
    float _643 = _640 + _296;
    float _644 = _641 + _297;
    bool  _645b = (_633 < 0.5);
    float _646 = _645b ? 0.0 : 1.0;
    // Lerp DirtColor1 -> DirtColor2
    float _647 = _295 - _292;
    float _648 = _296 - _293;
    float _649 = _297 - _294;
    float _650 = _634 * _647;
    float _651 = _634 * _648;
    float _652 = _634 * _649;
    float _653 = _650 + _292;
    float _654 = _651 + _293;
    float _655 = _652 + _294;
    float _656 = _642 - _653;
    float _657 = _643 - _654;
    float _658 = _644 - _655;
    float _659 = _656 * _646;
    float _660 = _657 * _646;
    float _661 = _658 * _646;
    float _662 = _659 + _653;  // dirtColorFinal.x
    float _663 = _660 + _654;  // dirtColorFinal.y
    float _664 = _661 + _655;  // dirtColorFinal.z

    float _665 = _527 * _324;
    float _666 = _326 * 2.0;
    float _667 = _666 - 1.0;

    // Add dirt color on top of base
    float _668 = _662 + _571;
    float _669 = _663 + _572;
    float _670 = _664 + _573;
    float _671 = _662 * _571;
    float _672 = _663 * _572;
    float _673 = _664 * _573;
    float _674 = _667 + _665;
    float _675 = saturate(_674);
    float _676 = _675 * 2.0;
    float _677 = _676 - 1.0;
    float _678 = saturate(_677);
    float _679 = 1.0 - _615;
    float _680 = saturate(_676);
    float _681 = _678 * _679;
    float _682 = _680 * _615;
    float _683 = _668 - _671;
    float _684 = _669 - _672;
    float _685 = _670 - _673;
    float _686 = _683 * _328;  // * DirtColor_AddBlend
    float _687 = _684 * _328;
    float _688 = _685 * _328;
    float _689 = _686 + _671;
    float _690 = _687 + _672;
    float _691 = _688 + _673;

    float _692 = _327 * 0.5;   // DirtMask_Contrast * 0.5
    float _693 = 1.0 - _692;
    float _694 = _662 - _689;
    float _695 = _663 - _690;
    float _696 = _664 - _691;
    float _697 = _694 * _329;  // * DirtColor_Override
    float _698 = _695 * _329;
    float _699 = _696 * _329;
    float _700 = _681 - _692;
    float _701 = _700 + _682;
    float _702 = _693 - _692;
    bool  _703b = (_702 < 0.0);
    float _704 = abs(_702);
    float _705 = max(0.0001, _704);
    float _706 = _703b ? -1.0 : 1.0;
    float _707 = _705 * _706;
    float _708 = _701 / _707;
    float _709 = saturate(_708);
    float _710 = _689 - _571;
    float _711 = _710 + _697;
    float _712 = _690 - _572;
    float _713 = _712 + _698;
    float _714 = _691 - _573;
    float _715 = _714 + _699;
    float _716 = _709 * _711;
    float _717 = _709 * _713;
    float _718 = _709 * _715;
    float _719 = _716 + _571;  // finalColor.x (with dirt)
    float _720 = _717 + _572;  // finalColor.y
    float _721 = _718 + _573;  // finalColor.z

    float _722 = _510 * _332;  // roughness * Dirt_Roughness
    float _723 = _722 - _510;
    float _724 = _709 * _723;
    float _725 = _724 + _510;  // roughness with dirt

    float _726 = _584 * _331;  // metallic * Dirt_Metallic
    float _727 = _726 - _584;
    float _728 = _709 * _727;
    float _729 = _728 + _584;  // metallic with dirt

    // ---------------------------------------------------------------------------
    // WaterLine effect
    // ---------------------------------------------------------------------------
    float _840_v, _841_v, _842_v;
    if (_342 > 0.0)
    {
        // WaterLine enabled
        float _731 = frac(_536);
        float _837, _838, _839;
        if (_731 > 0.0)
        {
            // Side projection: ExtraWearMap sampled in world space XZ and YZ planes
            float _733 = dot(float3(_105,_106,_107), float3(1,0,0));
            float _734 = abs(_733);
            float _735 = _734 - 0.5;
            float _736 = _735 * -15.8217;
            float _737 = exp2(_736);
            float _738 = _737 + 1.0;
            float _739 = 1.0 / _738;

            // XZ plane sample
            float _740 = _349 * _92;
            float _741 = _349 * _85;
            float _742 = _349 * _86;
            float _743 = saturate(_739);
            float2 _ddx_xz = ddx_coarse(float2(_740, _741));
            float2 _ddy_xz = ddy_coarse(float2(_740, _741));
            float4 _755 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_740, _741), _ddx_xz, _ddy_xz);
            float _756 = _755.z;

            float _757 = _743 - _734;
            float _758 = _757 * 0.5;
            float _759 = _758 + _734;

            float _760 = dot(float3(_105,_106,_107), float3(0,1,0));

            // YZ plane sample (reuse ddx/ddy.y and _742)
            float2 _ddx_yz = ddx_coarse(float2(_742, _741));
            float2 _ddy_yz = ddy_coarse(float2(_742, _741));
            float4 _765 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_742, _741), _ddx_yz, _ddy_yz);
            float _766 = _765.z;

            // Top projection
            float _767 = _348 * _92;
            float _768 = _348 * _86;
            float _769 = abs(_760);
            float2 _ddx_top = ddx_coarse(float2(_767, _768));
            float2 _ddy_top = ddy_coarse(float2(_767, _768));
            float4 _778 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_767, _768), _ddx_top, _ddy_top);
            float _779 = _778.y;

            float _780 = _766 - _756;
            float _781 = _780 * _759;
            float _782 = _781 + _756;
            float _783 = _779 - _782;
            float _784 = _783 * _769;
            float _785 = _784 + _782;

            float _786 = _536 * 2.0;
            float _787 = _786 - 1.0;
            float _788 = saturate(_787);
            float _789 = 1.0 - _785;
            float _790 = saturate(_786);
            float _791 = _789 * _788;
            float _792 = _790 * _785;
            float _793 = _791 + _792;

            float _794 = _793;
            float _795 = _794 - _536;
            float _798 = _795 * _347;
            float _799 = _795 * _347;
            float _800 = _795 * _347;
            float _801 = _798 + _536;
            float _802 = _799 + _536;
            float _803 = _800 + _536;
            float _804 = _801 - 0.5;
            float _805 = _802 - 0.5;
            float _806 = _803 - 0.5;
            float _807 = max(_346, 1e-6);
            float _808 = log2(_807);
            float _809 = _808 * 10.0;
            float _810 = exp2(_809);
            float _811 = _810 * 990.0;
            float _812 = _811 + 10.0;
            float _813 = _804 * -1.44270;
            float _814 = _813 * _812;
            float _815 = _805 * -1.44270;
            float _816 = _815 * _812;
            float _817 = _806 * -1.44270;
            float _818 = _817 * _812;
            float _819 = exp2(_814);
            float _820 = exp2(_816);
            float _821 = exp2(_818);
            float _822 = _819 + 1.0;
            float _823 = _820 + 1.0;
            float _824 = _821 + 1.0;
            float _825 = 1.0 / _822;
            float _826 = 1.0 / _823;
            float _827 = 1.0 / _824;
            float _828 = saturate(_825);
            float _829 = saturate(_826);
            float _830 = saturate(_827);
            float _831 = _828 - _801;
            float _832 = _829 - _802;
            float _833 = _830 - _803;
            float _834 = _831 * _346;
            float _835 = _832 * _346;
            float _836 = _833 * _346;
            _837 = _834 + _801;
            _838 = _835 + _802;
            _839 = _836 + _803;
        }
        else
        {
            // _793 = _536 path
            float _794 = _536;
            float _795 = _794 - _536; // = 0
            float _798 = _795 * _347;
            float _799 = _795 * _347;
            float _800 = _795 * _347;
            float _801 = _798 + _536;
            float _802 = _799 + _536;
            float _803 = _800 + _536;
            float _804 = _801 - 0.5;
            float _805 = _802 - 0.5;
            float _806 = _803 - 0.5;
            float _807 = max(_346, 1e-6);
            float _808 = log2(_807);
            float _809 = _808 * 10.0;
            float _810 = exp2(_809);
            float _811 = _810 * 990.0;
            float _812 = _811 + 10.0;
            float _813 = _804 * -1.44270;
            float _814 = _813 * _812;
            float _815 = _805 * -1.44270;
            float _816 = _815 * _812;
            float _817 = _806 * -1.44270;
            float _818 = _817 * _812;
            float _819 = exp2(_814);
            float _820 = exp2(_816);
            float _821 = exp2(_818);
            float _822 = _819 + 1.0;
            float _823 = _820 + 1.0;
            float _824 = _821 + 1.0;
            float _825 = 1.0 / _822;
            float _826 = 1.0 / _823;
            float _827 = 1.0 / _824;
            float _828 = saturate(_825);
            float _829 = saturate(_826);
            float _830 = saturate(_827);
            float _831 = _828 - _801;
            float _832 = _829 - _802;
            float _833 = _830 - _803;
            float _834 = _831 * _346;
            float _835 = _832 * _346;
            float _836 = _833 * _346;
            _837 = _834 + _801;
            _838 = _835 + _802;
            _839 = _836 + _803;
        }
        _840_v = _837;
        _841_v = _838;
        _842_v = _839;
    }
    else
    {
        _840_v = 0.0;
        _841_v = 0.0;
        _842_v = 0.0;
    }

    // ---------------------------------------------------------------------------
    // Sample NormalRoughnessCavityMap (_368) at UV0 (primary UV)
    // ---------------------------------------------------------------------------
    float2 _ddx_uv0 = ddx_coarse(float2(_100, _93));
    float2 _ddy_uv0 = ddy_coarse(float2(_100, _93));
    float4 _854 = NormalRoughnessCavityMap.SampleGrad(BilinearWrap, float2(_100, _93), _ddx_uv0, _ddy_uv0);
    float _855 = _854.y;
    float _856 = _854.z;
    float _857 = _854.w;

    // Decode normal from NormalRoughnessCavityMap
    float _858 = _855 * 2.0;
    float _859 = _857 * 2.0;
    float _860 = _858 - 1.0;
    float _861 = _859 - 1.0;
    float _862 = _860 * _860;
    float _863b = (_860 > 0.0);
    float _864 = -_862;
    float _865 = _863b ? _862 : _864;
    float _866 = _861 * _861;
    float _867b = (_861 > 0.0);
    float _868 = -_866;
    float _869 = _867b ? _866 : _868;
    float _870 = _865 + _869;
    float _871_v = _870 * 0.5;
    float _872 = _865 - _869;
    float _873_v = _872 * 0.5;
    float _874 = abs(_871_v);
    float _875 = 1.0 - _874;
    float _876 = abs(_873_v);
    float _877 = _875 - _876;
    float _878 = dot(float3(_871_v,_873_v,_877), float3(_871_v,_873_v,_877));
    float _879 = rsqrt(_878);
    float _880 = _871_v * _879;
    float _881 = _873_v * _879;
    float _882 = _879 * _877;

    // ---------------------------------------------------------------------------
    // WaterLine color/roughness override
    // ---------------------------------------------------------------------------
    float _926, _927, _928, _929;
    if (_342 > 0.0)
    {
        float _883 = _719 * _308;
        float _884 = _720 * _309;
        float _885 = _721 * _310;
        float _886 = _352 - _353;
        float _887 = _587 * _886;
        float _888 = _887 + _353;
        float _889 = _719 * _305;
        float _890 = _720 * _306;
        float _891 = _721 * _307;
        float _892 = _308 - _883;
        float _893 = _309 - _884;
        float _894 = _310 - _885;
        float _895 = _892 * _350;
        float _896 = _893 * _350;
        float _897 = _894 * _350;
        float _898 = _895 + _883;
        float _899 = _896 + _884;
        float _900 = _897 + _885;
        float _901 = _888 - _725;
        float _902 = _901 * _351;
        float _903 = _902 * _840_v;
        float _904 = _903 + _725;
        float _905 = _889 - _898;
        float _906 = _890 - _899;
        float _907 = _891 - _900;
        float _908 = _840_v * _840_v;
        float _909 = _908 * _905;
        float _910 = _841_v * _841_v;
        float _911 = _910 * _906;
        float _912 = _842_v * _842_v;
        float _913 = _912 * _907;
        float _914 = _898 - _719;
        float _915 = _914 + _909;
        float _916 = _899 - _720;
        float _917 = _916 + _911;
        float _918 = _900 - _721;
        float _919 = _918 + _913;
        float _920 = _915 * _840_v;
        float _921 = _917 * _841_v;
        float _922 = _919 * _842_v;
        _926 = _920 + _719;
        _927 = _921 + _720;
        _928 = _922 + _721;
        _929 = _904;
    }
    else
    {
        _926 = _719;
        _927 = _720;
        _928 = _721;
        _929 = _725;
    }

    // ---------------------------------------------------------------------------
    // Combine base normal with layer normal
    // ---------------------------------------------------------------------------
    float _930 = _880 + _508;
    float _931 = _881 + _509;
    float _932 = dot(float4(_930,_931,_882,0), float4(_930,_931,_882,0));
    float _933 = rsqrt(_932);
    float _934 = _933 * _930;
    float _935 = _933 * _931;
    float _936 = _933 * _882;

    // Normalize tangent/bitangent/normal for TBN
    float _937 = dot(float3(_110,_111,_112), float3(_110,_111,_112));
    float _938 = rsqrt(_937);
    float _939 = dot(float3(_126,_127,_128), float3(_126,_127,_128));
    float _940 = rsqrt(_939);
    float _941 = _940 * _126;
    float _942 = _940 * _127;
    float _943 = _940 * _128;
    float _944 = dot(float3(_105,_106,_107), float3(_105,_106,_107));
    float _945 = rsqrt(_944);
    float _946 = _945 * _105;
    float _947 = _945 * _106;
    float _948 = _945 * _107;

    // Transform tangent-space normal to world space
    float _949 = _934 * _110;
    float _950 = _949 * _938;
    float _951 = _935 * _941 + _950;
    float _952 = _936 * _946 + _951;
    float _953 = _934 * _111;
    float _954 = _953 * _938;
    float _955 = _935 * _942 + _954;
    float _956 = _936 * _947 + _955;
    float _957 = _934 * _112;
    float _958 = _957 * _938;
    float _959 = _935 * _943 + _958;
    float _960 = _936 * _948 + _959;
    float _961 = dot(float3(_952,_956,_960), float3(_952,_956,_960));
    float _962 = rsqrt(_961);
    float _963 = _962 * _952;  // worldNormal.x
    float _964 = _962 * _956;  // worldNormal.y
    float _965 = _962 * _960;  // worldNormal.z

    // ---------------------------------------------------------------------------
    // Oil effect
    // ---------------------------------------------------------------------------
    float _1027, _1028, _1029, _1030;
    if (_335 > 0.0)
    {
        // Sample ExtraWearMap for oil wear
        float _967 = _397 * _338;
        float _968 = _398 * _338;
        float2 _ddx_oil = ddx_coarse(float2(_967, _968));
        float2 _ddy_oil = ddy_coarse(float2(_967, _968));
        float4 _977 = ExtraWearMap.SampleGrad(BilinearWrap, float2(_967, _968), _ddx_oil, _ddy_oil);
        float _978 = _977.x;
        float _979 = _978 * 2.0;
        float _980 = _979 - 1.0;
        float _981 = 1.0 - _340;   // 1 - Oil_DarkColor
        float _982 = _980 * _339;  // * Oil_WearBlend
        float _983 = _982 + _337;  // + Oil_Thickness
        float _984 = min(_929, _341); // min(Roughness, Oil_Roughness)
        float _985 = _926 * _981;
        float _986 = _927 * _981;
        float _987 = _928 * _981;

        // View-dependent oil iridescence
        float _988 = dot(float3(_147,_148,_149), float3(_963,_964,_965));
        float _989 = abs(_988);
        float _990 = _989 * _983;
        float _991 = _990 * 35.0;
        float _992 = _990 * 24.85;
        float _993 = _990 * 30.45;
        float _994 = cos(_992);
        float _995 = cos(_993);
        float _996 = cos(_991);
        float _997 = _994 * -0.5;
        float _998 = _995 * -0.5;
        float _999 = _996 * -0.5;
        float _1000 = _997 + 0.5;
        float _1001 = _998 + 0.5;
        float _1002 = _999 + 0.5;
        float _1003 = _997 * _990;
        float _1004 = _998 * _990;
        float _1005 = _999 * _990;
        float _1006 = _1000 - _1003;
        float _1007 = _1001 - _1004;
        float _1008 = _1002 - _1005;
        float _1009 = _985 * 2.0;
        float _1010 = _1006 * _1006;
        float _1011 = _1010 * _1009;
        float _1012 = _986 * 2.0;
        float _1013 = _1007 * _1007;
        float _1014 = _1013 * _1012;
        float _1015 = _987 * 2.0;
        float _1016 = _1008 * _1008;
        float _1017 = _1016 * _1015;
        float _1018 = _1011 - _985;
        float _1019 = _1014 - _986;
        float _1020 = _1017 - _987;
        float _1021 = _1018 * _336;  // * Oil_Intensity
        float _1022 = _1019 * _336;
        float _1023 = _1020 * _336;
        _1027 = _1021 + _985;
        _1028 = _1022 + _986;
        _1029 = _1023 + _987;
        _1030 = _984;
    }
    else
    {
        _1027 = _926;
        _1028 = _927;
        _1029 = _928;
        _1030 = _929;
    }

    // ---------------------------------------------------------------------------
    // WaterLine normal weakening
    // ---------------------------------------------------------------------------
    float _1031 = _354 - _355;
    float _1032 = _587 * _1031;
    float _1033 = _1032 + _355;
    float _1034 = _840_v * _1033;
    float _1035 = _841_v * _1033;
    float _1036 = _842_v * _1033;
    float _1037 = _105 - _963;
    float _1038 = _106 - _964;
    float _1039 = _107 - _965;
    float _1040 = _1034 * _1037;
    float _1041 = _1035 * _1038;
    float _1042 = _1036 * _1039;
    float _1043 = _1040 + _963;  // finalNormal.x
    float _1044 = _1041 + _964;  // finalNormal.y
    float _1045 = _1042 + _965;  // finalNormal.z

    // ---------------------------------------------------------------------------
    // Physical material index / translucency encoding
    // ---------------------------------------------------------------------------
    float _1046 = max(0.0, _321);   // MaterialTranslucency
    float _1047 = _856 * _448;     // cavity * cavity (from both normal maps)
    bool  _1048b = (_729 > 0.0);
    bool  _1049b = (_1046 <= 0.0333333);
    bool  _1050b = _1048b || _1049b;
    float _1065, _1066;
    if (_1050b)
    {
        float _1051 = _1047 * 0.04;
        float _1052 = min(0.08, _1051);
        float _1053 = max(_1052, _729);
        _1065 = _1053;
        _1066 = 0.666667;
    }
    else
    {
        float _1054 = _1046 * 15.49;
        float _1055 = _1054 + 0.5;
        float _1056 = round(_1055);
        float _1057 = _1056 * 0.0627451;
        float _1058 = _1047 * 0.5;
        float _1059 = saturate(_1058);
        float _1060 = _1059 * 15.49;
        float _1061 = _1060 + 0.5;
        float _1062 = round(_1061);
        float _1063 = _1062 * 0.00392157;
        float _1064 = _1063 + _1057;
        _1065 = _1064;
        _1066 = 0.0;
    }

    // ---------------------------------------------------------------------------
    // Oct-encode final world normal for GBuffer
    // ---------------------------------------------------------------------------
    float _1067 = abs(_1043);
    float _1068 = abs(_1045);
    float _1069 = _1068 + _1067;
    float _1070 = abs(_1044);
    float _1071 = _1069 + _1070;
    float _1072 = 1.0 / _1071;
    float _1073 = _1072 * _1043;
    float _1074 = _1072 * _1045;
    bool  _1075b = (_1044 <= 0.0);
    float _1076 = abs(_1074);
    float _1077 = abs(_1073);
    float _1078 = 1.0 - _1076;
    float _1079 = 1.0 - _1077;
    bool  _1080b = (_1073 >= 0.0);
    float _1081 = -_1078;
    float _1082 = _1080b ? _1078 : _1081;
    bool  _1083b = (_1074 >= 0.0);
    float _1084 = -_1079;
    float _1085 = _1083b ? _1079 : _1084;
    float _1086 = _1075b ? _1082 : _1073;
    float _1087 = _1075b ? _1085 : _1074;
    float _1088 = _1086 * 0.5;
    float _1089 = _1087 * 0.5;
    float _1090 = _1088 + 0.5;  // encodedNormal.x
    float _1091 = _1089 + 0.5;  // encodedNormal.y

    // GBuffer type flag contribution to material ID
    float _1094 = PhysicalMaterialIndex * 0.333333;
    float _1095 = _1094 + _1066;

    // ---------------------------------------------------------------------------
    // GBuffer outputs
    // ---------------------------------------------------------------------------
    OUT.Target0 = float4(_1027, _1028, _1029, _1065);
    OUT.Target1 = float4(_1090, _1091, _1030, _1095);
    OUT.Target2 = float4(_153, _155, _587, 1.0);

    return OUT;
}