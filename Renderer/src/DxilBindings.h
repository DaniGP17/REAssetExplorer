#ifndef REASSETEXPLORER_DXILBINDINGS_H
#define REASSETEXPLORER_DXILBINDINGS_H
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// Bindings from the PSV0 part of a DXIL container.
// type: 1=sampler 2=CBV 3=typed SRV 4=raw SRV 5=structured SRV 6+=UAV.
struct DxilBinding {
    uint32_t type;
    uint32_t space;
    uint32_t reg;
};

inline std::vector<DxilBinding> ParseDxilBindings(std::span<const uint8_t> blob) {
    std::vector<DxilBinding> result;
    if (blob.size() < 32) return result;
    auto u32 = [&](std::size_t offset) {
        uint32_t value;
        std::memcpy(&value, blob.data() + offset, 4);
        return value;
    };
    uint32_t partCount = u32(28);
    for (uint32_t i = 0; i < partCount; i++) {
        uint32_t off = u32(32 + 4 * i);
        if (std::memcmp(blob.data() + off, "PSV0", 4) != 0) continue;
        std::size_t p = off + 8;
        uint32_t runtimeInfoSize = u32(p);
        p += 4 + runtimeInfoSize;
        uint32_t resCount = u32(p);
        p += 4;
        if (resCount == 0) break;
        uint32_t bindInfoSize = u32(p);
        p += 4;
        for (uint32_t r = 0; r < resCount; r++) {
            std::size_t base = p + r * bindInfoSize;
            result.push_back({ u32(base), u32(base + 4), u32(base + 8) });
        }
        break;
    }
    return result;
}

#endif
