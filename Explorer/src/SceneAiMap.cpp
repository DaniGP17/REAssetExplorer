#include "Explorer/SceneAiMap.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "Core/Assets/Readers/AimpReader.h"
#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"

namespace {

// Navmeshes are built on the floor they cover: raised a little so the depth
// test against the render meshes does not fade them.
constexpr float LIFT = 0.04f;
constexpr float POINT_SIZE = 0.25f;
constexpr float FACE_ALPHA = 0.4f;
constexpr float INNER_EDGE_ALPHA = 0.2f;
constexpr float POLYGON_ALPHA = 0.55f;
constexpr float VOLUME_FACE_ALPHA = 0.05f;
constexpr float VOLUME_LINK_ALPHA = 0.3f;
constexpr float SHAPE_FACE_ALPHA = 0.25f;

uint32_t Tint(uint32_t rgba, float scale, float alpha) {
    auto channel = [&](int shift) {
        float v = static_cast<float>((rgba >> shift) & 0xFF) * scale;
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f) << shift;
    };
    return channel(0) | channel(8) | channel(16) | static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24;
}

class Writer {
public:
    explicit Writer(DebugOverlay& out) : out(out) {}

    void Line(const float p[3], const float q[3], uint32_t color) {
        Push(out.lines, p, color);
        Push(out.lines, q, color);
    }

    void Triangle(const float a[3], const float b[3], const float c[3], uint32_t color) {
        Push(out.triangles, a, color);
        Push(out.triangles, b, color);
        Push(out.triangles, c, color);
    }

    void Quad(const float a[3], const float b[3], const float c[3], const float d[3], uint32_t color) {
        Triangle(a, b, c, color);
        Triangle(a, c, d, color);
    }

    // Top ring 0-3, bottom ring 4-7.
    void Prism(const float corners[8][3], uint32_t edge, uint32_t face) {
        for (int k = 0; k < 4; k++) {
            int n = (k + 1) % 4;
            Line(corners[k], corners[n], edge);
            Line(corners[4 + k], corners[4 + n], edge);
            Line(corners[k], corners[4 + k], edge);
            if (face >> 24) Quad(corners[k], corners[n], corners[4 + n], corners[4 + k], face);
        }
        if (face >> 24) {
            Quad(corners[0], corners[1], corners[2], corners[3], face);
            Quad(corners[4], corners[5], corners[6], corners[7], face);
        }
    }

    void Box(const float lo[3], const float hi[3], uint32_t edge, uint32_t face) {
        float corners[8][3];
        static const int RING[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 8; k++) {
            corners[k][0] = RING[k % 4][0] ? hi[0] : lo[0];
            corners[k][1] = k < 4 ? hi[1] : lo[1];
            corners[k][2] = RING[k % 4][1] ? hi[2] : lo[2];
        }
        Prism(corners, edge, face);
    }

    void Diamond(const float c[3], float size, uint32_t color) {
        float tips[6][3];
        for (int k = 0; k < 6; k++) {
            std::copy(c, c + 3, tips[k]);
            tips[k][k / 2] += k % 2 ? -size : size;
        }
        for (int a = 0; a < 6; a++) {
            for (int b = a + 1; b < 6; b++) {
                if (a / 2 != b / 2) Line(tips[a], tips[b], color);
            }
        }
    }

private:
    void Push(std::vector<ViewerDebugVertex>& list, const float p[3], uint32_t color) {
        list.push_back({ { p[0], p[1], p[2] }, color });
        for (int axis = 0; axis < 3; axis++) {
            out.boundsMin[axis] = std::min(out.boundsMin[axis], p[axis]);
            out.boundsMax[axis] = std::max(out.boundsMax[axis], p[axis]);
        }
    }

    DebugOverlay& out;
};

const RszInstance* InstanceAt(const RszData& rsz, const RszValue& value) {
    const RszBytes* bytes = value.As<RszBytes>();
    if (bytes == nullptr || bytes->size() != 4) return nullptr;
    uint32_t index;
    std::memcpy(&index, bytes->data(), 4);
    return index > 0 && index < rsz.instances.size() ? &rsz.instances[index] : nullptr;
}

void AppendContainer(const AiMapData& map, const AiMapContainer& container, bool secondary, const AiMapStyle& style,
                     Writer& out) {
    auto hidden = [&](const std::string& key) { return style.hidden && style.hidden->contains(key); };
    auto vertex = [&](int32_t index, float p[3]) {
        const auto& v = container.vertices[static_cast<std::size_t>(index)];
        p[0] = v[0];
        p[1] = v[1] + LIFT;
        p[2] = v[2];
    };
    // Node attributes by group and local index; the links go by node index.
    std::vector<std::vector<uint64_t>> attributes(container.groups.size());
    for (std::size_t g = 0; g < container.groups.size(); g++) attributes[g].assign(container.groups[g].NodeCount(), 0);
    std::unordered_map<int32_t, const AiMapNode*> nodeByIndex;
    for (const AiMapNode& node : container.nodes) {
        nodeByIndex.emplace(node.index, &node);
        if (node.group >= 0 && static_cast<std::size_t>(node.group) < attributes.size() && node.local >= 0 &&
            static_cast<std::size_t>(node.local) < attributes[node.group].size()) {
            attributes[node.group][node.local] = node.attributes;
        }
    }
    auto colorOf = [&](uint64_t bits) {
        int layer = AiMapNodeLayer(bits);
        return layer >= 0 && static_cast<std::size_t>(layer) < map.layers.size() ? map.layers[layer].color | 0xFF000000u
                                                                                 : style.color;
    };
    std::array<bool, 65> hiddenLayers{};  // layer + 1
    for (int layer = -1; layer < 64; layer++) hiddenLayers[layer + 1] = hidden(AiMapLayerKey(layer));
    auto layerHidden = [&](uint64_t bits) { return hiddenLayers[AiMapNodeLayer(bits) + 1]; };
    std::vector<bool> groupShown(container.groups.size());
    for (std::size_t g = 0; g < container.groups.size(); g++) groupShown[g] = !hidden(AiMapGroupKey(secondary, g));

    const float light[3] = { 0.30f, 0.85f, 0.43f };
    for (std::size_t g = 0; g < container.groups.size(); g++) {
        const AiMapGroup& group = container.groups[g];
        if (!groupShown[g]) continue;
        const std::vector<uint64_t>& bits = attributes[g];
        for (std::size_t i = 0; i < group.triangles.size(); i++) {
            if (layerHidden(bits[i])) continue;
            const auto& tri = group.triangles[i];
            float p[3][3];
            for (int k = 0; k < 3; k++) vertex(tri[k], p[k]);
            float e1[3] = { p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2] };
            float e2[3] = { p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2] };
            float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            float shade = length > 0 ? 0.55f + 0.45f * std::fabs(n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / length : 1.0f;
            uint32_t color = colorOf(bits[i]);
            out.Triangle(p[0], p[1], p[2], Tint(color, shade, FACE_ALPHA));
            for (int k = 0; k < 3; k++) {
                bool contour = (group.triangleEdges[i][k] & AI_EDGE_CONTOUR) != 0;
                // Inner edges are shared: drawn once, from the lower vertex index.
                if (!contour && tri[k] > tri[(k + 1) % 3]) continue;
                out.Line(p[k], p[(k + 1) % 3], contour ? Tint(color, 1.2f, 0.95f) : Tint(color, 1.0f, INNER_EDGE_ALPHA));
            }
        }
        for (std::size_t i = 0; i < group.polygons.size(); i++) {
            if (layerHidden(bits[i])) continue;
            const std::vector<int32_t>& polygon = group.polygons[i];
            uint32_t color = Tint(colorOf(bits[i]), 1.1f, POLYGON_ALPHA);
            for (std::size_t k = 0; k < polygon.size(); k++) {
                float p[3];
                float q[3];
                vertex(polygon[k], p);
                vertex(polygon[(k + 1) % polygon.size()], q);
                out.Line(p, q, color);
            }
        }
        for (std::size_t i = 0; i < group.points.size(); i++) {
            if (layerHidden(bits[i])) continue;
            float p[3] = { group.points[i][0], group.points[i][1] + LIFT, group.points[i][2] };
            out.Diamond(p, POINT_SIZE, Tint(colorOf(bits[i]), 1.2f, 1.0f));
        }
        for (std::size_t i = 0; i < group.boundaries.size(); i++) {
            if (layerHidden(bits[i])) continue;
            float corners[8][3];
            for (int k = 0; k < 8; k++) vertex(group.boundaries[i][k], corners[k]);
            uint32_t color = colorOf(bits[i]);
            out.Prism(corners, Tint(color, 1.2f, 0.95f), Tint(color, 1.0f, SHAPE_FACE_ALPHA));
        }
        for (std::size_t i = 0; i < group.boxes.size(); i++) {
            if (layerHidden(bits[i])) continue;
            const auto& lo = container.vertices[group.boxes[i][0]];
            const auto& hi = container.vertices[group.boxes[i][1]];
            uint32_t color = colorOf(bits[i]);
            out.Box(lo.data(), hi.data(), Tint(color, 1.1f, 0.7f), Tint(color, 1.0f, VOLUME_FACE_ALPHA));
        }
    }

    if (hidden(AiMapLinksKey(secondary))) return;
    auto center = [&](const AiMapNode& node, float c[3]) {
        if (node.group < 0 || static_cast<std::size_t>(node.group) >= container.groups.size() || !groupShown[node.group]) return false;
        const AiMapGroup& group = container.groups[node.group];
        auto local = static_cast<std::size_t>(node.local);
        if (local >= group.NodeCount() || layerHidden(node.attributes)) return false;
        std::vector<int32_t> corners;
        if (local < group.points.size()) {
            c[0] = group.points[local][0];
            c[1] = group.points[local][1] + LIFT;
            c[2] = group.points[local][2];
            return true;
        }
        if (local < group.triangles.size()) corners.assign(group.triangles[local].begin(), group.triangles[local].end());
        else if (local < group.polygons.size()) corners = group.polygons[local];
        else if (local < group.boundaries.size()) corners.assign(group.boundaries[local].begin(), group.boundaries[local].end());
        else if (local < group.boxes.size()) corners = { group.boxes[local][0], group.boxes[local][1] };
        if (corners.empty()) return false;
        std::fill(c, c + 3, 0.0f);
        for (int32_t index : corners) {
            float p[3];
            vertex(index, p);
            for (int k = 0; k < 3; k++) c[k] += p[k] / static_cast<float>(corners.size());
        }
        return true;
    };
    std::set<std::pair<int32_t, int32_t>> drawn;
    for (const AiMapLink& link : container.links) {
        auto source = nodeByIndex.find(link.source);
        auto target = nodeByIndex.find(link.target);
        if (source == nodeByIndex.end() || target == nodeByIndex.end()) continue;
        const AiMapGroup& from = container.groups[source->second->group];
        const AiMapGroup& to = container.groups[target->second->group];
        // Neighboring faces of one mesh: their shared edges already show it.
        bool faces = from.kind == AiMapGroup::Kind::Triangle || from.kind == AiMapGroup::Kind::Polygon;
        if (faces && from.kind == to.kind) continue;
        if (!drawn.emplace(std::min(link.source, link.target), std::max(link.source, link.target)).second) continue;
        float p[3];
        float q[3];
        if (!center(*source->second, p) || !center(*target->second, q)) continue;
        bool volume = from.kind == AiMapGroup::Kind::Box && to.kind == AiMapGroup::Kind::Box;
        out.Line(p, q, Tint(style.color, 1.3f, volume ? VOLUME_LINK_ALPHA : 0.95f));
    }
}

}

std::vector<std::string> AiMapReferences(const RszData& rsz, const RszInstance& component) {
    std::vector<std::string> paths;
    const RszValue* handlesField = component.Field("v0");
    const RszArray* handles = handlesField ? handlesField->As<RszArray>() : nullptr;
    if (handles == nullptr) return paths;
    for (const RszValue& item : *handles) {
        // Native type: the MapHandle comes as its instance index; v10 is the map resource.
        const RszInstance* handle = InstanceAt(rsz, item);
        if (handle == nullptr || handle->typeName != "via.navigation.MapHandle") continue;
        const RszValue* pathField = handle->Field("v10");
        std::string path = pathField ? pathField->AsString() : std::string();
        if (!path.empty()) paths.push_back(std::move(path));
    }
    return paths;
}

std::string AiMapPakPath(const IGame& game, const std::string& path) {
    std::size_t dot = path.rfind('.');
    std::string type = dot == std::string::npos ? std::string() : path.substr(dot);
    for (char& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string version = game.GetAiMapExt(type);
    return version.empty() ? std::string() : ToPakPath(path, version.c_str());
}

void AppendAiMaps(const LoadedGame& game, const RszData& rsz, const RszInstance& component, const std::string& owner,
                  std::vector<SceneAiMap>& out) {
    const AimpReader* reader = game.Readers().Get<AimpReader>();
    if (reader == nullptr) return;
    for (const std::string& path : AiMapReferences(rsz, component)) {
        std::string pakPath = AiMapPakPath(game.Game(), path);
        if (pakPath.empty()) continue;
        auto same = [&](const SceneAiMap& map) { return map.path == pakPath; };
        if (std::any_of(out.begin(), out.end(), same)) continue;
        try {
            auto data = std::make_shared<const AiMapData>(reader->Read(game.ExtractFile(pakPath)));
            out.push_back({ pakPath, owner, std::move(data) });
        } catch (const std::exception& e) {
            LogWarning("skip AI map %s: %s", pakPath.c_str(), e.what());
        }
    }
}

const char* AiMapTypeName(AiMapData::Type type) {
    switch (type) {
    case AiMapData::Type::Navmesh: return "Navmesh";
    case AiMapData::Type::Waypoint: return "Waypoints";
    case AiMapData::Type::VolumeSpace: return "Volume space";
    default: return "No map";
    }
}

std::string AiMapGroupName(const AiMapGroup& group) {
    constexpr std::string_view PREFIX = "via.navigation.map.ContentGroup";
    return group.className.starts_with(PREFIX) ? group.className.substr(PREFIX.size()) : group.className;
}

std::string AiMapGroupKey(bool secondary, std::size_t group) {
    return std::string(secondary ? "aigroup:secondary:" : "aigroup:main:") + std::to_string(group);
}

std::string AiMapLinksKey(bool secondary) {
    return secondary ? "ailinks:secondary" : "ailinks:main";
}

std::string AiMapLayerKey(int layer) {
    return layer < 0 ? "ailayer:none" : "ailayer:" + std::to_string(layer);
}

int AiMapNodeLayer(uint64_t attributes) {
    return attributes == 0 ? -1 : std::countr_zero(attributes);
}

void AppendAiMapOverlay(const AiMapData& map, const AiMapStyle& style, DebugOverlay& overlay) {
    Writer out(overlay);
    if (map.main.present) AppendContainer(map, map.main, false, style, out);
    if (map.secondary.present) AppendContainer(map, map.secondary, true, style, out);
}
