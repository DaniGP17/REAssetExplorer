#include "Core/Assets/Readers/PrefabReader.h"

#include <stdexcept>

#include "Core/Assets/Readers/SceneReader.h"
#include "Core/IO/MemoryReader.h"
#include "Core/Rsz/RszReader.h"

// Version 17 (RE7 RT and RE8), after REE-Lib PfbFile. Unread: object-reference count at 12, table offset at 24.
SceneData PrefabReader::Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != 0x00424650) throw std::runtime_error("pfb: bad magic");

    int32_t gameObjectCount = r.ReadAt<int32_t>(4);
    int32_t resourceCount = r.ReadAt<int32_t>(8);
    uint64_t userDataCount = r.ReadAt<uint64_t>(16);
    uint64_t resourceInfoOffset = r.ReadAt<uint64_t>(32);
    uint64_t userDataInfoOffset = r.ReadAt<uint64_t>(40);
    uint64_t dataOffset = r.ReadAt<uint64_t>(48);

    SceneData pfb;
    pfb.gameObjects.reserve(gameObjectCount);
    for (int32_t i = 0; i < gameObjectCount; i++) {
        std::size_t at = 0x38 + static_cast<std::size_t>(i) * 12;
        SceneGameObjectInfo info{};
        info.id = r.ReadAt<int32_t>(at);
        info.parentId = r.ReadAt<int32_t>(at + 4);
        info.componentCount = r.ReadAt<int32_t>(at + 8);
        info.prefabId = -1;
        pfb.gameObjects.push_back(info);
    }

    pfb.resources.reserve(resourceCount);
    for (int32_t i = 0; i < resourceCount; i++) {
        pfb.resources.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(resourceInfoOffset + i * 8)));
    }

    pfb.sceneUserData.reserve(userDataCount);
    for (uint64_t i = 0; i < userDataCount; i++) {
        std::size_t at = userDataInfoOffset + i * 16;
        RszUserDataRef ref;
        ref.typeId = r.ReadAt<uint32_t>(at);
        ref.instanceId = 0;
        ref.path = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 8));
        pfb.sceneUserData.push_back(std::move(ref));
    }

    RszReader rszReader(db);
    pfb.rsz = rszReader.Read(r, dataOffset);
    SceneReader::BuildHierarchy(pfb);
    return pfb;
}
