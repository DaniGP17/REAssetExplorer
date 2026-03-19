#include "Explorer/LightComponents.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

std::size_t FieldSize(const RszInstance& c, std::size_t index) {
    const RszBytes* b = index < c.fields.size() ? c.fields[index].value.As<RszBytes>() : nullptr;
    return b ? b->size() : 0;
}

// RE7's lights serialize a 4-byte field before Color (v2), and DirectionalLight two more after v29 and one after
// the BakedShadowMap bool; the readers name RE8's fields.
std::size_t LightFieldIndex(const RszInstance& c, std::size_t index) {
    bool directional = c.typeName == "via.render.DirectionalLight";
    if (!directional && c.typeName != "via.render.PointLight" && c.typeName != "via.render.SpotLight") return index;
    if (index < 2 || FieldSize(c, 2) != 4 || FieldSize(c, 3) != 16) return index;
    if (!directional || index <= 29) return index + 1;
    return index <= 31 ? index + 3 : index + 4;
}

// The type dump names some via.render.SpotLight fields wrongly (Intensity, RenderOutputID...); vN is the Nth field.
const RszValue* FieldValue(const RszInstance& c, const char* field) {
    if (field[0] == 'v') {
        std::size_t index = LightFieldIndex(c, static_cast<std::size_t>(std::atoi(field + 1)));
        return index < c.fields.size() ? &c.fields[index].value : nullptr;
    }
    return c.Field(field);
}

const RszBytes* Bytes(const RszInstance& c, const char* field, std::size_t size) {
    const RszValue* v = FieldValue(c, field);
    const RszBytes* b = v ? v->As<RszBytes>() : nullptr;
    return b && b->size() >= size ? b : nullptr;
}

bool Bool(const RszInstance& c, const char* field) {
    const RszBytes* b = Bytes(c, field, 1);
    return b && (*b)[0] != 0;
}

float Float(const RszInstance& c, const char* field) {
    float f = 0;
    if (const RszBytes* b = Bytes(c, field, 4)) std::memcpy(&f, b->data(), 4);
    return f;
}

uint32_t UInt(const RszInstance& c, const char* field) {
    uint32_t u = 0;
    if (const RszBytes* b = Bytes(c, field, 4)) std::memcpy(&u, b->data(), 4);
    return u;
}

void Floats(const RszInstance& c, const char* field, float* out, std::size_t count) {
    if (const RszBytes* b = Bytes(c, field, count * 4)) std::memcpy(out, b->data(), count * 4);
}

std::string String(const RszInstance& c, const char* field) {
    const RszValue* v = FieldValue(c, field);
    return v ? v->AsString() : std::string();
}

SceneLightCommon ReadCommon(const RszInstance& c) {
    SceneLightCommon light;
    light.enabled = Bool(c, "v0");
    light.intensity = Float(c, "v1");
    Floats(c, "v2", light.color, 3);
    light.blackBodyRadiation = Bool(c, "v3");
    light.temperature = Float(c, "v4");
    light.bounceIntensity = Float(c, "v5");
    light.minRoughness = Float(c, "v6");
    light.aoEfficiency = Float(c, "v7");
    light.importantLevel = UInt(c, "v8");
    light.lightingTarget = UInt(c, "v9");
    light.lightBakeOption = UInt(c, "v10");
    light.usingSameIntensity = Bool(c, "v11");
    light.volumetricScatteringIntensity = Float(c, "v12");
    return light;
}

}

float ScenePunctualLight::EffectiveRange() const {
    if (illuminanceThreshold <= 0) return 0;
    float ratio = common.intensity / illuminanceThreshold;
    return ratio > 1 ? (std::sqrt(ratio) - 1) * radius : 0;
}

std::optional<SceneDirectionalLight> ReadDirectionalLight(const RszInstance& c) {
    if (c.typeName != "via.render.DirectionalLight") return std::nullopt;
    SceneDirectionalLight light;
    light.common = ReadCommon(c);
    Floats(c, "v13", light.direction, 3);
    // RE8 layout: v35 is the 80-byte ShadowMapBoundary OBB (RE7 serializes other fields there).
    light.shadowEnable = Bool(c, "v14") && Bytes(c, "v35", 80) != nullptr;
    light.shadowDistance = Float(c, "v16");
    light.shadowBias = Float(c, "v19");
    light.shadowVariance = Float(c, "v18");
    light.shadowDepthBias = Float(c, "v21");
    light.shadowSlopeBias = Float(c, "v22");
    light.shadowMinimumAreaSize = Float(c, "v23");
    Floats(c, "v27", light.partition, 4);
    light.minimumFov = Float(c, "v29");
    Floats(c, "v35", light.shadowBoundary, 20);
    light.bakedShadowMap = String(c, "v30");
    light.bakedShadowBias = Float(c, "v32");
    return light;
}

// Point: v13 Unit, v14 Radius, v15 ReferenceEffectiveRange, v16 IlluminanceThreshold,
// v17 ShadowEnable. Spot: the same, then v17 Cone, v18 Spread, v19 Falloff, v20 ShadowEnable.
std::optional<ScenePunctualLight> ReadPunctualLight(const RszInstance& c) {
    bool spot = c.typeName == "via.render.SpotLight";
    if (!spot && c.typeName != "via.render.PointLight") return std::nullopt;
    ScenePunctualLight light;
    light.common = ReadCommon(c);
    light.spot = spot;
    light.unit = static_cast<LightUnit>(UInt(c, "v13"));
    light.radius = Float(c, "v14");
    light.referenceEffectiveRange = Float(c, "v15");
    light.illuminanceThreshold = Float(c, "v16");
    if (spot) {
        light.cone = Float(c, "v17");
        light.spread = Float(c, "v18");
        light.falloff = Float(c, "v19");
        light.shadowEnable = Bool(c, "v20");
    } else {
        light.shadowEnable = Bool(c, "v17");
    }
    return light;
}

// v0 Enabled, v1 IBLTextureResource, v2 Exposure, v3 Rotation, v5 TrimmingFor2DIBL,
// v6 VirtualOffset, v7 AddBlendIBLTextureResource, v8 AddBlendIntensity.
std::optional<SceneIbl> ReadIbl(const RszInstance& c) {
    if (c.typeName != "via.render.IBL") return std::nullopt;
    SceneIbl ibl;
    ibl.enabled = Bool(c, "v0");
    ibl.texture = String(c, "v1");
    ibl.exposure = Float(c, "v2");
    ibl.rotation = Float(c, "v3");
    Floats(c, "v5", ibl.trim, 4);
    ibl.virtualOffset = Float(c, "v6");
    ibl.addBlendTexture = String(c, "v7");
    ibl.addBlendIntensity = Float(c, "v8");
    return ibl;
}

// v0 Enabled, v2 ProbeResource, v3 NetworkResource.
std::optional<SceneLightProbes> ReadLightProbes(const RszInstance& c) {
    if (c.typeName != "via.render.LightProbes") return std::nullopt;
    SceneLightProbes probes;
    probes.enabled = Bool(c, "v0");
    probes.probes = String(c, "v2");
    probes.network = String(c, "v3");
    probes.priority = UInt(c, "v4");
    probes.intensity = Float(c, "v5");
    probes.saturate = Float(c, "v6");
    Floats(c, "v7", probes.probeColor, 3);
    Floats(c, "v8", probes.ambientColor, 3);
    probes.ambientColorIntensity = Float(c, "v9");
    Floats(c, "v14", probes.obb, 20);
    return probes;
}

// v0 Enabled, v7 AORadius (renderCACAO's radius is 0.05 * AORadius, 1.25 in the RE8 capture).
bool ReadSsaoControl(const RszInstance& c, SceneSsao& ssao) {
    if (c.typeName != "via.render.SSAOControl") return false;
    ssao.controlFound = true;
    ssao.controlEnabled = Bool(c, "v0");
    ssao.radius = Float(c, "v7");
    return true;
}

bool ReadMainCamera(const RszInstance& c, SceneMainCamera& camera) {
    if (c.typeName != "via.Camera") return false;
    camera.found = true;
    camera.fov = Float(c, "v0");
    camera.nearPlane = Float(c, "v1");
    camera.farPlane = Float(c, "v2");
    return true;
}

// v0 Enabled, v1 Cubemap, v2 BlendWeight, v5 Distant, v6 OBBBoundary, v7 OBB.
std::optional<SceneLocalCubemap> ReadLocalCubemap(const RszInstance& c) {
    if (c.typeName != "via.render.LocalCubemap") return std::nullopt;
    SceneLocalCubemap cubemap;
    cubemap.enabled = Bool(c, "v0");
    cubemap.texture = String(c, "v1");
    cubemap.blendWeight = Float(c, "v2");
    cubemap.distant = Bool(c, "v5");
    cubemap.obbBoundary = Bool(c, "v6");
    Floats(c, "v7", cubemap.obb, 20);
    return cubemap;
}

// v0 Enabled, v1 InscatteringColor, v2 Intensity, v3 Density, v4 HeightFalloff, v5 MaxOpacity, v6 StartDistance,
// v7 StartHeightDistance, v8 BlendEnabled, v9 BlendInscatteringColor, v10 BlendIntensity, v11 BlendEndDistance,
// v12 BlendRange, v13 SeparateSky, v14 SkyStencilValue, v15 SkyMaxOpacity, v16 SkyHeightFalloff,
// v17 SkyStartHeightDistance, v18 FogMaskTextureEnabled, v19 FogMaskTexture, v20 FMTInscatteringColor,
// v21 FMTIntensity, v22 FMTDensity, v23 FMTHeightFalloff, v24 FMTMaxOpacity, v25 FMTStartDistance,
// v26 FMTMaxDistance, v27 FMTStartHeightDistance, v28 MaskDistanceFalloff, v29..v33 the mask motion,
// v34 FSBlendRate, v35 FSRayleighColor, v36 FSRayleighCoefficient, v37 FSMieColor, v38 FSMieCoefficient,
// v39 FSSunPosGameObject, v40 FSSunPos, v41 FSSunMaskDetectSize, v42 FSSunMaskDistance, v43 FSAsymmetryFactor.
bool ReadFogComponent(const RszInstance& c, SceneFog& fog) {
    if (c.typeName != "via.render.Fog") return false;
    fog.found = true;
    fog.enabled = Bool(c, "v0");
    Floats(c, "v1", fog.color, 3);
    fog.intensity = Float(c, "v2");
    fog.density = Float(c, "v3");
    fog.heightFalloff = Float(c, "v4");
    fog.maxOpacity = Float(c, "v5");
    fog.startDistance = Float(c, "v6");
    fog.startHeight = Float(c, "v7");
    fog.blendEnabled = Bool(c, "v8");
    Floats(c, "v9", fog.blendColor, 3);
    fog.blendIntensity = Float(c, "v10");
    fog.blendEndDistance = Float(c, "v11");
    fog.blendRange = Float(c, "v12");
    fog.separateSky = Bool(c, "v13");
    fog.skyMaxOpacity = Float(c, "v15");
    fog.skyHeightFalloff = Float(c, "v16");
    fog.skyStartHeight = Float(c, "v17");
    fog.maskTextureEnabled = Bool(c, "v18");
    fog.maskTexture = String(c, "v19");
    Floats(c, "v20", fog.fmtColor, 3);
    fog.fmtIntensity = Float(c, "v21");
    fog.fmtDensity = Float(c, "v22");
    fog.fmtHeightFalloff = Float(c, "v23");
    fog.fmtMaxOpacity = Float(c, "v24");
    fog.fmtStartDistance = Float(c, "v25");
    fog.fmtMaxDistance = Float(c, "v26");
    fog.fmtStartHeight = Float(c, "v27");
    fog.maskDistanceFalloff = Float(c, "v28");
    fog.fsBlendRate = Float(c, "v34");
    Floats(c, "v35", fog.fsRayleighColor, 3);
    fog.fsRayleighCoefficient = Float(c, "v36");
    Floats(c, "v37", fog.fsMieColor, 3);
    fog.fsMieCoefficient = Float(c, "v38");
    fog.fsSunMaskDetectSize = Float(c, "v41");
    fog.fsAsymmetryFactor = Float(c, "v43");
    return true;
}

bool ReadToneMapping(const RszInstance& c, SceneToneMapping& toneMapping) {
    if (c.typeName != "via.render.ToneMapping") return false;
    toneMapping.found = true;
    toneMapping.enabled = Bool(c, "v0");
    toneMapping.vignetting = UInt(c, "v9");
    toneMapping.kerareBegin = Float(c, "v11");
    toneMapping.kerareEnd = Float(c, "v17");
    toneMapping.vignettingBrightness = Float(c, "v18");
    toneMapping.temporalAA = UInt(c, "v23");
    toneMapping.sharpness = Float(c, "v28");
    toneMapping.meteringTexture = String(c, "v19");
    toneMapping.tonemapRange = Float(c, "v5");
    toneMapping.preTonemapRange = Float(c, "v6");
    toneMapping.temporalAAAlgorithm = UInt(c, "v22");
    toneMapping.neighborhoodClamp = Bool(c, "v24");
    toneMapping.subPixel = Float(c, "v25");
    toneMapping.jitterScale = Float(c, "v27");
    toneMapping.responsiveAARate = Float(c, "v29");
    return true;
}

bool ReadSoftBloom(const RszInstance& c, SceneSoftBloom& bloom) {
    if (c.typeName != "via.render.SoftBloom") return false;
    bloom.found = true;
    bloom.enabled = Bool(c, "v0");
    bloom.reductionLevel = UInt(c, "v1");
    bloom.threshold = Float(c, "v2");
    bloom.dispersion = Float(c, "v3");
    bloom.outputRatio = Float(c, "v4");
    bloom.algorithm = UInt(c, "v5");
    bloom.lwMode = Bool(c, "v6");
    bloom.useBlurColor = Bool(c, "v7");
    bloom.blurColor = UInt(c, "v8");
    bloom.highPrecision = Bool(c, "v9");
    bloom.dirtMask = String(c, "v10");
    bloom.dirtMaskThreshold = Float(c, "v11");
    bloom.dirtMaskTintColor = UInt(c, "v12");
    bloom.dirtMaskIntensity = Float(c, "v13");
    bloom.sizeRate = Float(c, "v14");
    bloom.sizeScale.clear();
    const RszValue* scale = FieldValue(c, "v15");
    if (const RszArray* array = scale ? scale->As<RszArray>() : nullptr) {
        for (const RszValue& element : *array) {
            const RszBytes* b = element.As<RszBytes>();
            float f = 0;
            if (b && b->size() >= 4) std::memcpy(&f, b->data(), 4);
            bloom.sizeScale.push_back(f);
        }
    }
    return true;
}

// v0 Enabled, v1 VolumeType, v2 OBB, v5 Color, v6 Density, v7 Eccentricity, v8 DensityAttenuationByHeight,
// v9 ReferenceAltitude.
std::optional<SceneVolumetricFog> ReadVolumetricFog(const RszInstance& c) {
    if (c.typeName != "via.render.VolumetricFog") return std::nullopt;
    SceneVolumetricFog fog;
    fog.enabled = Bool(c, "v0");
    fog.type = UInt(c, "v1");
    Floats(c, "v2", fog.obb, 20);
    fog.color = UInt(c, "v5");
    fog.density = Float(c, "v6");
    fog.eccentricity = Float(c, "v7");
    fog.attenuationByHeight = Float(c, "v8");
    Floats(c, "v9", fog.referenceAltitude, 3);
    return fog;
}

// v0 Enabled, v1 TextureSize, v2 ShadowEnabled, v3 ShadowQuality, v4 AmbientLightEnabled, v5 FogCullingDistance,
// v6 DepthDecodingParam, v7 VolumetricFogSoftness, v8 PrevFrameBlendFactor, v9 Rejection, v10 RejectSensitivity,
// v11 RejectSensitivityFactor, v12 LeakBias, v13 IntegrationType, v14 JitterNoise.
bool ReadVolumetricFogControl(const RszInstance& c, SceneVolumetricFogControl& control) {
    if (c.typeName != "via.render.VolumetricFogControl") return false;
    control.found = true;
    control.enabled = Bool(c, "v0");
    control.textureSize = UInt(c, "v1");
    control.shadowEnabled = Bool(c, "v2");
    control.shadowQuality = UInt(c, "v3");
    control.ambientLightEnabled = Bool(c, "v4");
    control.cullingDistance = Float(c, "v5");
    control.depthDecodingParam = Float(c, "v6");
    control.softness = Float(c, "v7");
    control.prevFrameBlendFactor = Float(c, "v8");
    control.rejection = Bool(c, "v9");
    control.rejectSensitivity = Float(c, "v10");
    control.rejectSensitivityFactor = Float(c, "v11");
    control.leakBias = Float(c, "v12");
    control.integrationType = UInt(c, "v13");
    control.jitterNoise = UInt(c, "v14");
    return true;
}
