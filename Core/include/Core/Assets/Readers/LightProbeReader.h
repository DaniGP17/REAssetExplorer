#ifndef REASSETEXPLORER_LIGHTPROBEREADER_H
#define REASSETEXPLORER_LIGHTPROBEREADER_H
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/Assets/IAssetReader.h"

// Barycentric weights: w[i] = dot(rows[i].xyz, p - origin) with origin = (rows[0].w, rows[1].w, rows[2].w), the position of vertices[3]; w[3] = 1 - w[0] - w[1] - w[2].
struct ProbeTetrahedron {
    uint32_t vertices[4];
    uint32_t neighbors[4];  // across the face opposite vertices[i]; UINT32_MAX on the hull
    float rows[3][4];
};
static_assert(sizeof(ProbeTetrahedron) == 80);

struct ProbeNetworkData {
    std::vector<std::array<float, 3>> positions;
    std::vector<ProbeTetrahedron> tetrahedra;
    std::vector<uint8_t> bspTree;  // IndirectIllumination's BSPTree buffer as stored after its byte size
};

// 12 values per probe, one per icosahedron direction, each three floats packed as IndirectIllumination
// unpacks them: |f16(v << 4)|, f16((v >> 7) & 0x7FFF), |f16(v >> 17)|.
struct LightProbeData {
    std::vector<uint32_t> values;
};

// .prb: probe positions, then the tetrahedralization (a BSP tree follows, unread).
class ProbeNetworkReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".prb.") != std::string_view::npos; }

    ProbeNetworkData Read(std::span<const uint8_t> data) const;
};

// .lprb ("NPRB"): a per-probe offset table into deduplicated 48-byte records.
class LightProbeReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".lprb.") != std::string_view::npos; }

    LightProbeData Read(std::span<const uint8_t> data) const;
};

#endif
