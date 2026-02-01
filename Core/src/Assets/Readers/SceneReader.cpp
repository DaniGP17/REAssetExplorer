#include "Core/Assets/Readers/SceneReader.h"

#include <unordered_map>

#include "Core/IO/MemoryReader.h"
#include "Core/Rsz/RszReader.h"

namespace {

const RszValue* FieldByNames(const RszInstance& instance, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (const RszValue* value = instance.Field(name)) return value;
    }
    return nullptr;
}

uint32_t FieldOffset(const RszInstance& instance, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        for (const RszFieldValue& field : instance.fields) {
            if (field.name == name) return field.offset;
        }
    }
    return 0;
}

std::string StringField(const RszInstance& instance, std::initializer_list<const char*> names) {
    const RszValue* value = FieldByNames(instance, names);
    return value ? value->AsString() : std::string();
}

}

SceneData SceneReader::Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const {
    MemoryReader r(data);

    if ((r.ReadAt<uint32_t>(0) & 0x00FFFFFF) != 0x004E4353) {
        throw std::runtime_error("scn: bad magic");
    }

    SceneData scn;
    int32_t gameObjectCount = r.ReadAt<int32_t>(4);
    int32_t resourceCount = r.ReadAt<int32_t>(8);
    int32_t folderCount = r.ReadAt<int32_t>(12);
    int32_t prefabCount = r.ReadAt<int32_t>(16);
    int32_t userDataCount = r.ReadAt<int32_t>(20);

    uint64_t folderInfoOffset = r.ReadAt<uint64_t>(24);
    uint64_t resourceInfoOffset = r.ReadAt<uint64_t>(32);
    uint64_t prefabInfoOffset = r.ReadAt<uint64_t>(40);
    uint64_t userDataInfoOffset = r.ReadAt<uint64_t>(48);
    uint64_t dataOffset = r.ReadAt<uint64_t>(56);

    std::size_t pos = 64;
    scn.gameObjects.reserve(gameObjectCount);
    for (int32_t i = 0; i < gameObjectCount; i++) {
        SceneGameObjectInfo info;
        auto guidBytes = r.BytesAt(pos, 16);
        std::copy(guidBytes.begin(), guidBytes.end(), info.guid.bytes.begin());
        info.id = r.ReadAt<int32_t>(pos + 16);
        info.parentId = r.ReadAt<int32_t>(pos + 20);
        info.componentCount = r.ReadAt<int32_t>(pos + 24);
        info.prefabId = r.ReadAt<int32_t>(pos + 28);
        scn.gameObjects.push_back(info);
        pos += 32;
    }

    scn.folders.reserve(folderCount);
    for (int32_t i = 0; i < folderCount; i++) {
        scn.folders.push_back({ r.ReadAt<int32_t>(folderInfoOffset + i * 8),
                                r.ReadAt<int32_t>(folderInfoOffset + i * 8 + 4) });
    }

    scn.resources.reserve(resourceCount);
    for (int32_t i = 0; i < resourceCount; i++) {
        scn.resources.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(resourceInfoOffset + i * 8)));
    }

    scn.prefabs.reserve(prefabCount);
    for (int32_t i = 0; i < prefabCount; i++) {
        scn.prefabs.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(prefabInfoOffset + i * 8)));
    }

    scn.sceneUserData.reserve(userDataCount);
    for (int32_t i = 0; i < userDataCount; i++) {
        std::size_t at = userDataInfoOffset + i * 16;
        RszUserDataRef ref;
        ref.typeId = r.ReadAt<uint32_t>(at);
        ref.instanceId = 0;
        ref.path = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 8));
        scn.sceneUserData.push_back(std::move(ref));
    }

    RszReader rszReader(db);
    scn.rsz = rszReader.Read(r, dataOffset);

    BuildHierarchy(scn);
    return scn;
}

void SceneReader::BuildHierarchy(SceneData& scn) {
    const RszData& rsz = scn.rsz;
    std::unordered_map<int32_t, std::size_t> nodeById;

    auto instanceAt = [&](int32_t tableIndex) -> int32_t {
        if (tableIndex < 0 || tableIndex >= static_cast<int32_t>(rsz.objectTable.size())) return -1;
        int32_t instanceIndex = rsz.objectTable[tableIndex];
        if (instanceIndex <= 0 || instanceIndex >= static_cast<int32_t>(rsz.instances.size())) return -1;
        return instanceIndex;
    };

    for (const SceneFolderInfo& folder : scn.folders) {
        SceneNode node;
        node.kind = SceneNode::Kind::Folder;
        node.id = folder.id;
        node.parentId = folder.parentId;
        node.instanceIndex = instanceAt(folder.id);

        if (const RszInstance* instance = scn.Instance(node.instanceIndex)) {
            node.name = StringField(*instance, { "Name", "v0" });
            node.scenePath = StringField(*instance, { "Path", "v5" });
            if (node.scenePath.find(".scn") != std::string::npos) {
                scn.externalScenes.push_back(node.scenePath);
            }
        }

        nodeById[node.id] = scn.nodes.size();
        scn.nodes.push_back(std::move(node));
    }

    for (const SceneGameObjectInfo& go : scn.gameObjects) {
        SceneNode node;
        node.kind = SceneNode::Kind::GameObject;
        node.id = go.id;
        node.parentId = go.parentId;
        node.instanceIndex = instanceAt(go.id);

        if (const RszInstance* instance = scn.Instance(node.instanceIndex)) {
            node.name = StringField(*instance, { "Name", "v0" });
        }

        for (int32_t c = 0; c < go.componentCount; c++) {
            int32_t componentIndex = instanceAt(go.id + 1 + c);
            if (componentIndex < 0) break;
            node.componentIndices.push_back(componentIndex);

            const RszInstance& component = rsz.instances[componentIndex];
            if (component.typeName == "via.Transform") {
                if (const RszValue* v = FieldByNames(component, { "LocalPosition", "v0" })) node.position = v->AsVec3();
                if (const RszValue* v = FieldByNames(component, { "LocalRotation", "v1" })) node.rotation = v->AsVec4();
                if (const RszValue* v = FieldByNames(component, { "LocalScale", "v2" })) node.scale = v->AsVec3();
                node.transformOffsets[0] = FieldOffset(component, { "LocalPosition", "v0" });
                node.transformOffsets[1] = FieldOffset(component, { "LocalRotation", "v1" });
                node.transformOffsets[2] = FieldOffset(component, { "LocalScale", "v2" });
                if (const RszValue* v = FieldByNames(component, { "ParentJointName", "v3" })) node.parentJoint = v->AsString();
                if (const RszValue* v = FieldByNames(component, { "SameJointsConstraint", "v4" })) {
                    const RszBytes* bytes = v->As<RszBytes>();
                    node.sameJoints = v->AsBool() || (bytes && !bytes->empty() && (*bytes)[0] != 0);
                }
            }
        }

        nodeById[node.id] = scn.nodes.size();
        scn.nodes.push_back(std::move(node));
    }

    for (std::size_t i = 0; i < scn.nodes.size(); i++) {
        const SceneNode& node = scn.nodes[i];
        auto parent = nodeById.find(node.parentId);
        if (node.parentId < 0 || parent == nodeById.end()) {
            scn.roots.push_back(i);
        } else {
            scn.nodes[parent->second].children.push_back(i);
        }
    }
}
