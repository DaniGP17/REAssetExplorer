#ifndef REASSETEXPLORER_SCENELOD_H
#define REASSETEXPLORER_SCENELOD_H
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Assets/Readers/UserReader.h"
#include "Core/Assets/SceneData.h"

enum class SceneLodTest : uint8_t {
    Box,          // camera within detailDistance of the members' bounds
    Circle,       // horizontal camera distance to center below detailDistance
    TerrainCell,  // camera in cell or one of its eight neighbours
    Room,         // drawn from the room the camera stands in (SceneLods::roomDraws)
};

struct SceneLodGroup {
    SceneLodTest test = SceneLodTest::Box;
    float aabbMin[3] = { 1e9f, 1e9f, 1e9f };
    float aabbMax[3] = { -1e9f, -1e9f, -1e9f };
    float center[3] = { 0, 0, 0 };
    float detailDistance = 0;
    int32_t cell[2] = { 0, 0 };
};

// Near the group the low members are hidden, away from it the others.
struct SceneLodMember {
    uint32_t instance;
    uint32_t group;
    bool low;
};

struct SceneLodRole {
    int32_t group = -1;
    bool low = false;
};

struct SceneLods {
    std::vector<SceneLodGroup> groups;
    std::vector<SceneLodMember> members;
    // Room group the camera may stand in -> room groups drawn from it.
    std::map<uint32_t, std::vector<uint32_t>> roomDraws;

    uint32_t AddGroup(float detailDistance);
    void AddMember(uint32_t group, uint32_t instance, bool low, const float lo[3], const float hi[3]);
};

// Room groups count as near when currentRoom is -1 or draws them.
bool SceneLodNear(const SceneLods& lods, uint32_t group, const float eye[3], int32_t currentRoom, bool wasNear);

struct SceneFiles {
    std::function<std::optional<SceneData>(const std::string& pakPath)> scene;
    std::function<std::optional<UserFileData>(const std::string& pakPath)> user;
};

class SceneLodRule {
public:
    virtual ~SceneLodRule() = default;
    virtual void Scene(const std::string& scenePath, const SceneData& scene) {}
    // Role for everything loaded from `reference`, a scene that `scene` (at scenePath) references.
    virtual SceneLodRole ReferencedScene(const std::string& scenePath, const SceneData& scene,
                                         const std::string& reference, SceneLods& lods) {
        return {};
    }
    virtual void Object(const SceneData& scene, const SceneNode& node, uint32_t instance,
                        const float lo[3], const float hi[3]) {}
    // Runs once the whole scene tree has been read.
    virtual void Finish(SceneLods& lods, const SceneFiles& files) {}
};

std::vector<std::unique_ptr<SceneLodRule>> MakeSceneLodRules();

#endif
