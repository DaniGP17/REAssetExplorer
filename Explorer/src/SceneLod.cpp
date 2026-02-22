#include "Explorer/SceneLod.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>

namespace {

// app.EnvTerrainSceneManager: OnesideLength, CenterGrindIndex and the enable area depth.
constexpr float TERRAIN_CELL_SIZE = 40.0f;
constexpr int32_t TERRAIN_CENTER_CELL = 50;
constexpr int32_t TERRAIN_DETAIL_CELLS = 1;

// Ours, for village LOD markers loaded without the controllers that switch them.
constexpr float VILLAGE_DETAIL_DISTANCE = 150.0f;
constexpr float BOX_HYSTERESIS = 1.1f;

std::string LowerStem(const std::string& path) {
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    name = name.substr(0, name.find('.'));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

const RszInstance* FindComponent(const SceneData& scene, const SceneNode& node, std::string_view type) {
    for (int32_t index : node.componentIndices) {
        const RszInstance* component = scene.Instance(index);
        if (component && component->typeName == type) return component;
    }
    return nullptr;
}

bool HasComponent(const SceneData& scene, std::string_view type) {
    return std::any_of(scene.rsz.instances.begin(), scene.rsz.instances.end(),
                       [&](const RszInstance& i) { return i.typeName == type; });
}

float FloatField(const RszInstance& instance, std::string_view name) {
    const RszValue* value = instance.Field(name);
    const double* d = value ? value->As<double>() : nullptr;
    return d ? static_cast<float>(*d) : 0.0f;
}

std::vector<std::size_t> ParentIndices(const SceneData& scene) {
    std::vector<std::size_t> parents(scene.nodes.size(), SIZE_MAX);
    for (std::size_t i = 0; i < scene.nodes.size(); i++) {
        for (std::size_t child : scene.nodes[i].children) parents[child] = i;
    }
    return parents;
}

// RE8 terrain tiles xNNzMM: a scene with app.EnvTerrainController references <name>_n (full
// detail) and <name>_l (a low mesh only).
class TerrainTileRule : public SceneLodRule {
public:
    SceneLodRole ReferencedScene(const std::string& scenePath, const SceneData& scene,
                                 const std::string& reference, SceneLods& lods) override {
        std::string stem = LowerStem(scenePath);
        std::string child = LowerStem(reference);
        if (child != stem + "_n" && child != stem + "_l") return {};
        int32_t x = 0;
        int32_t z = 0;
        if (std::sscanf(stem.c_str(), "x%dz%d", &x, &z) != 2) return {};
        if (!HasComponent(scene, "app.EnvTerrainController")) return {};
        auto [it, added] = groups.try_emplace(scenePath, 0);
        if (added) {
            it->second = lods.AddGroup(0);
            SceneLodGroup& group = lods.groups[it->second];
            group.test = SceneLodTest::TerrainCell;
            group.cell[0] = x;
            group.cell[1] = z;
        }
        return { static_cast<int32_t>(it->second), child.back() == 'l' };
    }

private:
    std::map<std::string, uint32_t> groups;
};

// RE8 village (app.EnvVillageSceneManager). The collector registers the folders of its scene
// that load a room scene, keyed by the third '_' part of their name; one
// app.EnvVillageSceneController per key, named the same way, draws the key's _Out, _Etc and
// other folders while the player is horizontally closer than DrawOnOut, DrawOnEtc and
// DrawOnInPr. With EnableLODMesh it also swaps the app.EnvVillageLODMarker objects under the
// key's app.EnvRoomController at HiMeshDrawDistance.
class VillageZoneRule : public SceneLodRule {
public:
    void Scene(const std::string& scenePath, const SceneData& scene) override {
        std::vector<std::size_t> parents = ParentIndices(scene);
        markerOf.assign(scene.nodes.size(), -1);
        roomOf.assign(scene.nodes.size(), {});
        for (std::size_t i = 0; i < scene.nodes.size(); i++) {
            const SceneNode& node = scene.nodes[i];
            if (const RszInstance* controller = FindComponent(scene, node, "app.EnvVillageSceneController")) {
                AddController(scene, parents, i, *controller);
            }
            for (std::size_t n = i; n != SIZE_MAX; n = parents[n]) {
                const SceneNode& ancestor = scene.nodes[n];
                if (markerOf[i] < 0) {
                    if (const RszInstance* marker = FindComponent(scene, ancestor, "app.EnvVillageLODMarker")) {
                        const RszValue* isLow = marker->Field("IsLowMesh");
                        markerOf[i] = isLow && isLow->AsBool() ? 1 : 0;
                    }
                }
                if (FindComponent(scene, ancestor, "app.EnvRoomController")) {
                    roomOf[i] = Key(ancestor.name);
                    if (markerOf[i] >= 0) roomScenes.insert(scenePath);
                    break;
                }
            }
        }
    }

    SceneLodRole ReferencedScene(const std::string& scenePath, const SceneData& scene,
                                 const std::string& reference, SceneLods& lods) override {
        auto [collector, added] = collectorScenes.try_emplace(scenePath, false);
        if (added) collector->second = HasComponent(scene, "app.EnvVillageSceneCollector");
        if (!collector->second) return {};
        auto folder = std::find_if(scene.nodes.begin(), scene.nodes.end(), [&](const SceneNode& n) {
            return n.kind == SceneNode::Kind::Folder && n.scenePath == reference;
        });
        if (folder == scene.nodes.end()) return {};
        std::string key = Key(folder->name);
        if (key.empty()) return {};
        Kind kind = folder->name.find("_Out") != std::string::npos   ? Kind::Out
                    : folder->name.find("_Etc") != std::string::npos ? Kind::Etc
                                                                     : Kind::Other;
        auto [group, created] = folderGroups.try_emplace({ key, kind }, 0);
        if (created) group->second = lods.AddGroup(0);
        return { static_cast<int32_t>(group->second), false };
    }

    void Object(const SceneData& scene, const SceneNode& node, uint32_t instance,
                const float lo[3], const float hi[3]) override {
        std::size_t index = static_cast<std::size_t>(&node - scene.nodes.data());
        if (index >= markerOf.size() || markerOf[index] < 0) return;
        Marked m{ instance, markerOf[index] == 1, roomOf[index] };
        std::copy(lo, lo + 3, m.lo);
        std::copy(hi, hi + 3, m.hi);
        marked.push_back(m);
    }

    void Finish(SceneLods& lods, const SceneFiles& files) override {
        if (controllers.empty()) ReadStageControlData(files);

        for (const auto& [id, group] : folderGroups) {
            SceneLodGroup& g = lods.groups[group];
            g.test = SceneLodTest::Circle;
            auto controller = controllers.find(id.first);
            if (controller == controllers.end()) {
                g.detailDistance = std::numeric_limits<float>::infinity();
                continue;
            }
            const Controller& c = controller->second;
            std::copy(c.position, c.position + 3, g.center);
            g.detailDistance = id.second == Kind::Out ? c.drawOut : id.second == Kind::Etc ? c.drawEtc : c.drawOther;
        }

        std::map<std::string, uint32_t> roomGroups;
        std::vector<const Marked*> orphans;
        for (const Marked& m : marked) {
            auto controller = controllers.find(m.room);
            if (controller == controllers.end()) {
                orphans.push_back(&m);
                continue;
            }
            const Controller& c = controller->second;
            if (!c.lodMesh) continue;
            auto [group, created] = roomGroups.try_emplace(m.room, 0);
            if (created) {
                group->second = lods.AddGroup(c.hiMeshDistance);
                SceneLodGroup& g = lods.groups[group->second];
                g.test = SceneLodTest::Circle;
                std::copy(c.position, c.position + 3, g.center);
            }
            lods.AddMember(group->second, m.instance, m.low, m.lo, m.hi);
        }
        ClusterOrphans(orphans, lods);
    }

private:
    enum class Kind { Out, Etc, Other };

    struct Controller {
        float position[3];
        float drawOut;
        float drawEtc;
        float drawOther;
        bool lodMesh;
        float hiMeshDistance;
    };

    struct Marked {
        uint32_t instance;
        bool low;
        std::string room;
        float lo[3];
        float hi[3];
    };

    static std::string Key(const std::string& name) {
        std::size_t first = name.find('_');
        if (first == std::string::npos) return {};
        std::size_t second = name.find('_', first + 1);
        if (second == std::string::npos) return {};
        return name.substr(second + 1, name.find('_', second + 1) - second - 1);
    }

    void AddController(const SceneData& scene, const std::vector<std::size_t>& parents, std::size_t index,
                       const RszInstance& component) {
        std::string key = Key(scene.nodes[index].name);
        if (key.empty() || controllers.count(key)) return;
        Controller c{};
        for (std::size_t n = index; n != SIZE_MAX; n = parents[n]) {
            const SceneNode& node = scene.nodes[n];
            if (node.kind != SceneNode::Kind::GameObject) continue;
            c.position[0] += node.position.x;
            c.position[1] += node.position.y;
            c.position[2] += node.position.z;
        }
        c.drawOut = FloatField(component, "DrawOnOut");
        c.drawEtc = FloatField(component, "DrawOnEtc");
        c.drawOther = FloatField(component, "DrawOnInPr");
        const RszValue* lodMesh = component.Field("EnableLODMesh");
        c.lodMesh = lodMesh && lodMesh->AsBool();
        c.hiMeshDistance = FloatField(component, "HiMeshDrawDistance");
        controllers.emplace(key, c);
    }

    // The chapters load the controllers (stagecontroldata/stNN[_VVV]_EnvControl) next to the
    // collector scene (sceneroom/stNN_Env[VVV]); without them they are read from there, and
    // for a room scene (sceneroom/stNN/stNN_...) from the stage's own.
    void ReadStageControlData(const SceneFiles& files) {
        if (!files.scene) return;
        std::vector<std::vector<std::string>> candidates;
        auto controlData = [](const std::string& scenePath, std::size_t dir, const std::string& name) {
            return scenePath.substr(0, dir) + "/stagecontroldata/" + name + "_envcontrol" +
                   scenePath.substr(scenePath.find('.', scenePath.find_last_of('/')));
        };
        for (const auto& [scenePath, isCollector] : collectorScenes) {
            std::size_t dir = scenePath.rfind("/sceneroom/");
            std::string stem = LowerStem(scenePath);
            std::size_t env = stem.find("_env");
            if (!isCollector || dir == std::string::npos || env == std::string::npos) continue;
            std::string stage = stem.substr(0, env);
            std::string variant = stem.substr(env + 4);
            candidates.emplace_back();
            if (!variant.empty()) candidates.back().push_back(controlData(scenePath, dir, stage + "_" + variant));
            candidates.back().push_back(controlData(scenePath, dir, stage));
        }
        for (const std::string& scenePath : roomScenes) {
            std::size_t dir = scenePath.rfind("/sceneroom/");
            std::string stem = LowerStem(scenePath);
            if (dir == std::string::npos || stem.find('_') == std::string::npos) continue;
            candidates.push_back({ controlData(scenePath, dir, stem.substr(0, stem.find('_'))) });
        }
        std::set<std::string> done;
        for (const std::vector<std::string>& paths : candidates) {
            for (const std::string& path : paths) {
                if (!done.insert(path).second) break;
                std::optional<SceneData> scene = files.scene(path);
                if (!scene) continue;
                Scene(path, *scene);
                break;
            }
        }
    }

    // Overlapping marked objects switch together: a proxy and the detail it stands for may
    // sit in different scenes.
    static void ClusterOrphans(const std::vector<const Marked*>& orphans, SceneLods& lods) {
        auto overlap = [](const Marked& a, const Marked& b) {
            for (int i = 0; i < 3; i++) {
                if (a.hi[i] < b.lo[i] || b.hi[i] < a.lo[i]) return false;
            }
            return true;
        };
        std::vector<std::size_t> root(orphans.size());
        std::iota(root.begin(), root.end(), 0);
        auto find = [&](std::size_t i) {
            while (root[i] != i) i = root[i] = root[root[i]];
            return i;
        };
        for (std::size_t a = 0; a < orphans.size(); a++) {
            for (std::size_t b = a + 1; b < orphans.size(); b++) {
                if (overlap(*orphans[a], *orphans[b])) root[find(a)] = find(b);
            }
        }
        std::map<std::size_t, uint32_t> groupOf;
        for (std::size_t i = 0; i < orphans.size(); i++) {
            auto [it, added] = groupOf.try_emplace(find(i), 0);
            if (added) it->second = lods.AddGroup(VILLAGE_DETAIL_DISTANCE);
            lods.AddMember(it->second, orphans[i]->instance, orphans[i]->low, orphans[i]->lo, orphans[i]->hi);
        }
    }

    std::map<std::string, Controller> controllers;
    std::map<std::string, bool> collectorScenes;
    std::set<std::string> roomScenes;
    std::map<std::pair<std::string, Kind>, uint32_t> folderGroups;
    std::vector<int8_t> markerOf;
    std::vector<std::string> roomOf;
    std::vector<Marked> marked;
};

// RE8 castle and other indoor stages (app.EnvSceneManager). The collector registers its
// scene's room folders by the third '_' part of their name; the chapter's
// app.AdditionalEnvCullingData points at app.EnvCullingData lists that name, for each room
// the player may stand in, the rooms drawn from it. The room is that of the ground under the
// player; every other listed room is not drawn.
class IndoorRoomRule : public SceneLodRule {
public:
    void Scene(const std::string& scenePath, const SceneData& scene) override {
        for (const SceneNode& node : scene.nodes) {
            const RszInstance* component = FindComponent(scene, node, "app.AdditionalEnvCullingData");
            const RszValue* data = component ? component->Field("data") : nullptr;
            const RszArray* refs = data ? data->As<RszArray>() : nullptr;
            if (!refs) continue;
            for (const RszValue& ref : *refs) {
                const RszObjectRef* object = ref.As<RszObjectRef>();
                if (!object) continue;
                for (const RszUserDataRef& user : scene.rsz.userData) {
                    if (user.instanceId == object->index) AddCullingData(UserPakPath(user.path));
                }
            }
        }
    }

    SceneLodRole ReferencedScene(const std::string& scenePath, const SceneData& scene,
                                 const std::string& reference, SceneLods& lods) override {
        auto [collector, added] = collectorScenes.try_emplace(scenePath, false);
        if (added) collector->second = HasComponent(scene, "app.EnvSceneCollector");
        if (!collector->second) return {};
        auto folder = std::find_if(scene.nodes.begin(), scene.nodes.end(), [&](const SceneNode& n) {
            return n.kind == SceneNode::Kind::Folder && n.scenePath == reference;
        });
        if (folder == scene.nodes.end()) return {};
        std::string key = NamePart(folder->name, 2);
        if (key.empty()) return {};
        auto [group, created] = rooms.try_emplace(key, 0);
        if (created) {
            group->second = lods.AddGroup(0);
            lods.groups[group->second].test = SceneLodTest::Room;
        }
        return { static_cast<int32_t>(group->second), false };
    }

    void Finish(SceneLods& lods, const SceneFiles& files) override {
        if (rooms.empty() || !files.user) return;
        // Without the chapter: stagecullingdata/stNN/stNN_CullingData next to sceneroom/stNN_Env.
        if (cullingData.empty()) {
            for (const auto& [scenePath, isCollector] : collectorScenes) {
                std::size_t dir = scenePath.rfind("/sceneroom/");
                std::string stem = LowerStem(scenePath);
                if (!isCollector || dir == std::string::npos || stem.find("_env") == std::string::npos) continue;
                std::string stage = stem.substr(0, stem.find("_env"));
                AddCullingData(scenePath.substr(0, dir) + "/stagecullingdata/" + stage + "/" + stage + "_cullingdata.user.2");
            }
        }
        for (const std::string& path : cullingData) {
            std::optional<UserFileData> user = files.user(path);
            if (!user) continue;
            const RszInstance* root = user->RootInstance() >= 0 ? &user->rsz.instances[user->RootInstance()] : nullptr;
            const RszValue* settings = root ? root->Field("Settings") : nullptr;
            const RszArray* entries = settings ? settings->As<RszArray>() : nullptr;
            if (!entries) continue;
            for (const RszValue& entry : *entries) {
                const RszObjectRef* ref = entry.As<RszObjectRef>();
                if (!ref || ref->index >= user->rsz.instances.size()) continue;
                const RszInstance& data = user->rsz.instances[ref->index];
                const RszValue* current = data.Field("CurrentName");
                const RszValue* names = data.Field("Names");
                auto from = current ? rooms.find(current->AsString()) : rooms.end();
                if (from == rooms.end() || !names || !names->As<RszArray>()) continue;
                std::vector<uint32_t>& drawn = lods.roomDraws[from->second];
                for (const RszValue& name : *names->As<RszArray>()) {
                    auto room = rooms.find(name.AsString());
                    if (room != rooms.end() && std::find(drawn.begin(), drawn.end(), room->second) == drawn.end()) {
                        drawn.push_back(room->second);
                    }
                }
            }
        }
    }

private:
    static std::string NamePart(const std::string& name, int index) {
        std::size_t start = 0;
        for (int i = 0; i < index; i++) {
            start = name.find('_', start);
            if (start == std::string::npos) return {};
            start++;
        }
        return name.substr(start, name.find('_', start) - start);
    }

    static std::string UserPakPath(std::string path) {
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "natives/stm/" + path + ".2";
    }

    void AddCullingData(const std::string& path) {
        if (std::find(cullingData.begin(), cullingData.end(), path) == cullingData.end()) cullingData.push_back(path);
    }

    std::map<std::string, bool> collectorScenes;
    std::map<std::string, uint32_t> rooms;
    std::vector<std::string> cullingData;
};

}

uint32_t SceneLods::AddGroup(float detailDistance) {
    groups.push_back({});
    groups.back().detailDistance = detailDistance;
    return static_cast<uint32_t>(groups.size() - 1);
}

void SceneLods::AddMember(uint32_t group, uint32_t instance, bool low, const float lo[3], const float hi[3]) {
    members.push_back({ instance, group, low });
    SceneLodGroup& g = groups[group];
    for (int i = 0; i < 3; i++) {
        g.aabbMin[i] = std::min(g.aabbMin[i], lo[i]);
        g.aabbMax[i] = std::max(g.aabbMax[i], hi[i]);
    }
}

bool SceneLodNear(const SceneLods& lods, uint32_t index, const float eye[3], int32_t currentRoom, bool wasNear) {
    const SceneLodGroup& group = lods.groups[index];
    switch (group.test) {
    case SceneLodTest::Room: {
        if (currentRoom < 0) return true;
        auto drawn = lods.roomDraws.find(static_cast<uint32_t>(currentRoom));
        return drawn == lods.roomDraws.end() ||
               std::find(drawn->second.begin(), drawn->second.end(), index) != drawn->second.end();
    }
    case SceneLodTest::Circle: {
        float dx = eye[0] - group.center[0];
        float dz = eye[2] - group.center[2];
        return dx * dx + dz * dz < group.detailDistance * group.detailDistance;
    }
    case SceneLodTest::TerrainCell: {
        // app.EnvTerrainSceneManager.calcCurrentGridIndex
        auto cell = [](float v) {
            int32_t c = static_cast<int32_t>(v / TERRAIN_CELL_SIZE);
            return (v < 0 ? c - 1 : c) + TERRAIN_CENTER_CELL;
        };
        int32_t dx = std::abs(cell(eye[0]) - group.cell[0]);
        int32_t dz = std::abs(cell(eye[2]) - group.cell[1]);
        return std::max(dx, dz) <= TERRAIN_DETAIL_CELLS;
    }
    case SceneLodTest::Box:
        break;
    }
    float d2 = 0;
    for (int i = 0; i < 3; i++) {
        float d = std::max({ group.aabbMin[i] - eye[i], 0.0f, eye[i] - group.aabbMax[i] });
        d2 += d * d;
    }
    float limit = wasNear ? group.detailDistance * BOX_HYSTERESIS : group.detailDistance;
    return d2 < limit * limit;
}

std::vector<std::unique_ptr<SceneLodRule>> MakeSceneLodRules() {
    std::vector<std::unique_ptr<SceneLodRule>> rules;
    rules.push_back(std::make_unique<TerrainTileRule>());
    rules.push_back(std::make_unique<VillageZoneRule>());
    rules.push_back(std::make_unique<IndoorRoomRule>());
    return rules;
}
