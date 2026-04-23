#include "Explorer/PostProcessZones.h"

#include <algorithm>

#include "Explorer/Log.h"
#include "Explorer/SceneUpload.h"

namespace {

float Lerp(float a, float b, float t) {
    return (b - a) * t + a;
}

// via.Color channels interpolate as bytes (app.VolumetricFogAnimationParam::leap).
uint32_t LerpColor(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float channel = Lerp(static_cast<float>((a >> shift) & 0xFF), static_cast<float>((b >> shift) & 0xFF), t);
        out |= (static_cast<uint32_t>(static_cast<int>(channel)) & 0xFF) << shift;
    }
    return out;
}

// app.ToneMapAnimationParam::lerp; the one-time data switches at once.
SceneToneMap Lerp(const SceneToneMap& a, const SceneToneMap& b, float t) {
    SceneToneMap out = b;
    out.ev = Lerp(a.ev, b.ev, t);
    out.brightAdaptationRate = Lerp(a.brightAdaptationRate, b.brightAdaptationRate, t);
    out.darkAdaptationRate = Lerp(a.darkAdaptationRate, b.darkAdaptationRate, t);
    out.linearBegin = Lerp(a.linearBegin, b.linearBegin, t);
    out.linearLength = Lerp(a.linearLength, b.linearLength, t);
    out.toe = Lerp(a.toe, b.toe, t);
    out.contrast = Lerp(a.contrast, b.contrast, t);
    out.vignettingBrightness = Lerp(a.vignettingBrightness, b.vignettingBrightness, t);
    return out;
}

SceneFog Lerp(const SceneFog& a, const SceneFog& b, float t) {
    SceneFog out = b;
    for (int c = 0; c < 3; c++) out.color[c] = Lerp(a.color[c], b.color[c], t);
    out.intensity = Lerp(a.intensity, b.intensity, t);
    out.density = Lerp(a.density, b.density, t);
    out.heightFalloff = Lerp(a.heightFalloff, b.heightFalloff, t);
    out.maxOpacity = Lerp(a.maxOpacity, b.maxOpacity, t);
    out.startDistance = Lerp(a.startDistance, b.startDistance, t);
    out.startHeight = Lerp(a.startHeight, b.startHeight, t);
    return out;
}

// leap blends these; Rejection keeps the start's until the animation ends.
SceneVolumetricFogZone Lerp(const SceneVolumetricFogZone& a, const SceneVolumetricFogZone& b, float t) {
    SceneVolumetricFogZone out = b;
    out.color = LerpColor(a.color, b.color, t);
    out.density = Lerp(a.density, b.density, t);
    out.scatteringDistribution = Lerp(a.scatteringDistribution, b.scatteringDistribution, t);
    out.attenuationByHeight = Lerp(a.attenuationByHeight, b.attenuationByHeight, t);
    out.cullingDistance = Lerp(a.cullingDistance, b.cullingDistance, t);
    out.depthDecodingParam = Lerp(a.depthDecodingParam, b.depthDecodingParam, t);
    out.rejectSensitivity = Lerp(a.rejectSensitivity, b.rejectSensitivity, t);
    if (t < 1) out.rejection = a.rejection;
    return out;
}

SceneSsao Lerp(const SceneSsao& a, const SceneSsao& b, float t) {
    SceneSsao out = b;
    out.intensity = Lerp(a.intensity, b.intensity, t);
    return out;
}

template <typename T>
void Begin(T& blend, const auto& target, float duration, bool immediate) {
    blend.from = blend.value;
    blend.to = target;
    blend.timer = 0;
    blend.duration = immediate ? 0.0f : duration;
    blend.active = true;
}

template <typename T>
bool Step(T& blend, float dt) {
    if (!blend.active) return false;
    blend.timer += dt;
    if (blend.duration <= 0 || blend.timer >= blend.duration) {
        blend.value = blend.to;
        blend.active = false;
    } else {
        blend.value = Lerp(blend.from, blend.to, blend.timer / blend.duration);
    }
    return true;
}

}

PostProcessZones::PostProcessZones(const SceneBuild& build)
    : zones(build.postProcessZones),
      inside(build.postProcessZones.size(), 0),
      fogComponent(build.fog),
      volumetricControl(build.volumetricFogControl),
      applicationFog(build.applicationVolumetricFog),
      sceneFogs(build.volumetricFogs),
      ssaoControl(build.ssao),
      toneMapping(build.toneMapping),
      colorCorrect(build.colorCorrect),
      softBloom(build.softBloom),
      softBloomZone(build.softBloomZone) {
    toneMap.value = build.toneMap;
    fog.value = build.fogZone;
    volumetricFog.value = build.volumetricFogZone;
    ssao.value = build.ssao;
    std::string target = ColorCubeTarget(colorCorrect, build.colorCorrectZone);
    cubeFrom = target.empty() ? colorCorrect.cube0 : target;
    cubeTo = target.empty() ? colorCorrect.cube1 : target;
    cubeRate = target.empty() ? colorCorrect.blendRate : 1.0f;
}

const std::string& PostProcessZones::CurrentZone() const {
    static const std::string NONE;
    return applied ? zones[*applied].name : NONE;
}

void PostProcessZones::Start(const ScenePostProcessZone& zone, bool immediate) {
    if (zone.toneMap.found) Begin(toneMap, zone.toneMap, zone.toneMap.animationTime, immediate);
    if (zone.fog.found) Begin(fog, zone.fog, zone.fog.animationTime, immediate);
    if (zone.volumetricFog.found) Begin(volumetricFog, zone.volumetricFog, zone.volumetricFog.animationTime, immediate);
    if (zone.ssao.zoneFound) Begin(ssao, zone.ssao, zone.ssao.zoneAnimationTime, immediate);
    if (zone.softBloom.found) softBloomZone = zone.softBloom;
    std::string target = ColorCubeTarget(colorCorrect, zone.colorCorrect);
    if (!target.empty() && target != cubeTo) {
        cubeFrom = cubeTo;
        cubeTo = target;
        cubeRate = 0;
        cubeTimer = 0;
        cubeDuration = immediate ? 0.0f : zone.colorCorrect.animationTime;
        cubeActive = true;
    }
}

void PostProcessZones::Apply(Viewer& viewer, bool resetExposure) {
    if (toneMap.value.found) ApplyToneMap(viewer, toneMap.value, resetExposure);
    if (toneMapping.found && colorCorrect.found) {
        ApplyPostProcess(viewer, toneMapping, colorCorrect, toneMap.value, cubeFrom, cubeTo, cubeRate);
    }
    if (softBloom.found) ApplySoftBloom(viewer, softBloom, softBloomZone);
    if (fogComponent.found) ApplyFog(viewer, fogComponent, fog.value);
    if (volumetricControl.found) ApplyVolumetricFog(viewer, volumetricControl, applicationFog, sceneFogs, volumetricFog.value);
    if (ssaoControl.controlFound && ssaoControl.controlEnabled) ApplySsao(viewer, ssaoControl, ssao.value);
}

void PostProcessZones::Update(Viewer& viewer, const float point[3], float dt) {
    if (zones.empty()) return;
    for (std::size_t z = 0; z < zones.size(); z++) {
        bool now = std::any_of(zones[z].volumes.begin(), zones[z].volumes.end(),
                               [&](const CollisionVolume& volume) { return VolumeContains(volume, point); });
        if (now && !inside[z]) current[zones[z].priority] = z;
        inside[z] = now ? 1 : 0;
    }
    std::optional<std::size_t> target;
    for (const auto& [priority, zone] : current) {
        if (inside[zone]) {
            target = zone;
            break;
        }
    }
    bool changed = false;
    if (target && target != applied) {
        Start(zones[*target], !started);
        LogInfo("post-process zone: %s (priority %d)", zones[*target].name.c_str(), zones[*target].priority);
        applied = target;
        changed = true;
    }
    changed = Step(toneMap, dt) | changed;
    changed = Step(fog, dt) | changed;
    changed = Step(volumetricFog, dt) | changed;
    changed = Step(ssao, dt) | changed;
    if (cubeActive) {
        cubeTimer += dt;
        cubeRate = cubeDuration > 0 ? std::min(cubeTimer / cubeDuration, 1.0f) : 1.0f;
        cubeActive = cubeRate < 1.0f;
        changed = true;
    }
    if (changed) Apply(viewer, false);
    started = started || applied.has_value();
}
