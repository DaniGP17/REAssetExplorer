#ifndef REASSETEXPLORER_POSTPROCESSZONES_H
#define REASSETEXPLORER_POSTPROCESSZONES_H
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Explorer/SceneBuilder.h"
#include "Renderer/Viewer.h"

// app.PostProcessSystemApp's zones: entering a zone makes it the current one of its priority (onOverlapping);
// the lowest priority number whose current zone still contains the point applies (PostProcessDevice::updateImpl),
// and each effect it sets blends in linearly over its user data's _AnimationTime (app.*Controller::updateAnimation).
class PostProcessZones {
public:
    explicit PostProcessZones(const SceneBuild& build);
    bool Empty() const { return zones.empty(); }
    // dt in seconds. The first zone applies at once, as at startup.
    void Update(Viewer& viewer, const float point[3], float dt);
    // Empty until a zone applies.
    const std::string& CurrentZone() const;

private:
    template <typename T>
    struct Blend {
        T from{};
        T to{};
        T value{};
        float timer = 0;
        float duration = 0;
        bool active = false;
    };

    void Start(const ScenePostProcessZone& zone, bool immediate);
    void Apply(Viewer& viewer, bool resetExposure);

    std::vector<ScenePostProcessZone> zones;
    std::vector<uint8_t> inside;
    std::map<int32_t, std::size_t> current;
    std::optional<std::size_t> applied;
    bool started = false;
    SceneFog fogComponent;
    SceneVolumetricFogControl volumetricControl;
    std::optional<SceneVolumetricFog> applicationFog;
    std::vector<SceneVolumetricFog> sceneFogs;
    SceneSsao ssaoControl;
    SceneToneMapping toneMapping;
    SceneColorCorrect colorCorrect;
    SceneSoftBloom softBloom;
    SceneSoftBloomZone softBloomZone;
    Blend<SceneToneMap> toneMap;
    Blend<SceneFog> fog;
    Blend<SceneVolumetricFogZone> volumetricFog;
    Blend<SceneSsao> ssao;
    std::string cubeFrom;
    std::string cubeTo;
    float cubeRate = 0;
    float cubeTimer = 0;
    float cubeDuration = 0;
    bool cubeActive = false;
};

#endif
