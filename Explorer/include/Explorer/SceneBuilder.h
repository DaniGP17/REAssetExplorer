#ifndef REASSETEXPLORER_SCENEBUILDER_H
#define REASSETEXPLORER_SCENEBUILDER_H
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Assets/EffectData.h"
#include "Core/Assets/MaterialData.h"
#include "Core/Assets/MotionData.h"
#include "Core/Assets/SceneData.h"
#include "Core/Assets/SdfData.h"
#include "Core/Assets/TerrainData.h"
#include "Core/Assets/TextureData.h"
#include "Core/LoadedGame.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Explorer/EffectSimulator.h"
#include "Explorer/LightComponents.h"
#include "Explorer/SceneAiMap.h"
#include "Explorer/SceneCollision.h"
#include "Explorer/SceneLod.h"
#include "Explorer/SkeletalAnimator.h"
#include "Renderer/RenderMath.h"
#include "Renderer/RenderTypes.h"

class RszTypeDatabase;

// Prefers an entry that ends with contains + suffix, so a full file name does
// not resolve to a longer sibling (fluid_01_002_0000 vs fluid_01_002_0000_sd).
std::string FindPath(const LoadedGame& game, std::string_view contains, std::string_view suffix);
std::string ToPakPath(std::string path, const char* version);
// Same-name mdf first: character folders also hold variants (em2000_death.mdf2).
std::string FindMeshMaterial(const LoadedGame& game, const std::string& meshPath);

std::vector<GameInputElement> BuildInputLayout(const SdfProgram& program);
GameTextureDesc ToGameTexture(const TextureData& tex);

struct TextureCache {
    std::map<std::string, uint32_t> byPath;
    std::vector<GameTextureDesc> descs;
    std::deque<TextureData> storage;
    // With deferred set, LoadTexture only reserves the index; ResolveTextures reads them.
    bool deferred = false;
    std::vector<std::pair<uint32_t, std::string>> pending;

    // Index 0 stays black so record padding (zeroed texture slots) never
    // resolves to an arbitrary texture.
    TextureCache();
};

template <typename T>
class AsyncLoads;

uint32_t LoadTexture(const LoadedGame& game, TextureCache& cache, const std::string& path);
// Reads the pending textures on several threads, taking those already loaded by file path from prefetched.
void ResolveTextures(const LoadedGame& game, TextureCache& cache, AsyncLoads<TextureData>* prefetched = nullptr);
// Streamed assets (texture top mips, movies) live in a streaming/ copy of the path.
std::string StreamingCopyPath(const LoadedGame& game, const std::string& pakPath);
// For emitters without a UVSequence (the cache's index 0 is transparent black).
uint32_t SoftDotTexture(TextureCache& cache);
std::vector<GameMaterialDesc> BuildGameMaterials(const LoadedGame& game, const MaterialData& mdf,
                                                 TextureCache& cache);

struct LoadedEffect {
    std::string path;
    EffectData data;
    std::vector<EffectEmitterAssets> assets;
    // The material emitters' programs (EffectMaterialAssets::program), keyed "master pak path|program".
    std::vector<ViewerMaterialProgram> programs;
    std::vector<std::string> programKeys;
};

LoadedEffect LoadEffect(const LoadedGame& game, TextureCache& cache, const std::string& pakPath);
// With a deferred cache, call once its textures are resolved.
void ResolveEffectTextureFlags(LoadedEffect& fx, const TextureCache& cache);

struct DebugOverlay {
    std::vector<ViewerDebugVertex> triangles;
    std::vector<ViewerDebugVertex> lines;
    float boundsMin[3] = { 1e9f, 1e9f, 1e9f };
    float boundsMax[3] = { -1e9f, -1e9f, -1e9f };
};

void AppendTerrainOverlay(const TerrainData& terr, DebugOverlay& overlay);
std::string SiblingMesh(const LoadedGame& game, const std::string& mcolPakPath);


struct MasterRegistry {
    std::deque<SdfData> masters;
    std::map<std::string, const SdfData*> mastersByPath;
    std::map<std::string, uint32_t> pipelineByPath;
    std::vector<GameDeferredDesc> deferredDescs;
    const SdfProgram* prepass = nullptr;
};

struct SkinnedInstance {
    std::string meshPath;
    uint32_t jointOffset;
    Mat4 world;
    std::shared_ptr<const SkeletonData> skeleton;
    std::string owner;  // SceneObjectKey; empty for loose meshes
    // Index into SceneBuild::skinned whose same-named joints this one follows
    // (SameJointsConstraint); -1 for none.
    int32_t follows = -1;
    uint32_t instance = 0;  // into SceneBuild::worlds
};

// An object riding a joint of a skinned instance (via.Transform ParentJointName).
struct SceneJointAttachment {
    uint32_t instance = 0;  // index into SceneBuild::worlds
    int32_t skinned = -1;   // index into SceneBuild::skinned
    int32_t joint = -1;     // joint of that skeleton
    Mat4 local;             // relative to the joint
};

// Inspector replacements keyed by the *Key helpers below; values are pak paths.
using AssetOverrides = std::map<std::string, std::string>;
// Override key whose value is the light environment state to load (morning, day, ...).
inline constexpr const char* LIGHT_STATE_OVERRIDE = "lightstate";

// Engine samplers by name: Point / Bilinear (mip point) / Trilinear / Automatic (anisotropic 16 in the RE8
// capture) and Wrap / Clamp / Mirror. False for other names.
bool SamplerFromName(const std::string& name, D3D12_FILTER& filter, D3D12_TEXTURE_ADDRESS_MODE& address);

std::string ComponentFieldKey(const std::string& objectKey, int32_t componentIndex, std::string_view field);
std::string MaterialTextureKey(const std::string& mdfPakPath, const std::string& material, const std::string& type);
std::string MeshMaterialKey(const std::string& meshPakPath);
// Material parameters use the same override map; the value is "a,b,c,d".
std::string MaterialParamKey(const std::string& mdfPakPath, const std::string& material, const std::string& param);
// The form scenes and materials store: "natives/stm/streaming/a/b.tex.10" -> "a/b.tex".
std::string SourceAssetPath(const std::string& pakPath);
SceneData ReadSceneOrPrefab(const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath);

// Location in the viewer's material records, for Viewer::UpdateMaterialParams.
struct SceneMaterialParam {
    std::string key;  // MaterialParamKey
    uint32_t materialIndex;
    uint32_t offset;
    uint32_t count;
};

// submesh numbers the clusters of one LOD across its parts in file order, as the
// outline does.
struct SceneDrawTag {
    uint32_t lod = 0;
    uint32_t submesh = 0;
};

// A scene instance of a mesh with several LODs; its draws carry their LOD in drawTags.
struct SceneMeshLod {
    uint32_t instance;
    float center[3];
    float radius;  // the mesh's bounding sphere, unscaled, as the engine uses it
    uint32_t lodCount;
    float thresholds[7];  // LOD i while depth / (P00 * radius) <= thresholds[i]
};

// app.PostProcessZoneControl: a trigger volume (filter Sensor_Player) and the first user data of each list
// (_UseIndex 0), from the zone or from the active PostProcessWeatherSetting naming it. found = false: the zone
// leaves that effect as it is.
struct ScenePostProcessZone {
    std::string name;
    int32_t priority = 0;
    std::vector<CollisionVolume> volumes;
    SceneToneMap toneMap;
    SceneFog fog;
    SceneVolumetricFogZone volumetricFog;
    SceneSsao ssao;
    SceneColorCorrectZone colorCorrect;
    SceneSoftBloomZone softBloom;
};

// A GameObject the viewport can move: every instance, collider and light it or its children own
// follows its world matrix.
struct SceneObject {
    std::string key;    // SceneObjectKey
    int32_t parent = -1;  // nearest GameObject ancestor in SceneBuild::objects
    Mat4 world;
};

// Inspector-editable Transform fields of a GameObject: "position", "rotation" (XYZ euler degrees) or
// "scale", valued "x,y,z" in the overrides.
std::string TransformParamKey(const std::string& objectKey, std::string_view field);
bool ParseTransformParamKey(std::string_view key, std::string& objectKey, std::string& field);

enum class SceneLightKind : uint32_t { Point, Spot, Directional };

// A scene light as the editor shows it: an icon at the GameObject and, selected, its reach.
struct SceneLightMarker {
    std::string owner;  // SceneObjectKey
    SceneLightKind kind = SceneLightKind::Point;
    bool enabled = false;
    float position[3] = {};
    float direction[3] = {};  // spot: where it shines; directional: towards the light
    float range = 0;
    float cone = 0;    // degrees, full angle
    float spread = 0;  // degrees of the cone's soft edge
    float color[3] = { 1, 1, 1 };
    int32_t lightIndex = -1;  // into SceneBuild::lights; -1 when the light adds nothing
};

// ObjectEffectManager.DataContainer (+ExternalDataContainers) -> EPVDataContainer prefab (epvc)
// -> ExpertData[].Data prefab (epve) -> elements naming .efx files.
struct ObjectEffectElement {
    std::string container;  // pak paths
    std::string provider;
    std::string type;
    std::vector<std::string> effects;
    uint32_t triggerId = 0xFFFFFFFF;
    bool autoPlay = false;
    Mat4 local;  // Offset, Rotation (degrees), Scale
    float loopFrames = 0;
};

struct EffectProviderCache {
    std::map<std::string, std::optional<SceneData>> prefabs;
};

std::vector<ObjectEffectElement> ReadObjectEffects(const LoadedGame& game, const RszTypeDatabase& db, const SceneData& scn,
                                                   const RszInstance& manager, EffectProviderCache& cache);
// EnvironmentEffectManager: the EPV*Data components on its GameObject. Zones start them in the game; every
// element without a TriggerId counts as playing.
std::vector<ObjectEffectElement> ReadEnvironmentEffects(const LoadedGame& game, const SceneData& scn, const SceneNode& node);

// An effect an ObjectEffectManager plays on its own.
struct SceneEffect {
    uint32_t asset = 0;  // SceneBuild::effectAssets index
    Mat4 world;
    std::string owner;  // SceneObjectKey
    float loopFrames = 0;  // replays every loopFrames frames; 0 plays once
    std::vector<uint32_t> zones;  // SceneBuild::effectZones that enable it; empty plays always
};

// via.effect.script.EffectEmitZoneGroup: its target EnvironmentEffectManagers play while the player is inside.
struct SceneEffectZone {
    std::string name;
    std::vector<CollisionVolume> volumes;
};

struct SceneZoneInstance {
    uint32_t instance;  // an effect's static mesh, shown with the effect
    std::vector<uint32_t> zones;
};

// Every mesh of a scene shares one set of vertex/index buffers.
struct SceneBuild {
    std::vector<uint8_t> positions;
    std::vector<uint8_t> normals;
    std::vector<uint8_t> uv0;
    std::vector<uint8_t> uv1;
    std::vector<uint8_t> weights;
    std::vector<SkinnedInstance> skinned;
    std::vector<SceneJointAttachment> attachments;
    uint32_t skinMatrixCount = 0;
    std::vector<uint8_t> indices;
    std::vector<ViewerMeshDraw> draws;  // draws[i].id == i
    std::vector<SceneDrawTag> drawTags;
    std::vector<ViewerInstanceWorld> worlds;
    std::vector<std::string> instanceOwners;  // SceneObjectKey of each instance; empty for loose meshes
    std::vector<std::string> hiddenObjects;  // SceneObjectKeys the game starts hidden
    std::vector<std::vector<uint32_t>> instanceMaterials;
    std::vector<std::string> instanceMeshes;
    std::vector<GameMaterialDesc> materials;
    // "mdf|material|master" per materials entry.
    std::vector<std::string> materialNames;
    std::vector<SceneMaterialParam> materialParams;
    std::vector<GameLightParam> lights;
    // Lights under the application MainCamera (Assistlight), positioned relative to the camera.
    std::vector<GameLightParam> cameraLights;
    // Volumetric scattering color (rgb) per lights / cameraLights entry.
    std::vector<float> lightScattering;
    std::vector<float> cameraLightScattering;
    std::vector<SceneDirectionalLight> directionalLights;
    std::string directionalOwner;  // SceneObjectKey of directionalLights.front(), the one uploaded
    std::vector<SceneLightMarker> lightMarkers;
    std::vector<SceneObject> objects;
    std::vector<SceneIbl> ibls;
    std::vector<SceneLocalCubemap> localCubemaps;
    std::vector<SceneLightProbes> lightProbes;
    SceneToneMap toneMap;
    SceneFog fog;
    SceneFog fogZone;
    // The scenes' enabled media in load order; the MainCamera's global one apart.
    std::vector<SceneVolumetricFog> volumetricFogs;
    std::optional<SceneVolumetricFog> applicationVolumetricFog;
    SceneVolumetricFogControl volumetricFogControl;
    SceneVolumetricFogZone volumetricFogZone;
    std::vector<ScenePostProcessZone> postProcessZones;
    SceneSsao ssao;
    SceneMainCamera mainCamera;
    SceneToneMapping toneMapping;
    SceneColorCorrect colorCorrect;
    SceneColorCorrectZone colorCorrectZone;
    SceneSoftBloom softBloom;
    SceneSoftBloomZone softBloomZone;
    // ColorCubeElement[index] set by timelines played at scene start (PlaySceneStart).
    std::map<uint32_t, std::string> colorCubeElements;
    SceneCollision collision;
    std::vector<SceneAiMap> aiMaps;
    std::vector<LoadedEffect> effectAssets;  // their material program indices point into materialPrograms
    std::vector<SceneEffect> effects;
    std::vector<SceneEffectZone> effectZones;
    std::vector<SceneZoneInstance> zoneInstances;
    std::vector<ViewerMaterialProgram> materialPrograms;
    std::vector<std::string> materialProgramKeys;
    SceneLods lods;
    std::vector<SceneMeshLod> meshLods;
    // States the light environment controllers offer, and the one loaded.
    std::vector<std::string> lightStates;
    std::string lightState;
    MasterRegistry masters;
    const AssetOverrides* overrides = nullptr;  // input: applied while building
    float aabbMin[3] = { 1e9f, 1e9f, 1e9f };
    float aabbMax[3] = { -1e9f, -1e9f, -1e9f };
};

struct SceneMeshAsset {
    bool valid = false;
    std::string meshPath;
    std::vector<ViewerMeshDraw> draws;
    std::vector<SceneDrawTag> drawTags;
    uint32_t lodCount = 1;
    std::vector<uint32_t> materialIndices;
    std::shared_ptr<const SkeletonData> skeleton;
    float aabbMin[3];
    float aabbMax[3];
    float boundingRadius = 0;
    uint32_t lodGroup = 0;
};

// Scenes only draw LOD0; allLods adds the other LODs too (tagged, for LOD preview).
SceneMeshAsset AppendSceneMesh(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                               const std::string& meshPakPath, const std::string& mdfPakPath,
                               bool allLods = false);
// Radius 0.5. Without materialName, the material with the most real textures.
SceneMeshAsset AppendMaterialSphere(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                    const std::string& mdfPakPath, const std::string& materialName = {});
// The draw for material slot i is tagged as LOD i, so the viewport shows one at
// a time (lodCount = material count).
SceneMeshAsset AppendMaterialSpheres(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                     const std::string& mdfPakPath);
void AddSceneInstance(SceneBuild& build, const SceneMeshAsset& asset, const Mat4& world, std::string owner = {});
// Node indices are stable per scene file, so the outline and the build agree on
// keys without sharing state.
std::string SceneObjectKey(const std::string& scenePakPath, std::size_t nodeIndex);
void BuildSceneFromScn(const LoadedGame& game, const RszTypeDatabase& db,
                       const std::string& rootScnPath, SceneBuild& build, TextureCache& cache,
                       float lightScale);

GamePrepassDesc MakePrepassDesc(const SceneBuild& build);
// Spans point into build, which must outlive the upload.
ViewerMesh MakeViewerMesh(const SceneBuild& build);

#endif
