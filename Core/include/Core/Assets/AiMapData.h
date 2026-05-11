#ifndef REASSETEXPLORER_AIMAPDATA_H
#define REASSETEXPLORER_AIMAPDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Group shapes index their container's vertices.
struct AiMapGroup {
    enum class Kind { Point, Triangle, Polygon, Boundary, Box, Wall, Unknown };
    Kind kind = Kind::Unknown;
    std::string className;
    // One entry per node, in the vectors of its kind.
    std::vector<std::array<int32_t, 3>> triangles;
    std::vector<std::array<uint8_t, 3>> triangleEdges;  // EdgeAttribute per edge (v0-v1, v1-v2, v2-v0)
    std::vector<std::vector<int32_t>> polygons;
    std::vector<std::array<float, 3>> points;
    std::vector<std::array<int32_t, 8>> boundaries;  // top ring of 4 vertices, then the bottom ring
    std::vector<std::array<uint16_t, 4>> boxes;    // min, max vertex indices (twice)
    std::vector<std::array<float, 16>> walls;      // row-major
    std::vector<std::array<int32_t, 8>> wallCorners;

    std::size_t NodeCount() const;
};

// via.navigation.map.NodeContent.EdgeAttribute
constexpr uint8_t AI_EDGE_CONTOUR = 1;  // outer edge, no neighboring triangle

struct AiMapNode {
    int32_t index = 0;
    int32_t group = 0;
    int32_t local = 0;
    int32_t flags = 0;
    uint64_t attributes = 0;  // bit i: layer i
};

struct AiMapLink {
    int32_t source = 0;  // node index fields (AiMapNode::index), not list positions
    int32_t target = 0;
    int32_t edge = 0;
    uint64_t attributes = 0;
};

struct AiMapContainer {
    std::vector<AiMapGroup> groups;
    std::vector<std::array<float, 3>> vertices;
    std::vector<AiMapNode> nodes;
    std::vector<AiMapLink> links;
    float boundsMin[3]{};
    float boundsMax[3]{};
    bool present = false;
};

struct AiMapLayer {
    std::string name;
    uint32_t color = 0xFFFFFFFF;  // RGBA8, R in the low byte
};

struct AiMapData {
    enum class Type : uint8_t { Navmesh = 0, Waypoint = 1, VolumeSpace = 2, NoMap = 3 };
    std::string name;
    Type type = Type::NoMap;
    uint8_t section = 0;
    int32_t structure = 0;
    AiMapContainer main;
    AiMapContainer secondary;  // navmesh polygons and their boxes
    std::vector<AiMapLayer> layers;
};

#endif
