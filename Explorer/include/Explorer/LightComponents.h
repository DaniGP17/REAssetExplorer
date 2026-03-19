#ifndef REASSETEXPLORER_LIGHTCOMPONENTS_H
#define REASSETEXPLORER_LIGHTCOMPONENTS_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Rsz/RszTypes.h"

// RE8 via.render light components. Their RSZ fields are unnamed (v0..); the names follow the
// native reflection registration order in re8.exe, which the serialized order keeps.

// via.render.Light, the base of every light: v0..v12.
struct SceneLightCommon {
    bool enabled = false;
    float intensity = 0;
    float color[3] = { 1, 1, 1 };
    bool blackBodyRadiation = false;
    float temperature = 6500;
    float bounceIntensity = 1;
    float minRoughness = 0;
    float aoEfficiency = 0;
    uint32_t importantLevel = 0;
    uint32_t lightingTarget = 0;
    uint32_t lightBakeOption = 0;
    bool usingSameIntensity = false;  // the volumetric scattering takes the light's intensity
    float volumetricScatteringIntensity = 0;
};

struct SceneDirectionalLight {
    SceneLightCommon common;
    float direction[3] = { 0, 1, 0 };
    bool shadowEnable = false;
    float shadowDistance = 0;
    float shadowBias = 0;
    float shadowDepthBias = 0;
    float shadowSlopeBias = 0;
    float shadowVariance = 0;
    float shadowMinimumAreaSize = 0;
    float partition[4] = {};
    float minimumFov = 0;
    float shadowBoundary[20] = {};  // OBB: rows, position, extent
    std::string bakedShadowMap;  // .sst (SparseShadowTree)
    float bakedShadowBias = 0;
};

enum class LightUnit : uint32_t { Lumen = 0, Candela = 1 };

// via.render.PointLight and via.render.SpotLight.
struct ScenePunctualLight {
    SceneLightCommon common;
    bool spot = false;
    LightUnit unit = LightUnit::Lumen;
    float radius = 0;  // of the emitter
    float referenceEffectiveRange = 0;
    float illuminanceThreshold = 0;
    float cone = 0;
    float spread = 0;
    float falloff = 0;
    bool shadowEnable = false;

    // The bounding radius PointLight::update gives the light.
    float EffectiveRange() const;
};

struct SceneIbl {
    bool enabled = false;
    std::string texture;
    float exposure = 0;
    float rotation = 0;  // degrees
    float trim[4] = { 0, 0, 1, 1 };  // l, t, r, b of the 2D texture
    float virtualOffset = 0;  // sky sphere (radius 100) center below the camera
    std::string addBlendTexture;
    float addBlendIntensity = 0;
};

// app.ToneMapAnimationParam of a post-process zone.
struct SceneToneMap {
    bool found = false;
    float ev = 0;
    float contrast = 1;
    float linearBegin = 0.22f;
    float linearLength = 0.4f;
    float toe = 1;
    bool autoExposure = false;  // app.ChangeToneMapOneTimeParam._AutoExposure: 0 enables it
    float minWhitePoint = 1;
    float maxWhitePoint = 1;
    float whiteRange = 0.8f;
    float brightAdaptationRate = 1;
    float darkAdaptationRate = 1;
    float vignettingBrightness = 0;
    float animationTime = 0;  // seconds a zone change takes to reach these values
};

// via.render.ToneMapping, the fields the LDR passes read: v9 Vignetting (0 Enable, 1 KerarePlus, 2 Disable),
// v11 KerareBeginAngle, v17 KerareEndAngle, v18 VignettingBrightness, v23 TemporalAA, v28 Sharpness.
struct SceneToneMapping {
    bool found = false;
    bool enabled = false;
    uint32_t vignetting = 2;
    float kerareBegin = 0;
    float kerareEnd = 0;
    float vignettingBrightness = 0;
    uint32_t temporalAA = 0;
    float sharpness = 0;
    std::string meteringTexture;
    float tonemapRange = 0;
    float preTonemapRange = 0;
    uint32_t temporalAAAlgorithm = 0;
    bool neighborhoodClamp = false;
    float subPixel = 0;
    float jitterScale = 0;
    float responsiveAARate = 0;
};

// via.render.SoftBloom: v0 Enabled, v1 ReductionLevel, v2 Threshold, v3 Dispersion, v4 OutputRatio, v5 Algorithm,
// v6 LWMode, v7 UseUserDefinedBlurColor, v8 BlurColor, v9 IsHighPrecision, v10 DirtMask, v11 DirtMaskThreshold,
// v12 DirtMaskTintColor, v13 DirtMaskIntensity, v14 SizeRate, v15 SizeScale.
struct SceneSoftBloom {
    bool found = false;
    bool enabled = false;
    uint32_t reductionLevel = 0;
    float threshold = 0;
    float dispersion = 0;
    float outputRatio = 0;
    uint32_t algorithm = 0;
    bool lwMode = false;
    bool useBlurColor = false;
    uint32_t blurColor = 0xFFFFFFFF;
    bool highPrecision = false;
    std::string dirtMask;
    float dirtMaskThreshold = 0;
    uint32_t dirtMaskTintColor = 0xFFFFFFFF;
    float dirtMaskIntensity = 0;
    float sizeRate = 1;
    std::vector<float> sizeScale;
};

// app.PostSoftBloomUserData of a post-process zone: app.SoftBloomController sets these on SoftBloom.
struct SceneSoftBloomZone {
    bool found = false;
    bool enabled = false;
    uint32_t reductionLevel = 0;
    float threshold = 0;
    float dispersion = 0;
    float outputRatio = 0;
    bool useBlurColor = false;
    uint32_t blurColor = 0xFFFFFFFF;
};

// via.render.LDRColorCorrect (a child of LDRPostProcess): v0 Enabled, v1 ColorCubeBlendRate, v2 ColorCube0,
// v3 ColorCube1, v4 ColorCube2BlendRate, v5 ColorCube2, v6 ColorCubeElement textures, v7 BlendRate,
// v8 BlendTargetIndex.
struct SceneColorCorrect {
    bool found = false;
    bool enabled = false;
    float cubeBlendRate = 0;
    std::string cube0;
    std::string cube1;
    float cube2BlendRate = 0;
    std::string cube2;
    std::vector<std::string> elements;
    float blendRate = 0;
    uint32_t blendTargetIndex = 0;
};

// app.PostColorCorrectUserData of a post-process zone: app.ColorCorrectController blends LDRColorCorrect to
// ColorCubeElement[blendTargetIndex] over animationTime.
struct SceneColorCorrectZone {
    bool found = false;
    uint32_t blendTargetIndex = 0;
    float animationTime = 0;
};

// via.render.Fog. The post-process zone's app.PostFogUserData replaces enabled through startHeight
// (app.FogController::applyData).
struct SceneFog {
    bool found = false;
    bool enabled = false;
    float color[3] = {};
    float intensity = 0;
    float density = 0;
    float heightFalloff = 0;
    float maxOpacity = 0;
    float startDistance = 0;
    float startHeight = 0;
    bool blendEnabled = false;
    float blendColor[3] = {};
    float blendIntensity = 0;
    float blendEndDistance = 0;
    float blendRange = 0;
    bool separateSky = false;
    float skyMaxOpacity = 0;
    float skyHeightFalloff = 0;
    float skyStartHeight = 0;
    bool maskTextureEnabled = false;
    std::string maskTexture;
    float fmtColor[3] = {};
    float fmtIntensity = 0;
    float fmtDensity = 0;
    float fmtHeightFalloff = 0;
    float fmtMaxOpacity = 0;
    float fmtStartDistance = 0;
    float fmtMaxDistance = 0;
    float fmtStartHeight = 0;
    float maskDistanceFalloff = 0;
    float fsBlendRate = 0;
    float fsRayleighColor[3] = {};
    float fsRayleighCoefficient = 0;
    float fsMieColor[3] = {};
    float fsMieCoefficient = 0;
    float fsSunMaskDetectSize = 0;
    float fsAsymmetryFactor = 0;
    float animationTime = 0;  // zone user data only
};

// via.render.VolumetricFog: type 0 is the global medium, 1 a box the GameObject's transform places.
struct SceneVolumetricFog {
    bool enabled = false;
    uint32_t type = 0;
    uint32_t color = 0xFFFFFFFF;  // via.Color
    float density = 0;
    float eccentricity = 0;
    float attenuationByHeight = 0;
    float referenceAltitude[3] = {};
    float obb[20] = {};  // serialized: rows, position, extent
    float world[16] = {};
};

// via.render.VolumetricFogControl.
struct SceneVolumetricFogControl {
    bool found = false;
    bool enabled = false;
    uint32_t textureSize = 0;  // 0 W160xH90xD64, 1 W160xH90xD128
    bool shadowEnabled = false;
    uint32_t shadowQuality = 0;  // 1 High
    bool ambientLightEnabled = false;
    float cullingDistance = 0;
    float depthDecodingParam = 0;
    float softness = 0;  // serialized 20 times the engine's
    float prevFrameBlendFactor = 0;
    bool rejection = false;
    float rejectSensitivity = 0;
    float rejectSensitivityFactor = 0;
    float leakBias = 0;
    uint32_t integrationType = 0;  // 1 Accurate
    uint32_t jitterNoise = 0;
};

// app.PostVolumetricFogUserData of a post-process zone, which app.VolumetricFogControllApp applies to the
// MainCamera's global VolumetricFog and VolumetricFogControl.
struct SceneVolumetricFogZone {
    bool found = false;
    bool enabled = false;
    uint32_t color = 0xFFFFFFFF;
    float density = 0;
    float scatteringDistribution = 0;
    float attenuationByHeight = 0;
    float cullingDistance = 0;
    float depthDecodingParam = 0;
    bool rejection = false;
    float rejectSensitivity = 0;
    bool controlFound = false;
    float leakBias = 0;
    float animationTime = 0;
};

struct SceneLocalCubemap {
    bool enabled = false;
    std::string texture;
    float blendWeight = 1;
    bool distant = false;
    bool obbBoundary = false;  // false: the OBB's bounding AABB bounds the parallax
    float obb[20] = {};  // world matrix (row-major 4x4), then extent
    float position[3] = {};  // the GameObject's
};

// via.render.LightProbes: a probe network (.prb) and its baked values (.lprb).
// via.render.LightProbes: UpdateLightProbeOBB writes its probe values inside obb into the network's pool.
struct SceneLightProbes {
    bool enabled = false;
    std::string probes;
    std::string network;
    uint32_t priority = 0;
    float intensity = 1;
    float saturate = 1;
    float probeColor[3] = { 1, 1, 1 };
    float ambientColor[3] = {};
    float ambientColorIntensity = 0;
    float obb[20] = {};  // row-major 4x4 (axes, then position), extent xyz, pad
};

// FFX CACAO inputs: the main camera's via.render.SSAOControl and the post-process zone's
// app.SSAOAnimationParam, which app.SSAOController writes into it.
struct SceneSsao {
    bool controlFound = false;
    bool controlEnabled = false;
    float radius = 0;
    bool zoneFound = false;
    bool zoneEnabled = false;
    float intensity = 0;
    float zoneAnimationTime = 0;
};

// The application scene's MainCamera: via.Camera v0 FOV, v1 NearClipPlane, v2 FarClipPlane.
struct SceneMainCamera {
    bool found = false;
    float fov = 0;
    float nearPlane = 0;
    float farPlane = 0;
};

std::optional<SceneDirectionalLight> ReadDirectionalLight(const RszInstance& component);
std::optional<ScenePunctualLight> ReadPunctualLight(const RszInstance& component);
std::optional<SceneIbl> ReadIbl(const RszInstance& component);
std::optional<SceneLocalCubemap> ReadLocalCubemap(const RszInstance& component);
std::optional<SceneLightProbes> ReadLightProbes(const RszInstance& component);
bool ReadSsaoControl(const RszInstance& component, SceneSsao& ssao);
bool ReadMainCamera(const RszInstance& component, SceneMainCamera& camera);
bool ReadFogComponent(const RszInstance& component, SceneFog& fog);
bool ReadToneMapping(const RszInstance& component, SceneToneMapping& toneMapping);
bool ReadSoftBloom(const RszInstance& component, SceneSoftBloom& bloom);
std::optional<SceneVolumetricFog> ReadVolumetricFog(const RszInstance& component);
bool ReadVolumetricFogControl(const RszInstance& component, SceneVolumetricFogControl& control);

#endif
