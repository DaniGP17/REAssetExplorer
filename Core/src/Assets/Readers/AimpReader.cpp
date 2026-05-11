#include "Core/Assets/Readers/AimpReader.h"

#include <stdexcept>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t AIMP_MAGIC = 0x504D4941;
constexpr int32_t MAX_COUNT = 50'000'000;
constexpr int LAYER_COUNT = 64;

int32_t Count(MemoryReader& r) {
    int32_t count = r.Read<int32_t>();
    if (count < 0 || count > MAX_COUNT) throw std::runtime_error("bad AIMP count");
    return count;
}

std::string InlineString(MemoryReader& r) {
    int32_t count = Count(r);
    std::string text;
    uint16_t pendingHigh = 0;
    for (int32_t i = 0; i < count; i++) {
        uint16_t unit = r.Read<uint16_t>();
        if (unit != 0) MemoryReader::AppendUtf16(text, unit, pendingHigh);
    }
    r.AlignTo(4);
    return text;
}

std::array<float, 3> Vec3(MemoryReader& r) {
    std::array<float, 3> v{};
    for (float& c : v) c = r.Read<float>();
    return v;
}

// Edge attribute bytes, then u32 traverse costs.
void SkipEdges(MemoryReader& r, int32_t count) {
    r.Skip(static_cast<std::size_t>(count));
    r.AlignTo(4);
    r.Skip(static_cast<std::size_t>(count) * 4);
}

AiMapGroup ReadGroup(MemoryReader& r) {
    AiMapGroup group;
    group.className = InlineString(r);
    int32_t count = Count(r);
    const std::string& name = group.className;
    if (name == "via.navigation.map.ContentGroupTriangle") {
        group.kind = AiMapGroup::Kind::Triangle;
        for (int32_t i = 0; i < count; i++) {
            std::array<int32_t, 3> tri{};
            for (int32_t& v : tri) v = r.Read<int32_t>();
            std::array<uint8_t, 3> edges{};
            for (uint8_t& e : edges) e = r.Read<uint8_t>();
            r.AlignTo(4);
            r.Skip(3 * 4);  // traverse costs
            group.triangles.push_back(tri);
            group.triangleEdges.push_back(edges);
        }
    } else if (name == "via.navigation.map.ContentGroupPolygon") {
        group.kind = AiMapGroup::Kind::Polygon;
        for (int32_t i = 0; i < count; i++) {
            int32_t points = Count(r);
            std::vector<int32_t> polygon(static_cast<std::size_t>(points));
            for (int32_t& v : polygon) v = r.Read<int32_t>();
            SkipEdges(r, points);
            r.Skip(24);  // min, max
            group.polygons.push_back(std::move(polygon));
        }
    } else if (name == "via.navigation.map.ContentGroupMapPoint") {
        group.kind = AiMapGroup::Kind::Point;
        for (int32_t i = 0; i < count; i++) {
            group.points.push_back(Vec3(r));
            r.Skip(12);  // normal
        }
    } else if (name == "via.navigation.map.ContentGroupMapBoundary") {
        group.kind = AiMapGroup::Kind::Boundary;
        for (int32_t i = 0; i < count; i++) {
            std::array<int32_t, 8> corners{};
            for (int32_t& v : corners) v = r.Read<int32_t>();
            r.Skip(24);  // min, max
            group.boundaries.push_back(corners);
        }
    } else if (name == "via.navigation.map.ContentGroupMapAABB") {
        group.kind = AiMapGroup::Kind::Box;
        for (int32_t i = 0; i < count; i++) {
            std::array<uint16_t, 4> box{};
            for (uint16_t& v : box) v = r.Read<uint16_t>();
            r.Skip(12 * 4 + 4);  // values, value
            group.boxes.push_back(box);
        }
    } else if (name == "via.navigation.map.ContentGroupWall") {
        group.kind = AiMapGroup::Kind::Wall;
        for (int32_t i = 0; i < count; i++) {
            std::array<float, 16> matrix{};
            for (float& v : matrix) v = r.Read<float>();
            r.Skip(48);  // scale, rotation, position (padded)
            std::array<int32_t, 8> corners{};
            for (int32_t& v : corners) v = r.Read<int32_t>();
            group.walls.push_back(matrix);
            group.wallCorners.push_back(corners);
        }
    } else {
        throw std::runtime_error("unknown AIMP content group " + name);
    }
    return group;
}

void ReadContainer(MemoryReader& r, uint8_t section, AiMapContainer& out) {
    int32_t groups = Count(r);
    for (int32_t i = 0; i < groups; i++) out.groups.push_back(ReadGroup(r));
    r.Skip(4);
    int32_t vertices = Count(r);
    for (int32_t i = 0; i < vertices; i++) {
        out.vertices.push_back(Vec3(r));
        r.Skip(4);
    }
    int32_t nodes = Count(r);
    r.Skip(4);  // max node index
    for (int32_t i = 0; i < nodes; i++) {
        AiMapNode node;
        node.index = r.Read<int32_t>();
        node.group = r.Read<int32_t>();
        node.local = r.Read<int32_t>();
        node.flags = r.Read<int32_t>();
        node.attributes = r.Read<uint64_t>();
        r.Skip(8);  // userdata index, link count
        out.nodes.push_back(node);
    }
    int32_t links = Count(r);
    for (int32_t i = 0; i < links; i++) {
        AiMapLink link;
        r.Skip(4);  // index
        link.source = r.Read<int32_t>();
        link.target = r.Read<int32_t>();
        link.edge = r.Read<int32_t>();
        link.attributes = r.Read<uint64_t>();
        r.Skip(4);
        out.links.push_back(link);
    }
    if (section == 0) r.Skip(4);
    r.Skip(8);  // two unknown floats
    for (float& c : out.boundsMin) c = r.Read<float>();
    r.Skip(4);
    for (float& c : out.boundsMax) c = r.Read<float>();
    out.present = true;
}

}

std::size_t AiMapGroup::NodeCount() const {
    return triangles.size() + polygons.size() + points.size() + boundaries.size() + boxes.size() + walls.size();
}

bool AimpReader::SupportsPath(std::string_view path) const {
    for (std::string_view ext : { ".aimap.", ".ainvm.", ".aiwayp.", ".aivspc." }) {
        if (path.find(ext) != std::string_view::npos) return true;
    }
    return false;
}

AiMapData AimpReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (data.size() < 16 || r.Read<uint32_t>() != AIMP_MAGIC) throw std::runtime_error("not an AI map (no AIMP magic)");
    AiMapData map;
    map.name = InlineString(r);
    map.type = static_cast<AiMapData::Type>(r.Read<uint8_t>());
    map.section = r.Read<uint8_t>();
    r.Skip(2 + 16);  // padding, map id
    map.structure = r.Read<int32_t>();
    uint64_t attributes = r.Read<uint64_t>();
    r.Skip(16);  // rsz, embedded content
    uint64_t mainOffset = r.Read<uint64_t>();
    r.Skip(8);   // group data
    uint64_t secondaryOffset = r.Read<uint64_t>();
    uint64_t secondaryData = r.Read<uint64_t>();

    if (mainOffset > 0) {
        r.Seek(static_cast<std::size_t>(mainOffset));
        ReadContainer(r, map.section, map.main);
    }
    if (secondaryOffset > 0 && secondaryData > 0) {
        r.Seek(static_cast<std::size_t>(secondaryOffset));
        ReadContainer(r, map.section, map.secondary);
    }
    if (attributes > 0) {
        r.Seek(static_cast<std::size_t>(attributes));
        for (int i = 0; i < LAYER_COUNT; i++) {
            AiMapLayer layer;
            layer.name = InlineString(r);
            layer.color = r.Read<uint32_t>();
            map.layers.push_back(std::move(layer));
        }
    }
    return map;
}
