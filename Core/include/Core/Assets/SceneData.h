#ifndef REASSETEXPLORER_SCENEDATA_H
#define REASSETEXPLORER_SCENEDATA_H
#include <cstdint>
#include <string>
#include <vector>

#include "../Rsz/RszTypes.h"

struct SceneGameObjectInfo {
    RszGuid guid;
    int32_t id;
    int32_t parentId;
    int32_t componentCount;
    int32_t prefabId;
};

struct SceneFolderInfo {
    int32_t id;
    int32_t parentId;
};

struct SceneNode {
    enum class Kind { Folder, GameObject };

    Kind kind;
    int32_t id;
    int32_t parentId;
    std::string name;

    RszVec3 position{ 0, 0, 0 };
    RszVec4 rotation{ 0, 0, 0, 1 };
    RszVec3 scale{ 1, 1, 1 };
    // File offsets of via.Transform's LocalPosition (Vec3), LocalRotation (Quaternion) and LocalScale (Vec3);
    // 0 when absent.
    uint32_t transformOffsets[3]{};
    // via.Transform: parent joint to attach to; sameJoints makes the joints follow the
    // parent's same-named joints (character parts).
    std::string parentJoint;
    bool sameJoints = false;

    std::string scenePath;
    int32_t instanceIndex = -1;
    std::vector<int32_t> componentIndices;
    std::vector<std::size_t> children;
};

struct SceneData {
    std::vector<SceneGameObjectInfo> gameObjects;
    std::vector<SceneFolderInfo> folders;
    std::vector<std::string> resources;
    std::vector<std::string> prefabs;
    std::vector<RszUserDataRef> sceneUserData;
    RszData rsz;

    std::vector<SceneNode> nodes;
    std::vector<std::size_t> roots;
    std::vector<std::string> externalScenes;

    const RszInstance* Instance(int32_t index) const {
        if (index < 0 || index >= static_cast<int32_t>(rsz.instances.size())) return nullptr;
        return &rsz.instances[index];
    }
};

#endif
