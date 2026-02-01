#ifndef REASSETEXPLORER_SCENEUPLOAD_H
#define REASSETEXPLORER_SCENEUPLOAD_H
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Assets/Readers/LightProbeReader.h"
#include "Core/LoadedGame.h"
#include "Explorer/SceneBuilder.h"
#include "Renderer/Viewer.h"

struct SceneLook {
    std::string skyPath;
    float skyIntensity = 8.0f;
    float directionalIntensity = 10.0f;
    bool unlit = true;
};

// The viewer must have no scene yet: game PSOs are created once per viewer.
void UploadScene(Viewer& viewer, const LoadedGame& game, const SceneBuild& build, const TextureCache& textures,
                 const SceneLook& look);

// Post-process zone user data on the scene's components, as the app.*Controller scripts set them.
// resetExposure restarts the auto exposure adaptation (a new scene, not a zone change).
void ApplyToneMap(Viewer& viewer, const SceneToneMap& map, bool resetExposure);
void ApplySsao(Viewer& viewer, const SceneSsao& control, const SceneSsao& zone);
// The color cubes UploadPostProcess loads, in the order SetPostProcess indexes them.
std::vector<std::string> ColorCubePalette(const SceneColorCorrect& colorCorrect);
void ApplySoftBloom(Viewer& viewer, const SceneSoftBloom& component, const SceneSoftBloomZone& zone);
float TemporalSharpness(const SceneToneMapping& toneMapping);
void ApplyTemporalAA(Viewer& viewer, const SceneToneMapping& toneMapping);
// ColorCubeElement[blendTargetIndex], or empty when the zone sets none.
std::string ColorCubeTarget(const SceneColorCorrect& colorCorrect, const SceneColorCorrectZone& zone);
// LwLDRPostProcess blends cubeFrom to cubeTo by rate (LDRColorCorrect ColorCube0, ColorCube1, BlendRate).
void ApplyPostProcess(Viewer& viewer, const SceneToneMapping& toneMapping, const SceneColorCorrect& colorCorrect,
                      const SceneToneMap& zone, const std::string& cubeFrom, const std::string& cubeTo, float rate);
void ApplyFog(Viewer& viewer, const SceneFog& component, const SceneFog& zone);
void ApplyVolumetricFog(Viewer& viewer, const SceneVolumetricFogControl& control,
                        const std::optional<SceneVolumetricFog>& applicationFog, const std::vector<SceneVolumetricFog>& sceneFogs,
                        const SceneVolumetricFogZone& zone);

// The IndirectProbe pool of the network the first component uses: every component on it, by ascending priority,
// as LightProbes::update and UpdateLightProbeOBB do. 12 packed values per probe.
std::vector<uint32_t> BuildLightProbePool(const LoadedGame& game, const std::vector<SceneLightProbes>& components,
                                          const ProbeNetworkData& network);

// RE7 app.LightProbesType: one network is live at a time (the house's, the outside's); the one whose components
// contain the camera. UploadScene uploads the first component's.
class LightProbeNetworks {
public:
    LightProbeNetworks(const LoadedGame& game, const std::vector<SceneLightProbes>& components);
    ~LightProbeNetworks();
    bool Multiple() const { return networks.size() > 1; }
    void Update(Viewer& viewer, const float point[3]);

private:
    struct Network;
    const LoadedGame& game;
    std::vector<std::unique_ptr<Network>> networks;
    std::size_t current = 0;
};

struct SceneFraming {
    float center[3];
    float radius;
};

// bulk frames most instances and sets the clip planes; false frames the whole
// bounds (a prefab is one object, not a level).
SceneFraming FrameScene(Viewer& viewer, const std::vector<ViewerInstanceWorld>& worlds, bool bulk = true);

#endif
