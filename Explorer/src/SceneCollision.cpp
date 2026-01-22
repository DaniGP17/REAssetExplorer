#include "Explorer/SceneCollision.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "Core/Assets/Readers/McolReader.h"
#include "Explorer/AsyncLoads.h"
#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"

namespace {

constexpr float PI = 3.14159265f;
constexpr uint32_t NO_GEOMETRY = UINT32_MAX;

// The type dump has no layout for native physics types: object references
// come as 4-byte instance indices and shape parameters as float blocks.
uint32_t ObjectIndex(const RszValue* value) {
    const RszBytes* bytes = value ? value->As<RszBytes>() : nullptr;
    if (bytes == nullptr || bytes->size() != 4) return 0;
    uint32_t index;
    std::memcpy(&index, bytes->data(), 4);
    return index;
}

bool ReadFloats(const RszValue* value, float* out, std::size_t count) {
    const RszBytes* bytes = value ? value->As<RszBytes>() : nullptr;
    if (bytes == nullptr || bytes->size() < count * 4) return false;
    std::memcpy(out, bytes->data(), count * 4);
    return true;
}

const RszInstance* InstanceAt(const RszData& rsz, uint32_t index) {
    return index > 0 && index < rsz.instances.size() ? &rsz.instances[index] : nullptr;
}

class GeometryBuilder {
public:
    explicit GeometryBuilder(SceneCollision& out) : out(out) {
        geometry.firstIndex = static_cast<uint32_t>(out.indices.size());
        geometry.baseVertex = static_cast<uint32_t>(out.positions.size() / 3);
    }

    uint32_t Vertex(const float p[3]) {
        out.positions.insert(out.positions.end(), p, p + 3);
        for (int c = 0; c < 3; c++) {
            geometry.boundsMin[c] = std::min(geometry.boundsMin[c], p[c]);
            geometry.boundsMax[c] = std::max(geometry.boundsMax[c], p[c]);
        }
        return vertexCount++;
    }

    void Triangle(uint32_t a, uint32_t b, uint32_t c) {
        out.indices.insert(out.indices.end(), { a, b, c });
    }

    void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        Triangle(a, b, c);
        Triangle(a, c, d);
    }

    // matrix: row-major, translation in the last row.
    void Box(const float matrix[16], const float halfExtent[3]) {
        uint32_t first = vertexCount;
        for (int k = 0; k < 8; k++) {
            float local[3] = { (k & 1 ? 1.0f : -1.0f) * halfExtent[0], (k & 2 ? 1.0f : -1.0f) * halfExtent[1],
                               (k & 4 ? 1.0f : -1.0f) * halfExtent[2] };
            float p[3];
            for (int c = 0; c < 3; c++) {
                p[c] = local[0] * matrix[c] + local[1] * matrix[4 + c] + local[2] * matrix[8 + c] + matrix[12 + c];
            }
            Vertex(p);
        }
        static const uint32_t FACES[6][4] = {
            { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 },
        };
        for (const auto& f : FACES) Quad(first + f[0], first + f[1], first + f[2], first + f[3]);
    }

    // p0 == p1 gives a sphere.
    void Capsule(const float p0[3], const float p1[3], float radius) {
        float dir[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (length > 1e-6f) {
            for (float& c : dir) c /= length;
        } else {
            dir[0] = 0;
            dir[1] = 1;
            dir[2] = 0;
        }
        float up[3] = { 0, 1, 0 };
        if (std::fabs(dir[1]) > 0.9f) {
            up[0] = 1;
            up[1] = 0;
        }
        float u[3] = { dir[1] * up[2] - dir[2] * up[1], dir[2] * up[0] - dir[0] * up[2], dir[0] * up[1] - dir[1] * up[0] };
        float uLength = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (float& c : u) c /= uLength;
        float v[3] = { dir[1] * u[2] - dir[2] * u[1], dir[2] * u[0] - dir[0] * u[2], dir[0] * u[1] - dir[1] * u[0] };

        constexpr int SEGMENTS = 16;
        constexpr int HALF_RINGS = 5;
        uint32_t previous = UINT32_MAX;
        for (int ring = 0; ring <= 2 * HALF_RINGS + 1; ring++) {
            // Rings from the p0 pole to the p1 pole; the middle two bound the cylinder.
            bool top = ring > HALF_RINGS;
            int k = top ? ring - HALF_RINGS - 1 : HALF_RINGS - ring;
            float angle = 0.5f * PI * static_cast<float>(k) / HALF_RINGS;
            const float* end = top ? p1 : p0;
            float offset = (top ? 1.0f : -1.0f) * radius * std::sin(angle);
            float ringRadius = radius * std::cos(angle);
            uint32_t first = vertexCount;
            for (int s = 0; s < SEGMENTS; s++) {
                float phi = 2 * PI * static_cast<float>(s) / SEGMENTS;
                float p[3];
                for (int c = 0; c < 3; c++) {
                    p[c] = end[c] + dir[c] * offset + (u[c] * std::cos(phi) + v[c] * std::sin(phi)) * ringRadius;
                }
                Vertex(p);
            }
            if (previous != UINT32_MAX) {
                for (int s = 0; s < SEGMENTS; s++) {
                    int n = (s + 1) % SEGMENTS;
                    Quad(previous + s, previous + n, first + n, first + s);
                }
            }
            previous = first;
        }
    }

    uint32_t Finish() {
        geometry.indexCount = static_cast<uint32_t>(out.indices.size()) - geometry.firstIndex;
        if (geometry.indexCount == 0) {
            out.positions.resize(static_cast<std::size_t>(geometry.baseVertex) * 3);
            return NO_GEOMETRY;
        }
        out.geometries.push_back(geometry);
        return static_cast<uint32_t>(out.geometries.size() - 1);
    }

private:
    SceneCollision& out;
    CollisionGeometry geometry;
    uint32_t vertexCount = 0;
};

uint32_t LoadMcol(const LoadedGame& game, const std::string& pakPath, SceneCollision& out, AsyncLoads<TerrainData>* prefetched) {
    auto [it, inserted] = out.geometryByPath.emplace(pakPath, NO_GEOMETRY);
    if (!inserted) return it->second;
    try {
        std::shared_ptr<TerrainData> loaded = prefetched ? prefetched->Take(pakPath) : nullptr;
        it->second = AppendCollisionGeometry(loaded ? *loaded : ReadMcol(game, pakPath), out);
    } catch (const std::exception& e) {
        LogWarning("skip collision mesh %s: %s", pakPath.c_str(), e.what());
    }
    return it->second;
}

void AppendShape(const LoadedGame& game, const RszData& rsz, uint32_t shapeIndex, const Mat4& world, uint32_t filter,
                 const std::string& owner, SceneCollision& out, int depth, AsyncLoads<TerrainData>* prefetched) {
    const RszInstance* shape = InstanceAt(rsz, shapeIndex);
    if (shape == nullptr || depth > 4) return;
    const std::string& type = shape->typeName;
    auto add = [&](uint32_t geometry, const Mat4& m) {
        if (geometry != NO_GEOMETRY) out.colliders.push_back({ geometry, m, filter, owner });
    };

    if (type == "via.physics.MeshShape") {
        const RszValue* pathField = shape->Field("v1");
        std::string path = pathField ? pathField->AsString() : std::string();
        if (path.empty()) return;
        uint32_t geometry = LoadMcol(game, ToPakPath(path, game.Game().GetMcolExt().c_str()), out, prefetched);
        Mat4 local;
        add(geometry, ReadFloats(shape->Field("v2"), local.m, 16) ? Mul(local, world) : world);
    } else if (type == "via.physics.StaticCompoundShape") {
        const RszValue* partsField = shape->Field("v1");
        const RszArray* parts = partsField ? partsField->As<RszArray>() : nullptr;
        if (parts == nullptr) return;
        for (const RszValue& part : *parts) {
            // Each part is a StaticCompoundShape.Instance whose first field is the shape.
            const RszInstance* instance = InstanceAt(rsz, ObjectIndex(&part));
            if (instance != nullptr) {
                AppendShape(game, rsz, ObjectIndex(instance->Field("v0")), world, filter, owner, out, depth + 1, prefetched);
            }
        }
    } else {
        // Primitives: parameters in the game object's space.
        GeometryBuilder builder(out);
        float data[20];
        if (type == "via.physics.BoxShape") {
            // OBB: matrix, then half extents.
            if (ReadFloats(shape->Field("v1"), data, 20)) builder.Box(data, data + 16);
        } else if (type == "via.physics.SphereShape" || type == "via.physics.ContinuousSphereShape") {
            if (ReadFloats(shape->Field("v1"), data, 4)) builder.Capsule(data, data, data[3]);
        } else if (type == "via.physics.CapsuleShape" || type == "via.physics.ContinuousCapsuleShape") {
            // Two float4 end points, then the radius.
            if (ReadFloats(shape->Field("v1"), data, 9)) builder.Capsule(data, data + 4, data[8]);
        } else if (type == "via.physics.AabbShape") {
            // Two float4 corners, min then max.
            if (ReadFloats(shape->Field("v1"), data, 8)) {
                float matrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                float half[3];
                for (int c = 0; c < 3; c++) {
                    matrix[12 + c] = 0.5f * (data[c] + data[4 + c]);
                    half[c] = 0.5f * std::fabs(data[4 + c] - data[c]);
                }
                builder.Box(matrix, half);
            }
        } else if (type == "via.physics.TriangleShape") {
            if (ReadFloats(shape->Field("v1"), data, 12)) {
                uint32_t a = builder.Vertex(data);
                uint32_t b = builder.Vertex(data + 4);
                uint32_t c = builder.Vertex(data + 8);
                builder.Triangle(a, b, c);
            }
        }
        add(builder.Finish(), world);
    }
}

}

uint32_t AppendCollisionGeometry(const TerrainData& mcol, SceneCollision& out) {
    GeometryBuilder builder(out);
    for (const auto& vertex : mcol.vertices) builder.Vertex(vertex.data());
    std::size_t count = mcol.vertices.size();
    for (const TerrTriangle& tri : mcol.triangles) {
        if (tri.vertices[0] >= count || tri.vertices[1] >= count || tri.vertices[2] >= count) continue;
        builder.Triangle(tri.vertices[0], tri.vertices[1], tri.vertices[2]);
    }
    for (const TerrSphere& sphere : mcol.spheres) builder.Capsule(sphere.center, sphere.center, sphere.radius);
    for (const TerrCapsule& capsule : mcol.capsules) builder.Capsule(capsule.p0, capsule.p1, capsule.radius);
    for (const TerrBox& box : mcol.boxes) builder.Box(box.matrix, box.extent);
    return builder.Finish();
}

TerrainData ReadMcol(const LoadedGame& game, const std::string& pakPath) {
    uint32_t version = static_cast<uint32_t>(std::stoul(game.Game().GetMcolExt().substr(1)));
    return game.Readers().Get<McolReader>()->Read(game.ExtractFile(pakPath), version);
}

void AppendColliders(const LoadedGame& game, const RszData& rsz, const RszInstance& component, const Mat4& world,
                     const std::string& owner, SceneCollision& out, AsyncLoads<TerrainData>* prefetched) {
    const RszValue* listField = component.Field("v5");
    const RszArray* list = listField ? listField->As<RszArray>() : nullptr;
    if (list == nullptr) return;
    for (const RszValue& item : *list) {
        const RszInstance* collider = InstanceAt(rsz, ObjectIndex(&item));
        if (collider == nullptr || collider->typeName != "via.physics.Collider") continue;
        const RszValue* filterField = collider->Field("v5");
        std::string cfil = filterField ? filterField->AsString() : std::string();
        auto found = std::find(out.filters.begin(), out.filters.end(), cfil);
        uint32_t filter = static_cast<uint32_t>(found - out.filters.begin());
        if (found == out.filters.end()) out.filters.push_back(cfil);
        AppendShape(game, rsz, ObjectIndex(collider->Field("v2")), world, filter, owner, out, 0, prefetched);
    }
}

std::vector<ColliderInfo> DescribeColliders(const RszData& rsz, const RszInstance& component) {
    std::vector<ColliderInfo> out;
    const RszValue* listField = component.Field("v5");
    const RszArray* list = listField ? listField->As<RszArray>() : nullptr;
    if (list == nullptr) return out;
    for (const RszValue& item : *list) {
        const RszInstance* collider = InstanceAt(rsz, ObjectIndex(&item));
        if (collider == nullptr || collider->typeName != "via.physics.Collider") continue;
        ColliderInfo info;
        const RszValue* filterField = collider->Field("v5");
        info.filter = filterField ? filterField->AsString() : std::string();
        if (const RszInstance* shape = InstanceAt(rsz, ObjectIndex(collider->Field("v2")))) {
            constexpr std::string_view PREFIX = "via.physics.";
            info.shape = shape->typeName.starts_with(PREFIX) ? shape->typeName.substr(PREFIX.size()) : shape->typeName;
            if (shape->typeName == "via.physics.MeshShape") {
                const RszValue* pathField = shape->Field("v1");
                info.mesh = pathField ? pathField->AsString() : std::string();
            } else if (shape->typeName == "via.physics.StaticCompoundShape") {
                const RszValue* partsField = shape->Field("v1");
                const RszArray* parts = partsField ? partsField->As<RszArray>() : nullptr;
                info.shape += " (" + std::to_string(parts ? parts->size() : 0) + " parts)";
            }
        } else {
            info.shape = "(none)";
        }
        out.push_back(std::move(info));
    }
    return out;
}

namespace {

bool Invert(const Mat4& m, Mat4& out) {
    const float* a = m.m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    for (int i = 0; i < 16; i++) out.m[i] = inv[i] / det;
    return true;
}

void AppendVolumes(const RszData& rsz, uint32_t shapeIndex, const Mat4& world, std::vector<CollisionVolume>& out, int depth) {
    const RszInstance* shape = InstanceAt(rsz, shapeIndex);
    if (shape == nullptr || depth > 4) return;
    const std::string& type = shape->typeName;
    float data[20];
    auto box = [&](const float matrix[16], const float half[3]) {
        Mat4 local;
        std::memcpy(local.m, matrix, sizeof(local.m));
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) local.m[r * 4 + c] *= half[r];
        }
        CollisionVolume volume;
        if (Invert(Mul(local, world), volume.worldToBox)) out.push_back(volume);
    };
    auto capsule = [&](const float p0[3], const float p1[3], float radius) {
        CollisionVolume volume;
        volume.kind = CollisionVolume::Kind::Capsule;
        TransformPoint(world, p0, volume.p0);
        TransformPoint(world, p1, volume.p1);
        float scale = std::sqrt(world.m[0] * world.m[0] + world.m[1] * world.m[1] + world.m[2] * world.m[2]);
        volume.radius = radius * scale;
        out.push_back(volume);
    };
    if (type == "via.physics.StaticCompoundShape") {
        const RszValue* partsField = shape->Field("v1");
        const RszArray* parts = partsField ? partsField->As<RszArray>() : nullptr;
        if (parts == nullptr) return;
        for (const RszValue& part : *parts) {
            const RszInstance* instance = InstanceAt(rsz, ObjectIndex(&part));
            if (instance != nullptr) AppendVolumes(rsz, ObjectIndex(instance->Field("v0")), world, out, depth + 1);
        }
    } else if (type == "via.physics.BoxShape") {
        if (ReadFloats(shape->Field("v1"), data, 20)) box(data, data + 16);
    } else if (type == "via.physics.AabbShape") {
        if (ReadFloats(shape->Field("v1"), data, 8)) {
            float matrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            float half[3];
            for (int c = 0; c < 3; c++) {
                matrix[12 + c] = 0.5f * (data[c] + data[4 + c]);
                half[c] = 0.5f * std::fabs(data[4 + c] - data[c]);
            }
            box(matrix, half);
        }
    } else if (type == "via.physics.SphereShape" || type == "via.physics.ContinuousSphereShape") {
        if (ReadFloats(shape->Field("v1"), data, 4)) capsule(data, data, data[3]);
    } else if (type == "via.physics.CapsuleShape" || type == "via.physics.ContinuousCapsuleShape") {
        if (ReadFloats(shape->Field("v1"), data, 9)) capsule(data, data + 4, data[8]);
    }
}

}

std::vector<CollisionVolume> ColliderVolumes(const RszData& rsz, const RszInstance& component, const Mat4& world) {
    std::vector<CollisionVolume> out;
    const RszValue* listField = component.Field("v5");
    const RszArray* list = listField ? listField->As<RszArray>() : nullptr;
    if (list == nullptr) return out;
    for (const RszValue& item : *list) {
        const RszInstance* collider = InstanceAt(rsz, ObjectIndex(&item));
        if (collider == nullptr || collider->typeName != "via.physics.Collider") continue;
        AppendVolumes(rsz, ObjectIndex(collider->Field("v2")), world, out, 0);
    }
    return out;
}

bool VolumeContains(const CollisionVolume& volume, const float point[3]) {
    if (volume.kind == CollisionVolume::Kind::Box) {
        float local[3];
        TransformPoint(volume.worldToBox, point, local);
        return std::fabs(local[0]) <= 1.0f && std::fabs(local[1]) <= 1.0f && std::fabs(local[2]) <= 1.0f;
    }
    float d[3];
    float lengthSq = 0;
    float t = 0;
    for (int c = 0; c < 3; c++) {
        d[c] = volume.p1[c] - volume.p0[c];
        lengthSq += d[c] * d[c];
        t += (point[c] - volume.p0[c]) * d[c];
    }
    t = lengthSq > 0 ? std::clamp(t / lengthSq, 0.0f, 1.0f) : 0.0f;
    float distSq = 0;
    for (int c = 0; c < 3; c++) {
        float e = point[c] - (volume.p0[c] + d[c] * t);
        distSq += e * e;
    }
    return distSq <= volume.radius * volume.radius;
}

std::string CollisionFilterGroup(const std::string& cfilPath) {
    constexpr std::string_view MARKER = "CollisionFilter/";
    std::size_t at = cfilPath.find(MARKER);
    if (at != std::string::npos) {
        std::size_t start = at + MARKER.size();
        std::size_t slash = cfilPath.find('/', start);
        if (slash != std::string::npos) return cfilPath.substr(start, slash - start);
    }
    return "Other";
}
