#ifndef REASSETEXPLORER_SCENECOLLISION_H
#define REASSETEXPLORER_SCENECOLLISION_H
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Core/Assets/TerrainData.h"
#include "Core/LoadedGame.h"
#include "Core/Rsz/RszTypes.h"
#include "Renderer/RenderMath.h"

// In the shape's own space. An mcol is stored once and shared by its colliders.
struct CollisionGeometry {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t baseVertex = 0;
    float boundsMin[3] = { 1e9f, 1e9f, 1e9f };
    float boundsMax[3] = { -1e9f, -1e9f, -1e9f };
};

struct SceneCollider {
    uint32_t geometry = 0;
    Mat4 world;
    uint32_t filter = 0;  // index into SceneCollision::filters
    std::string owner;    // SceneObjectKey of its game object
};

struct SceneCollision {
    std::vector<float> positions;  // xyz
    std::vector<uint32_t> indices;
    std::vector<CollisionGeometry> geometries;
    std::vector<SceneCollider> colliders;
    std::vector<std::string> filters;  // .cfil paths as the scene stores them
    std::map<std::string, uint32_t> geometryByPath;  // mcol pak path -> geometry, UINT32_MAX if unreadable
};

template <typename T>
class AsyncLoads;

TerrainData ReadMcol(const LoadedGame& game, const std::string& pakPath);
// component: a via.physics.Colliders. Collision meshes already in prefetched (by pak path) are not read again.
void AppendColliders(const LoadedGame& game, const RszData& rsz, const RszInstance& component, const Mat4& world,
                     const std::string& owner, SceneCollision& out, AsyncLoads<TerrainData>* prefetched = nullptr);
uint32_t AppendCollisionGeometry(const TerrainData& mcol, SceneCollision& out);
struct ColliderInfo {
    std::string shape;   // type without the via.physics. prefix
    std::string mesh;    // MeshShape: the mcol as the scene stores it
    std::string filter;  // .cfil path
};
std::vector<ColliderInfo> DescribeColliders(const RszData& rsz, const RszInstance& component);

// A primitive collider shape in world space, for containment tests.
struct CollisionVolume {
    enum class Kind : uint8_t { Box, Capsule } kind = Kind::Box;
    Mat4 worldToBox;  // Box: world to the box's unit space (half extents divided out)
    float p0[3] = {};  // Capsule: segment and radius; a sphere has p0 == p1
    float p1[3] = {};
    float radius = 0;
};
// component: a via.physics.Colliders. Mesh shapes are left out.
std::vector<CollisionVolume> ColliderVolumes(const RszData& rsz, const RszInstance& component, const Mat4& world);
bool VolumeContains(const CollisionVolume& volume, const float point[3]);
std::string CollisionFilterGroup(const std::string& cfilPath);

#endif
