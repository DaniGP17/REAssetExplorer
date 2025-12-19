#include "Core/Assets/Readers/MeshLodSettingsReader.h"

#include <algorithm>
#include <stdexcept>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t LOD_MAGIC = 0x444F4C;  // "LOD"

}

const MeshLodParameter* MeshLodSettings::Find(uint32_t key, uint32_t lodCount) const {
    auto entry = parameters.find(key);
    if (entry == parameters.end()) entry = parameters.find(0);
    if (entry == parameters.end()) return nullptr;
    std::size_t index = lodCount > 1 ? std::min<std::size_t>(lodCount - 2, entry->second.size() - 1) : 0;
    const MeshLodParameter& parameter = entry->second[index];
    return parameter.present ? &parameter : nullptr;
}

MeshLodSettings MeshLodSettingsReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.Read<uint32_t>() != LOD_MAGIC) throw std::runtime_error("lod settings: bad magic");
    r.Read<uint32_t>();
    uint32_t count = r.Read<uint32_t>() + 1;

    MeshLodSettings settings;
    for (uint32_t e = 0; e < count; e++) {
        uint32_t mask = r.Read<uint32_t>();
        r.Read<uint32_t>();
        uint32_t key = r.Read<uint32_t>();
        std::array<MeshLodParameter, 7>& entry = settings.parameters[key];
        for (uint32_t bit = 0; bit < entry.size(); bit++) {
            if (!(mask & (1u << bit))) continue;
            MeshLodParameter& parameter = entry[bit];
            parameter.present = true;
            parameter.occupancyMin = r.Read<float>();
            parameter.occupancyMax = r.Read<float>();
            for (uint32_t i = 1; i < bit; i++) parameter.occupancyRates.push_back(r.Read<float>());
        }
    }
    return settings;
}
