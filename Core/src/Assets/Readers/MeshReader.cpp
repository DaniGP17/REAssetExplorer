#include "Core/Assets/Readers/MeshReader.h"

#include "Core/IO/MemoryReader.h"

namespace {
    constexpr uint32_t MESH_VERSION_RE8 = 2020091500;
    constexpr uint32_t MESH_VERSION_RE7RT = 21041600;

    std::array<float, 16> ReadMatrix(const MemoryReader& r, std::size_t offset) {
        std::array<float, 16> m{};
        for (int i = 0; i < 16; i++) {
            m[i] = r.ReadAt<float>(offset + i * 4);
        }
        return m;
    }
}

MeshData MeshReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);

    if (r.ReadAt<uint32_t>(0x00) != 0x4853454D) {
        throw std::runtime_error("mesh: bad magic");
    }

    MeshData mesh;
    mesh.version = r.ReadAt<uint32_t>(0x04);
    if (mesh.version != MESH_VERSION_RE7RT && mesh.version != MESH_VERSION_RE8) {
        throw std::runtime_error("mesh: unsupported version " + std::to_string(mesh.version));
    }

    mesh.lodGroup = r.ReadAt<uint32_t>(0x0C);
    mesh.flags = r.ReadAt<uint8_t>(0x10);
    uint16_t stringCount = r.ReadAt<uint16_t>(0x12);

    uint64_t meshOffset = r.ReadAt<uint64_t>(0x18);
    uint64_t skeletonOffset = r.ReadAt<uint64_t>(0x30);
    uint64_t bufferOffset = r.ReadAt<uint64_t>(0x50);
    uint64_t clusterNamesOffset = r.ReadAt<uint64_t>(0x60);
    uint64_t jointNamesOffset = r.ReadAt<uint64_t>(0x68);
    uint64_t stringTableOffset = r.ReadAt<uint64_t>(0x78);

    std::vector<std::string> strings(stringCount);
    if (stringTableOffset != 0) {
        for (uint16_t i = 0; i < stringCount; i++) {
            strings[i] = r.ReadCStringAt(r.ReadAt<uint64_t>(stringTableOffset + i * 8));
        }
    }

    if (meshOffset != 0) {
        ReadLods(r, mesh, meshOffset);
    }

    if (clusterNamesOffset != 0) {
        for (uint8_t i = 0; i < mesh.totalClusterCount; i++) {
            uint16_t nameIndex = r.ReadAt<uint16_t>(clusterNamesOffset + i * 2);
            mesh.materialNames.push_back(nameIndex < strings.size() ? strings[nameIndex] : std::string());
        }
    }

    if (bufferOffset != 0) {
        ReadBuffers(r, mesh, bufferOffset);
    }

    if (skeletonOffset != 0) {
        ReadSkeleton(r, mesh, skeletonOffset, jointNamesOffset, strings);
    }

    return mesh;
}

void MeshReader::ReadLods(const MemoryReader& r, MeshData& mesh, uint64_t meshOffset) {
    mesh.lodCount = r.ReadAt<uint8_t>(meshOffset);
    mesh.totalClusterCount = r.ReadAt<uint8_t>(meshOffset + 1);
    mesh.uvCount = r.ReadAt<uint8_t>(meshOffset + 2);
    mesh.skinWeightCount = r.ReadAt<uint8_t>(meshOffset + 3);

    mesh.boundingRadius = r.ReadAt<float>(meshOffset + 20);
    for (int i = 0; i < 3; i++) {
        mesh.aabbMin[i] = r.ReadAt<float>(meshOffset + 24 + i * 4);
        mesh.aabbMax[i] = r.ReadAt<float>(meshOffset + 40 + i * 4);
    }

    uint64_t lodTableOffset = r.ReadAt<uint64_t>(meshOffset + 56);

    for (uint8_t i = 0; i < mesh.lodCount; i++) {
        uint64_t lodOffset = r.ReadAt<uint64_t>(lodTableOffset + i * 8);

        MeshLod lod;
        uint8_t partCount = r.ReadAt<uint8_t>(lodOffset);
        lod.vertexFormat = r.ReadAt<uint8_t>(lodOffset + 1);
        lod.lodFactor = r.ReadAt<float>(lodOffset + 4);
        uint64_t partTableOffset = r.ReadAt<uint64_t>(lodOffset + 8);

        for (uint8_t j = 0; j < partCount; j++) {
            uint64_t partOffset = r.ReadAt<uint64_t>(partTableOffset + j * 8);

            MeshPart part;
            part.partId = r.ReadAt<uint8_t>(partOffset);
            uint8_t clusterCount = r.ReadAt<uint8_t>(partOffset + 1);
            part.vertexCount = r.ReadAt<uint32_t>(partOffset + 8);
            part.indexCount = r.ReadAt<uint32_t>(partOffset + 12);

            std::size_t clusterOffset = partOffset + 16;
            for (uint8_t k = 0; k < clusterCount; k++) {
                MeshCluster cluster;
                cluster.materialId = r.ReadAt<uint8_t>(clusterOffset);
                cluster.indexCount = r.ReadAt<uint32_t>(clusterOffset + 4);
                cluster.startIndexLocation = r.ReadAt<uint32_t>(clusterOffset + 8);
                cluster.baseVertexLocation = r.ReadAt<int32_t>(clusterOffset + 12);
                part.clusters.push_back(cluster);
                clusterOffset += 24;
            }

            lod.parts.push_back(std::move(part));
        }

        mesh.lods.push_back(std::move(lod));
    }
}

void MeshReader::ReadBuffers(const MemoryReader& r, MeshData& mesh, uint64_t bufferOffset) {
    uint64_t elementListOffset = r.ReadAt<uint64_t>(bufferOffset);
    uint64_t vertexBufferOffset = r.ReadAt<uint64_t>(bufferOffset + 8);
    uint64_t indexBufferOffset = r.ReadAt<uint64_t>(bufferOffset + 16);

    // The RE7 RT update inserted an extra u64 in the buffer header.
    std::size_t pos = bufferOffset + 24 + (mesh.version == MESH_VERSION_RE7RT ? 8 : 0);
    uint32_t vertexBufferSize = r.ReadAt<uint32_t>(pos);
    uint32_t indexBufferSize = r.ReadAt<uint32_t>(pos + 4);
    uint16_t totalElementCount = r.ReadAt<uint16_t>(pos + 10);

    for (uint16_t i = 0; i < totalElementCount; i++) {
        std::size_t elementOffset = elementListOffset + i * 8;
        VertexStream stream;
        stream.slot = static_cast<VertexStreamSlot>(r.ReadAt<uint16_t>(elementOffset));
        stream.stride = r.ReadAt<uint16_t>(elementOffset + 2);
        stream.offset = r.ReadAt<uint32_t>(elementOffset + 4);
        mesh.streams.push_back(stream);
    }

    auto vb = r.BytesAt(vertexBufferOffset, vertexBufferSize);
    mesh.vertexBuffer.assign(vb.begin(), vb.end());

    auto ib = r.BytesAt(indexBufferOffset, indexBufferSize);
    mesh.indexBuffer.assign(ib.begin(), ib.end());
}

void MeshReader::ReadSkeleton(const MemoryReader& r, MeshData& mesh, uint64_t skeletonOffset, uint64_t jointNamesOffset,
                                 const std::vector<std::string>& strings) {
    uint32_t jointCount = r.ReadAt<uint32_t>(skeletonOffset);
    uint32_t remapCount = r.ReadAt<uint32_t>(skeletonOffset + 4);
    uint64_t jointNodesOffset = r.ReadAt<uint64_t>(skeletonOffset + 16);
    uint64_t localMatricesOffset = r.ReadAt<uint64_t>(skeletonOffset + 24);
    uint64_t worldMatricesOffset = r.ReadAt<uint64_t>(skeletonOffset + 32);
    uint64_t inverseBindMatricesOffset = r.ReadAt<uint64_t>(skeletonOffset + 40);

    for (uint32_t i = 0; i < remapCount; i++) {
        mesh.jointRemap.push_back(r.ReadAt<uint16_t>(skeletonOffset + 48 + i * 2));
    }

    for (uint32_t i = 0; i < jointCount; i++) {
        std::size_t nodeOffset = jointNodesOffset + i * 16;
        MeshJoint joint;
        joint.index = r.ReadAt<uint16_t>(nodeOffset);
        joint.parentIndex = r.ReadAt<uint16_t>(nodeOffset + 2);
        joint.siblingIndex = r.ReadAt<uint16_t>(nodeOffset + 4);
        joint.childIndex = r.ReadAt<uint16_t>(nodeOffset + 6);
        joint.symmetryIndex = r.ReadAt<uint16_t>(nodeOffset + 8);
        joint.localMatrix = ReadMatrix(r, localMatricesOffset + i * 64);
        joint.worldMatrix = ReadMatrix(r, worldMatricesOffset + i * 64);
        joint.inverseBindMatrix = ReadMatrix(r, inverseBindMatricesOffset + i * 64);
        mesh.joints.push_back(std::move(joint));
    }

    if (jointNamesOffset != 0) {
        for (uint32_t i = 0; i < jointCount; i++) {
            uint16_t nameIndex = r.ReadAt<uint16_t>(jointNamesOffset + i * 2);
            mesh.joints[i].name = nameIndex < strings.size() ? strings[nameIndex] : std::string();
        }
    }
}
