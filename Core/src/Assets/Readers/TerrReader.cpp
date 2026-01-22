#include "Core/Assets/Readers/TerrReader.h"

#include <cstring>
#include <string>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t TERR_MAGIC = 0x52524554;
constexpr uint32_t BVH_MAGIC = 0x4D485642;
constexpr uint32_t MIN_VERSION = 10008;
constexpr uint32_t PRAGMATA_VERSION = 25024;  // adds a header field this reader does not handle
constexpr std::size_t HEADER_SIZE = 48;

TerrGuid ReadGuid(const MemoryReader& r, std::size_t offset) {
    TerrGuid guid;
    std::memcpy(guid.data(), r.BytesAt(offset, 16).data(), 16);
    return guid;
}

std::vector<TerrGuid> ReadGuids(const MemoryReader& r, uint64_t offset, int32_t count) {
    std::vector<TerrGuid> guids;
    for (int32_t i = 0; i < count; i++) guids.push_back(ReadGuid(r, offset + i * 16));
    return guids;
}

// Triangle record: info (ukn, layer, +mask from 13008, +part from 20021),
// seven unused ints, three vertex indices, three edge links, and a trailing
// int up to 10019.
std::size_t TriangleInfoSize(uint32_t version) {
    return version >= 20021 ? 16 : version >= 13008 ? 12 : 8;
}

std::size_t TriangleSize(uint32_t version) {
    return TriangleInfoSize(version) + 7 * 4 + 6 * 4 + (version <= 10019 ? 4 : 0);
}

void ReadBvh(const MemoryReader& r, uint32_t version, TerrainData& terr) {
    if (r.ReadAt<uint32_t>(0) != BVH_MAGIC) throw std::runtime_error("terr: bad BVH magic");
    int32_t vertexCount = r.ReadAt<int32_t>(4);
    int32_t triangleCount = r.ReadAt<int32_t>(8);
    int32_t stringCount = r.ReadAt<int32_t>(16);
    int32_t sphereCount = r.ReadAt<int32_t>(20);
    int32_t capsuleCount = r.ReadAt<int32_t>(24);
    int32_t boxCount = r.ReadAt<int32_t>(28);
    int32_t treeSize = r.ReadAt<int32_t>(36);
    uint64_t treeOffset = r.ReadAt<uint64_t>(40);
    uint64_t vertexOffset = r.ReadAt<uint64_t>(48);
    uint64_t triangleOffset = r.ReadAt<uint64_t>(56);
    uint64_t stringOffset = r.ReadAt<uint64_t>(72);
    uint64_t sphereOffset = r.ReadAt<uint64_t>(80);
    uint64_t capsuleOffset = r.ReadAt<uint64_t>(88);
    uint64_t boxOffset = r.ReadAt<uint64_t>(96);
    if (vertexCount < 0 || triangleCount < 0 || stringCount < 0 || sphereCount < 0 || capsuleCount < 0 || boxCount < 0) {
        throw std::runtime_error("terr: negative BVH count");
    }

    terr.hasBvh = true;
    // The root AABB sits just before the tree nodes.
    if (treeSize > 0 && treeOffset >= 32) {
        for (int c = 0; c < 3; c++) {
            terr.boundsMin[c] = r.ReadAt<float>(treeOffset - 32 + c * 4);
            terr.boundsMax[c] = r.ReadAt<float>(treeOffset - 16 + c * 4);
        }
    }

    terr.vertices.reserve(vertexCount);
    for (int32_t i = 0; i < vertexCount; i++) {
        std::size_t pos = vertexOffset + i * 16;
        terr.vertices.push_back({ r.ReadAt<float>(pos), r.ReadAt<float>(pos + 4), r.ReadAt<float>(pos + 8) });
    }

    std::size_t stride = TriangleSize(version);
    std::size_t indexPos = TriangleInfoSize(version) + 7 * 4;
    terr.triangles.reserve(triangleCount);
    for (int32_t i = 0; i < triangleCount; i++) {
        std::size_t pos = triangleOffset + i * stride;
        TerrTriangle tri;
        tri.layer = r.ReadAt<int32_t>(pos + 4);
        for (int k = 0; k < 3; k++) tri.vertices[k] = r.ReadAt<uint32_t>(pos + indexPos + k * 4);
        terr.triangles.push_back(tri);
    }

    for (int32_t i = 0; i < sphereCount; i++) {
        std::size_t pos = sphereOffset + i * 32;
        TerrSphere s;
        s.layer = r.ReadAt<int32_t>(pos + 4);
        for (int c = 0; c < 3; c++) s.center[c] = r.ReadAt<float>(pos + 16 + c * 4);
        s.radius = r.ReadAt<float>(pos + 28);
        terr.spheres.push_back(s);
    }
    for (int32_t i = 0; i < capsuleCount; i++) {
        std::size_t pos = capsuleOffset + i * 64;
        TerrCapsule c;
        c.layer = r.ReadAt<int32_t>(pos + 4);
        for (int k = 0; k < 3; k++) {
            c.p0[k] = r.ReadAt<float>(pos + 16 + k * 4);
            c.p1[k] = r.ReadAt<float>(pos + 32 + k * 4);
        }
        c.radius = r.ReadAt<float>(pos + 48);
        terr.capsules.push_back(c);
    }
    for (int32_t i = 0; i < boxCount; i++) {
        std::size_t pos = boxOffset + i * 96;
        TerrBox b;
        b.layer = r.ReadAt<int32_t>(pos + 4);
        for (int k = 0; k < 16; k++) b.matrix[k] = r.ReadAt<float>(pos + 16 + k * 4);
        for (int k = 0; k < 3; k++) b.extent[k] = r.ReadAt<float>(pos + 80 + k * 4);
        terr.boxes.push_back(b);
    }

    for (int32_t i = 0; i < stringCount; i++) {
        uint64_t mainOffset = r.ReadAt<uint64_t>(stringOffset + i * 16);
        uint64_t subOffset = r.ReadAt<uint64_t>(stringOffset + i * 16 + 8);
        TerrLayerName name;
        if (mainOffset != 0) name.main = r.ReadWStringAt(mainOffset);
        if (subOffset != 0) name.sub = r.ReadWStringAt(subOffset);
        terr.layerNames.push_back(std::move(name));
    }
}

}

void ReadCollisionBvh(std::span<const uint8_t> bvh, uint32_t version, TerrainData& out) {
    ReadBvh(MemoryReader(bvh), version, out);
}

TerrainData TerrReader::Read(std::span<const uint8_t> data, uint32_t version) const {
    if (version < MIN_VERSION || version >= PRAGMATA_VERSION) {
        throw std::runtime_error("terr: unsupported version " + std::to_string(version));
    }
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != TERR_MAGIC) throw std::runtime_error("terr: bad magic");

    TerrainData terr;
    terr.version = version;
    int32_t typeCount = r.ReadAt<int32_t>(8);
    int32_t guidCount = r.ReadAt<int32_t>(12);
    terr.guid = ReadGuid(r, 16);
    uint64_t typesOffset = r.ReadAt<uint64_t>(32);
    uint64_t guidsOffset = r.ReadAt<uint64_t>(40);
    if (typeCount < 0 || guidCount < 0) throw std::runtime_error("terr: negative count");

    if (data.size() > HEADER_SIZE) {
        ReadBvh(MemoryReader(data.subspan(HEADER_SIZE)), version, terr);
    }

    // Type records use absolute offsets, unlike the BVH.
    if (typesOffset != 0) {
        for (int32_t i = 0; i < typeCount; i++) {
            std::size_t pos = typesOffset + i * 48;
            TerrType type;
            type.guid = ReadGuid(r, pos + 16);
            type.guids1 = ReadGuids(r, r.ReadAt<uint64_t>(pos + 32), r.ReadAt<int32_t>(pos + 8));
            type.guids2 = ReadGuids(r, r.ReadAt<uint64_t>(pos + 40), r.ReadAt<int32_t>(pos + 12));
            terr.types.push_back(std::move(type));
        }
    }
    if (guidsOffset != 0) terr.guids = ReadGuids(r, guidsOffset, guidCount);
    return terr;
}
