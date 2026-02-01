#include "Core/Assets/Readers/UvsReader.h"

#include <algorithm>

#include "Core/IO/MemoryReader.h"

UvsData UvsReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0x00) != 0x5556532E) {
        throw std::runtime_error("uvs: bad magic");
    }

    uint32_t textureCount = r.ReadAt<uint32_t>(0x04);
    uint32_t sequenceCount = r.ReadAt<uint32_t>(0x08);
    uint64_t textureOffset = r.ReadAt<uint64_t>(0x18);
    uint64_t sequenceOffset = r.ReadAt<uint64_t>(0x20);
    uint64_t patternOffset = r.ReadAt<uint64_t>(0x28);
    uint64_t stringOffset = r.ReadAt<uint64_t>(0x30);

    UvsData uvs;
    for (uint32_t i = 0; i < textureCount; i++) {
        uint64_t charOffset = r.ReadAt<uint64_t>(textureOffset + i * 40 + 8);
        uvs.textures.push_back(r.ReadWStringAt(stringOffset + charOffset * 2));
    }
    // Patterns follow in sequence order, each trailed by its cutout polygon (cutoutCount float2, -1 = none).
    std::size_t pos = patternOffset;
    for (uint32_t i = 0; i < sequenceCount; i++) {
        UvsSequence seq;
        seq.patternCount = r.ReadAt<uint32_t>(sequenceOffset + i * 8);
        seq.firstPattern = static_cast<uint32_t>(uvs.patterns.size());
        uvs.sequences.push_back(seq);
        for (uint32_t p = 0; p < seq.patternCount; p++) {
            UvsPattern pattern;
            pattern.flags = r.ReadAt<uint32_t>(pos);
            for (int c = 0; c < 4; c++) pattern.rect[c] = r.ReadAt<float>(pos + 8 + c * 4);
            pattern.textureIndex = r.ReadAt<uint32_t>(pos + 24);
            int32_t cutoutCount = std::max(0, r.ReadAt<int32_t>(pos + 28));
            uvs.patterns.push_back(pattern);
            pos += 32 + static_cast<std::size_t>(cutoutCount) * 8;
        }
    }
    return uvs;
}
