#ifndef REASSETEXPLORER_TEXTUREDATA_H
#define REASSETEXPLORER_TEXTUREDATA_H
#include <cstdint>
#include <vector>

struct TextureMip {
    uint64_t offset;
    uint32_t pitch;
    uint32_t size;
    std::vector<uint8_t> data;
};

struct TextureData {
    uint32_t version;
    uint16_t width;
    uint16_t height;
    uint16_t depthAndType;
    uint16_t mipInfo;
    uint32_t format;
    uint64_t layoutFlags;
    uint32_t streamingFlags;
    uint32_t dataSizeTotal;
    uint16_t tileMode;
    uint16_t alignment;
    std::vector<TextureMip> mips;

    uint32_t NumImages() const { return mipInfo & 0x0FFF; }
    uint32_t MipsPerImage() const { return mipInfo >> 12; }
    // The _ALPG effect textures: primitivevfx reads their alpha as a^4.84 and scales the Physical
    // blend's emissive term by 2^EV (the game's cbEvPow2).
    bool AlphaGamma() const { return (streamingFlags & 0x800) != 0; }
    float EffectEv() const { return AlphaGamma() ? static_cast<float>(streamingFlags & 0x7F) * 0.5f : 0.0f; }
};

#endif
