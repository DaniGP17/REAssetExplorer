#ifndef REASSETEXPLORER_RENDERTYPES_H
#define REASSETEXPLORER_RENDERTYPES_H
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <d3d12.h>

struct ViewerMeshDraw {
    uint32_t indexCount;
    uint32_t startIndex;
    int32_t baseVertex;
    uint32_t materialSlot;
    uint32_t pipelineIndex;
    uint32_t instanceIndex = 0;
    // World-space bounding sphere for culling; radius 0 disables culling.
    float boundsCenter[3]{};
    float boundsRadius = 0;
    // Caller-assigned; draw masks and the selection refer to it.
    uint32_t id = 0;
    bool shadowOnly = false;  // drawn into the shadow maps only
};

struct ViewerInstanceWorld {
    float m[12];
    // The skinning VS reads SkinningMatrices[jointOffset + vertex bone index].
    uint32_t jointOffset = 0;
};

struct ViewerMesh {
    std::span<const uint8_t> positions;
    std::span<const uint8_t> normals;
    std::span<const uint8_t> uv0;
    std::span<const uint8_t> uv1;
    std::span<const uint8_t> weights;  // 16B/vertex: 8 bone indices + 8 unorm8 weights
    std::span<const uint8_t> indices;
    std::vector<ViewerMeshDraw> draws;
    float aabbMin[3];
    float aabbMax[3];
};

struct GameInputElement {
    const char* semanticName;
    UINT semanticIndex;
    DXGI_FORMAT format;
    UINT slot;
    UINT offset;
};

// Pins a (stage, register) to the resource its SDF slot name expects. DXIL only
// exposes registers; guessing binds wind/pivot inputs to unrelated buffers and
// the shader reads out of bounds (GPU page fault, device hang).
enum class GameBindSlotKind : uint8_t {
    Scene,
    GBufferType,
    CheckerBoard,
    ZeroCb,
    Instance,
    BindlessData,
    Redirect,
    ZeroSrv,
    Skinning,       // float3x4 per joint, already in world space
    Tonemap,
    WhitePoint,     // WhitePointCS output: [0] = 1 / L
    Environment,
    ShadowCast,
};

struct GameBindOverride {
    bool pixelStage;      // false = vertex stage
    bool constantBuffer;  // false = buffer SRV
    uint8_t reg;
    GameBindSlotKind kind;
};

struct GameSamplerOverride {
    bool pixelStage;
    uint8_t reg;
    D3D12_FILTER filter;
    D3D12_TEXTURE_ADDRESS_MODE address;
};

struct GamePrepassDesc {
    std::span<const uint8_t> vs;
    std::vector<GameInputElement> inputLayout;
    std::vector<GameBindOverride> bindOverrides;
};

// Alpha-tested masters clip in their AZPrePass program; the GBuffer variant
// then runs with depth func EQUAL.
enum class GameComputeResource : uint8_t {
    Other,
    SceneInfo,
    Environment,
    LightInfo,
    CheckerBoard,
    Depth,
    CullingVolume,
    CullingList,
    AmbientBrdf,
    Normal,
    Occlusion,
    ProbeBspTree,
    ProbeTetrahedra,
    ProbeValues,
    CubemapArray,
    CubemapList,
    Cubemap,
    ProbeIndexCache,
    Gid,
    Gis,
};

// A game compute shader register named by the SDF (compute: byte 5 of slotRaw).
struct GameComputeSlot {
    enum class Kind : uint8_t { Cbv, Srv, Uav, Sampler };
    Kind kind;
    uint8_t reg;
    GameComputeResource resource;
    D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    D3D12_TEXTURE_ADDRESS_MODE address = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    D3D12_COMPARISON_FUNC comparison = D3D12_COMPARISON_FUNC_NEVER;
    std::string name;
};

struct GameDeferredDesc {
    std::span<const uint8_t> vs;
    std::span<const uint8_t> ps;
    std::vector<GameInputElement> inputLayout;
    std::array<uint8_t, 36> blendState{};
    std::array<uint8_t, 8> depthStencilState{};
    std::array<uint8_t, 16> rasterizerState{};
    bool alphaTest = false;
    // A Forward* program: drawn over the lit scene, its slots bound by SDF name.
    bool forward = false;
    std::vector<GameComputeSlot> vsSlots;
    std::vector<GameComputeSlot> psSlots;
    // The master's AZPrePass* for alpha-tested materials; empty for none.
    std::span<const uint8_t> prepassVs;
    std::span<const uint8_t> prepassPs;
    std::vector<GameInputElement> prepassLayout;
    std::array<uint8_t, 16> prepassRasterizerState{};
    std::vector<GameBindOverride> bindOverrides;
    std::vector<GameBindOverride> prepassBindOverrides;
    std::vector<GameSamplerOverride> samplers;
    std::vector<GameSamplerOverride> prepassSamplers;
    // The master's [A][TS]Shadow* program; empty for none.
    std::span<const uint8_t> shadowVs;
    std::span<const uint8_t> shadowPs;
    std::vector<GameInputElement> shadowLayout;
    std::array<uint8_t, 16> shadowRasterizerState{};
    std::vector<GameBindOverride> shadowBindOverrides;
    std::vector<GameSamplerOverride> shadowSamplers;
};

// via.render.DirectionalLight's cascade shadow settings and the camera DirectionalLight::update fits them to.
struct ViewerDirectionalShadow {
    bool enabled = false;
    float distance = 0;
    float minimumAreaSize = 0;
    float minimumFov = 0;  // degrees
    float partition[4] = {};
    float depthBias = 0;
    float slopeBias = 0;
    float bias = 0;
    float variance = 0;
    float aoEfficiency = 0;  // Light::mAOEfficiency, already 1 - AOEfficiency
    float boundary[20] = {};  // ShadowMapBoundary OBB: rows, position, extent
    float cameraNear = 0;
    float cameraFar = 0;
};

// A G-buffer channel drawn instead of the lit image.
enum class GBufferView : uint32_t { None, Normals, Roughness, Metallic, Occlusion, BaseColor, Emissive, Depth, Velocity };

// Passes the viewport's Show menu can turn off.
struct ViewerShowFlags {
    bool fog = true;
    bool volumetricFog = true;
    bool shadows = true;
    bool postProcess = true;
    bool localCubemaps = true;
};

// Views that replace the shading: the scene lit with a grey albedo, or a flat colour per pixel.
enum class ViewerDebugView : uint32_t { None, LightingOnly, LodColoration, Overdraw, LightComplexity };

// PostTonemap3Section with an SDR white of 1, applied after the exposure.
struct ViewerToneMap {
    float exposure = 1;
    float contrast = 1;
    float linearBegin = 0.22f;
    float linearLength = 0.4f;
    float toe = 1;
    // HistogramCS/WhitePointCS: exposure / L, L the whiteRange quantile of the scene luminance
    // within [minWhite, maxWhite], adapted over frames at brightRate/darkRate.
    bool autoExposure = false;
    float minWhite = 1;
    float maxWhite = 1;
    float whiteRange = 0.8f;
    float brightRate = 1;
    float darkRate = 1;
};

// EnvironmentInfo overrides (to reproduce a captured frame); negative time or frame keeps the viewer's
// clock or frame counter. userGlobalParams are set by game scripts (wind).
struct ViewerEnvironment {
    int64_t timeMs = -1;
    int64_t frame = -1;
    float userGlobalParams[32][4] = {};
};

struct ViewerTargetImage {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<float> pixels;
};

// exposure: the factor the resolve applied before the tone map (exposure times white point).
struct ViewerTargetCapture {
    std::vector<ViewerTargetImage> images;
    float exposure = 0;
};

// The FogParam cbuffer of fog.sdf as Fog::update writes it.
struct ViewerFogParam {
    float fogInscatteringColor[3];
    float fogDensity;
    float fogHeightFalloff;
    float fogMaxOpacity;
    float fogStartDistance;
    float fogHeightStartDistance;
    float fogBlendInscatteringColor[3];
    float fogBlendEndDistance;
    float fogBlendInvRange;
    float fogSkyMaxOpacity;
    float fogSkyHeightFalloff;
    float fogSkyHeightStartDistance;
    float fmtInscatteringColor[3];
    float fmtDensity;
    float fmtHeightFalloff;
    float fmtMaxOpacity;
    float fmtStartDistance;
    float fmtHeightStartDistance;
    float maskPlanePos1[3];
    float maskPlaneBlendRate;
    float maskPlanePos2[3];
    float maskDistanceFalloff;
    float fmtMaxDistance;
    float fsSunMaskDetectSize;
    float fsBlendRate;
    float fsRayleigh;
    float fsSunScreenPos[3];
    float fsMie;
    float fsBetaR[3];
    float fsAsymmetryFactor;
    float fsBetaM[3];
    float fsAspectRatio;
    float fogReserved[2];
    float fsFovArFactor;
    float fsRcpSunMaskDistance;
};
static_assert(sizeof(ViewerFogParam) == 208);

// A via.render.VolumetricFog medium: type 0 fills the view, type 1 is the unit box the GameObject's world
// matrix (row-major, translation in the last row) scales and places.
struct ViewerVolumetricFog {
    uint32_t type = 0;
    uint32_t albedo = 0xFFFFFFFF;  // via.Color
    float density = 0;
    float eccentricity = 0;
    float attenuationByHeight = 0;
    float referenceAltitude = 0;
    float world[16] = {};
};

// via.render.VolumetricFogControl after the application scripts.
struct ViewerVolumetricFogControl {
    bool enabled = false;
    uint32_t depthSlices = 64;
    float cullingDistance = 1000;
    float depthDecodingParam = 0.5f;
    float softness = 0.05f;
    float blendFactor = 0.95f;
    bool rejection = false;
    float rejectSensitivity = 0.5f;
    float rejectSensitivityFactor = 1;
    float leakBias = 0;
    uint32_t jitterNoise = 0;
};


// via.render.IBL as CubemapFarPlane2D draws it: u = atan2(z, x) / 2pi + 0.5 - rotation.
struct ViewerSceneIbl {
    float exposure = 1;
    float rotation = 0;  // radians
    float addBlend = 0;
    float virtualOffset = 0;
    float trimScale[2] = { 1, 1 };
    float trimOffset[2] = { 0, 0 };
    float irradianceSh[9][4] = {};  // cosine-convolved SH9 of exposure * sky + addBlend * add, xyz
};

enum class GameLightingResource : uint8_t {
    BlueNoise,
    Depth,
    LightParams,
    ShadowParams,
    CullingVolume,
    CullingList,
    AmbientBrdf,
    BaseColor,
    Normal,
    Occlusion,
    StaticShadow,
    Shadow,
    Ies,
    Gid,
    Gis,
    CompareSampler,
    ShadowRotation,
    Other,
};

// A DeferredLight PS register named by the SDF (RE8 adds BlueNoise16 at t0, shifting RE7's).
struct GameLightingSlot {
    bool sampler;
    uint8_t reg;
    GameLightingResource resource;
    bool constantBuffer = false;
};

struct GameComputeDesc {
    std::span<const uint8_t> cs;
    std::vector<GameComputeSlot> slots;
};

// The via.render.Fog pass: the fog.sdf program Fog::draw picks, over the lit scene before transparents.
struct GameFogDesc {
    std::span<const uint8_t> vs;
    GameComputeDesc ps;  // slots by PS register
};

// volumetricRendering.sdf's InjectShadedVolumeData and IntegrateFroxelContribution variants
// VolumetricFogControl::updateShaderResource names, and the blue noise tables it loads.
// The LDR tail of the frame, each stage's DXIL with its SDF slots: advancedsystem.sdf LwLDRPostProcess* (tone map,
// color cubes, vignette) and FXAA, tonemap.sdf CAS, screenoutput.sdf ScreenOutput*.
struct GamePostProcessDesc {
    GameComputeDesc histogram;
    GameComputeDesc whitePoint;
    GameComputeDesc ldrVs;
    GameComputeDesc ldrPs;
    GameComputeDesc fxaaVs;
    GameComputeDesc fxaaPs;
    GameComputeDesc cas;
    GameComputeDesc outputVs;
    GameComputeDesc outputPs;
    // SoftBloomImplement::drawUnUsedThreasholdBloom (StandardV3): the level i blend is NewBlendingTier<i>.
    GameComputeDesc bloomVs;
    GameComputeDesc bloomReduction;
    GameComputeDesc bloomFilter;
    GameComputeDesc bloomBlending[7];
    GameComputeDesc bloomFinal;
    // ToneMappingImplement::preUpdate's TemporalAA pass (TonemapFullScreenTriangleVS and a *PreTonemap program).
    GameComputeDesc temporalVs;
    GameComputeDesc temporalPs;
};

// ToneMapping's TemporalAA settings, as ToneMappingImplement::preUpdate writes them to the Tonemap cbuffer.
struct ViewerTemporalAA {
    bool enabled = false;
    float blend = 0.916667f;  // AABlend once enough frames are accumulated
    float sharpness = 0.333f;
    float tonemapRange = 0.1f;
    float preTonemapRange = 1.0f;
    float subPixel = 1.0f;
    float responsiveRate = 0.5f;
    float jitterScale = 1.0f;
};

// SoftBloomImplement::render's cbuffers.
struct ViewerSoftBloomParams {
    uint32_t levels = 0;       // reduction buffers, up to 7; 0 turns SoftBloom off
    float reduction[12] = {};  // cbSoftBloom of NewReduction
    float output[12] = {};     // cbSoftBloom of NewFinal
    float cone[4] = {};        // cbCone[1]
    float scale[8] = {};       // cbSoftBloomScale
};

// The cbuffers of those passes as the engine fills them.
struct ViewerPostProcessParams {
    float tonemapParam[12] = {};           // TonemapParam (ToneMappingImplement::preUpdate)
    float cameraKerare[4] = {};            // CameraKerare
    float colorCorrect[20] = {};           // ColorCorrectTexture: size, blend rate, cube 2 rate, 1 / size, color matrix
    float outputColorAdjustment[16] = {};  // OutputColorAdjustment (DisplaySettings::update)
    uint32_t cas[8] = {};                  // cbCAS (ToneMappingImplement::executeCASFilter)
    uint32_t colorCubes[3] = {};           // tTextureMap0..2: indices into LoadColorCubes' list
};

struct GameVolumetricFogDesc {
    GameComputeDesc inject;
    GameComputeDesc integrate;
    uint32_t integrateGroup = 4;  // threads per axis of the integration variant
    std::span<const uint8_t> sobol;
    std::span<const uint8_t> scrambling;
    std::span<const uint8_t> ranking;
};

// FfxCacaoSettings as CACAOImplement feeds them to updateConstants.
struct GameCacaoSettings {
    float radius;
    float shadowMultiplier;
    float shadowPower;
    float shadowClamp;
    float horizonAngleThreshold;
    float fadeOutFrom;
    float fadeOutTo;
    float adaptiveQualityLimit;
    float sharpness;
    float detailShadowStrength;
    float bilateralSigmaSquared;
    float bilateralSimilarityDistanceSigma;
    uint32_t blurPassCount;
};

// The ffx_cacao.sdf programs of the downsampled, highest quality path the RE8 capture runs.
struct GameCacaoDesc {
    GameComputeDesc prepareDepths;
    GameComputeDesc prepareNormals;
    GameComputeDesc generateBase;
    GameComputeDesc generate;
    GameComputeDesc importanceMap;
    GameComputeDesc importanceA;
    GameComputeDesc importanceB;
    GameComputeDesc blur;
    GameComputeDesc upscale;
    GameCacaoSettings settings;
};

struct GameLightingDesc {
    std::span<const uint8_t> vs;
    std::span<const uint8_t> ps;
    uint32_t gidRegister = 13;
    uint32_t renderTargetCount = 1;
    std::vector<GameLightingSlot> slots;
    std::span<const uint8_t> blendState;  // SdfProgram::blendState; empty for the default
};

// Mirrors struct.LightParameter in lighting.sdf (80B stride).
struct GameLightParam {
    float position[3];
    float boundingRadius;
    float direction[3];
    float falloff;
    float attenuation[4];
    float color[3];
    float tolerance;
    uint32_t shadowIndex;
    uint32_t iesId;
    uint32_t reserved[2];
};

struct GameTextureMip {
    std::span<const uint8_t> data;
    uint32_t pitch;
};

struct GameTextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t arraySize = 1;
    std::vector<GameTextureMip> mips;
    uint32_t depth = 1;  // > 1: a volume, each mip holding every slice
};

struct GameMaterialDesc {
    std::vector<uint8_t> paramBlock;
    std::vector<uint32_t> textureIndices;
};

enum class ParticleBlend : uint32_t {
    AlphaBlend = 0,
    Physical = 1,
    Additive = 2
};

constexpr uint32_t PARTICLE_AXIS_ALIGNED = 0x10;
// Physical blend keeps its alpha (darkens what is behind) only with this; otherwise it adds.
constexpr uint32_t PARTICLE_PHYSICAL_ALPHA = 0x20;
constexpr uint32_t PARTICLE_ALPHA_GAMMA = 0x40;

// Matches the particle shader's StructuredBuffer element (96B).
struct ViewerParticle {
    float position[3];
    float rotation;          // roll around the view axis, radians
    float size[2];           // full width/height; axis-aligned quads use only the width
    uint32_t textureIndex;   // bindless index (SRV_BINDLESS_BASE + i)
    uint32_t flags;          // ParticleBlend in bits 0-3, PARTICLE_* bits
    float color[4];          // linear rgb, a = color alpha * life fade
    float uvRect[4];         // left, top, right, bottom
    float alphaRate;         // alpha = a^alphaRate * a * texture alpha
    float emissiveRate;
    float softDistance;      // 0 = hard depth test
    float evPow2;            // Physical blend: scales the emissive term
    float axis[3];           // PARTICLE_AXIS_ALIGNED: the quad spans position (v = uvRect.y) to position + axis (v = uvRect.w), facing the camera
    float detonemapRate;     // 0 = off
    uint32_t lightIndex = UINT32_MAX;  // its ViewerParticleLight, whose diffuse result scales the color
    float pad[3]{};
};

// A master material's particle program (Ribbon, Polygon...) run as is: DXIL stages with their SDF slots.
struct ViewerMaterialProgram {
    std::vector<uint8_t> vs;
    std::vector<uint8_t> ps;
    std::vector<GameComputeSlot> vsSlots;
    std::vector<GameComputeSlot> psSlots;
    std::array<uint8_t, 36> blend{};
    std::array<uint8_t, 8> depth{};
    std::array<uint8_t, 16> raster{};
};

// The primitive vertex those programs read. The other inputs they declare read zeros.
struct ViewerMaterialVertex {
    float position[3];
    uint8_t color[4];
    float uv[2];         // PrimitiveMaterialTex
    float generic0[4];   // ribbon: x = signed half width, y = +1 base / -1 tip; polygon: xy = signed half extents
    float generic5[4];   // ribbon: tangent; polygon: rotation quaternion
    float generic6[4];   // ribbon: pivot - emitter position; polygon: world scale
    uint32_t generic2[4];  // z: lit points per side
    uint32_t generic4[4];  // x: first ViewerParticleLight, z: ribbon segment
    uint32_t zero[4];
};

// The game's ParticleLighting record: one lit point that PreCalculateLighting turns into a diffuse color.
struct ViewerParticleLight {
    float position[3];
    uint32_t segment;  // points per side | shape << 8 (0 camera facing, 2 polygon) | pmcFlags << 16
    float size[2];     // polygon half extents
    float offset[2];
    float rotation[4];  // polygon quaternion
    float coord[2];
    uint32_t attributes;  // polygon point: row | column << 8
    float lightShadowRatio;
    float reserved[2];
    float backfaceLightRatio;
    float directionalLightShadowRatio;
};

struct ViewerMaterialBatch {
    uint32_t program = 0;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;  // triangle list
    uint32_t primitiveFlags = 0;  // pmcFlags
    float emitterPosition[3]{};
    std::vector<uint8_t> userMaterial;  // the UserMaterial constant buffer
    std::vector<std::pair<std::string, uint32_t>> textures;  // SDF slot name -> bindless texture index
};

struct ViewerImageView {
    float zoom = 1;                  // 1 fits the shown region to the frame
    float pan[2]{};                  // region center offset from the frame center, pixels
    uint32_t channels = 7;           // bits R, G, B, A; one channel shows as gray
    bool checker = false;
    int32_t mip = -1;                // -1 picks the mip for the zoom
    uint32_t slice = 0;
    uint32_t texture = 0;            // index into SetPreviewTextures
    float source[4]{ 0, 0, 1, 1 };   // uv region shown
};

struct ViewerOverlayRect {
    float uv[4];     // left, top, right, bottom
    float color[4];  // 1 px outline
    float fill[4];
};

struct ViewerDebugVertex {
    float position[3];
    uint32_t color;  // RGBA8, R in the low byte
};

struct ViewerDrawBounds {
    uint32_t id;
    float center[3];
    float radius;
};

enum ViewerLightIconFlags : uint32_t { LIGHT_ICON_SELECTED = 1, LIGHT_ICON_DISABLED = 2 };

// Light icons are world-sized sprites capped on screen; they fade out as they shrink, and hide behind
// geometry closer than their depth minus the margin (lights often sit inside their lamp mesh).
constexpr float LIGHT_ICON_WORLD_SIZE = 1.0f;
constexpr float LIGHT_ICON_MAX_PIXELS = 34.0f;
constexpr float LIGHT_ICON_FADE_BEGIN_PIXELS = 12.0f;
constexpr float LIGHT_ICON_FADE_END_PIXELS = 6.0f;
constexpr float LIGHT_ICON_OCCLUSION_MARGIN = 0.4f;

// A screen-sized light sprite; kind as SceneLightKind (point, spot, directional).
struct ViewerLightIcon {
    float position[3];
    uint32_t kind;
    uint32_t color;  // RGBA8, R in the low byte
    uint32_t flags;
};

// Per-instance vertex data (52 B).
struct ViewerCollisionInstance {
    float world[12];  // row-major float3x4 (ToFloat3x4)
    uint32_t color;   // RGBA8, R in the low byte
};

struct ViewerCollisionBatch {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t baseVertex = 0;
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    bool translucent = false;
};

#endif
