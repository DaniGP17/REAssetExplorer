#ifndef REASSETEXPLORER_MESHDATA_H
#define REASSETEXPLORER_MESHDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class VertexStreamSlot : uint16_t {
    Position = 0,
    NormalTangent = 1,
    Uv0 = 2,
    Uv1 = 3,
    Weights = 4,
    Color = 5
};

struct MeshCluster {
    uint8_t materialId;
    uint32_t indexCount;
    uint32_t startIndexLocation;
    int32_t baseVertexLocation;
};

struct MeshPart {
    uint8_t partId;
    uint32_t vertexCount;
    uint32_t indexCount;
    std::vector<MeshCluster> clusters;
};

struct MeshLod {
    float lodFactor;
    uint8_t vertexFormat;
    std::vector<MeshPart> parts;
};

struct VertexStream {
    VertexStreamSlot slot;
    uint16_t stride;
    uint32_t offset;
};

struct MeshJoint {
    std::string name;
    uint16_t index;
    uint16_t parentIndex;
    uint16_t siblingIndex;
    uint16_t childIndex;
    uint16_t symmetryIndex;
    std::array<float, 16> localMatrix;
    std::array<float, 16> worldMatrix;
    std::array<float, 16> inverseBindMatrix;
};

struct MeshData {
    uint32_t version;
    uint8_t flags;
    uint8_t lodCount;
    uint8_t totalClusterCount;
    uint8_t uvCount;
    uint8_t skinWeightCount;
    std::array<float, 3> aabbMin;
    std::array<float, 3> aabbMax;
    float boundingRadius = 0;
    uint32_t lodGroup = 0;  // header 0x0C: the MeshLodSettings key

    std::vector<MeshLod> lods;
    std::vector<VertexStream> streams;
    std::vector<uint8_t> vertexBuffer;
    std::vector<uint8_t> indexBuffer;
    std::vector<std::string> materialNames;
    std::vector<uint16_t> jointRemap;
    std::vector<MeshJoint> joints;
};

#endif
