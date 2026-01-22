#ifndef REASSETEXPLORER_TERRAINDATA_H
#define REASSETEXPLORER_TERRAINDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using TerrGuid = std::array<uint8_t, 16>;

struct TerrTriangle {
    uint32_t vertices[3]{};
    int32_t layer = 0;
};

struct TerrSphere {
    int32_t layer = 0;
    float center[3]{};
    float radius = 0;
};

struct TerrCapsule {
    int32_t layer = 0;
    float p0[3]{};
    float p1[3]{};
    float radius = 0;
};

struct TerrBox {
    int32_t layer = 0;
    float matrix[16]{};  // row-major, translation in the last row
    // Half size (centre to face); matches the render mesh.
    float extent[3]{};
};

struct TerrLayerName {
    std::string main;
    std::string sub;
};

struct TerrType {
    TerrGuid guid{};
    std::vector<TerrGuid> guids1;
    std::vector<TerrGuid> guids2;
};

struct TerrainData {
    uint32_t version = 0;
    TerrGuid guid{};
    bool hasBvh = false;  // some files are a bare header
    float boundsMin[3]{};
    float boundsMax[3]{};
    std::vector<std::array<float, 3>> vertices;
    std::vector<TerrTriangle> triangles;
    std::vector<TerrSphere> spheres;
    std::vector<TerrCapsule> capsules;
    std::vector<TerrBox> boxes;
    std::vector<TerrLayerName> layerNames;
    std::vector<TerrType> types;
    std::vector<TerrGuid> guids;
};

#endif
