#include "Core/Assets/Readers/LightProbeReader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t NPRB_MAGIC = 0x4252504E;
constexpr std::size_t NPRB_HEADER = 16;
constexpr std::size_t PROBE_RECORD = 48;

}

ProbeNetworkData ProbeNetworkReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    ProbeNetworkData network;
    uint32_t probeCount = r.Read<uint32_t>();
    network.positions.resize(probeCount);
    for (std::array<float, 3>& position : network.positions) {
        for (float& c : position) c = r.Read<float>();
        r.Read<float>();
    }
    uint32_t tetraCount = r.Read<uint32_t>();
    std::span<const uint8_t> tetras = r.ReadBytes(static_cast<std::size_t>(tetraCount) * sizeof(ProbeTetrahedron));
    network.tetrahedra.resize(tetraCount);
    std::memcpy(network.tetrahedra.data(), tetras.data(), tetras.size());
    if (r.Size() - r.Tell() >= 4) {
        uint32_t bspBytes = r.Read<uint32_t>();
        std::span<const uint8_t> bsp = r.ReadBytes(std::min<std::size_t>(bspBytes, r.Size() - r.Tell()));
        network.bspTree.assign(bsp.begin(), bsp.end());
    }
    return network;
}

LightProbeData LightProbeReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.Read<uint32_t>() != NPRB_MAGIC) throw std::runtime_error("lprb: bad magic");
    uint32_t probeCount = r.Read<uint32_t>();
    LightProbeData probes;
    probes.values.resize(static_cast<std::size_t>(probeCount) * (PROBE_RECORD / 4));
    for (uint32_t i = 0; i < probeCount; i++) {
        uint32_t offset = r.ReadAt<uint32_t>(NPRB_HEADER + static_cast<std::size_t>(i) * 4);
        std::span<const uint8_t> record = r.BytesAt(NPRB_HEADER + offset, PROBE_RECORD);
        std::memcpy(probes.values.data() + static_cast<std::size_t>(i) * (PROBE_RECORD / 4), record.data(), PROBE_RECORD);
    }
    return probes;
}
