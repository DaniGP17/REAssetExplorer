#ifndef REASSETEXPLORER_TEXTUREDECODE_H
#define REASSETEXPLORER_TEXTUREDECODE_H
#include <cstdint>
#include <vector>

#include "Core/Assets/TextureData.h"

// CPU decode of one mip of a texture's first image to top-down RGBA8 (sRGB
// formats keep their encoded values). Handles BC1-BC7, 8-bit RGBA/BGRA/R/RG
// and half-float RGBA; HDR values are clamped. False for other formats.
bool DecodeTextureRgba(const TextureData& tex, uint32_t mip, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height);
// Unclamped linear RGB of one mip, top-down; BC6H and half-float RGBA only.
bool DecodeTextureHdr(const TextureData& tex, uint32_t mip, std::vector<float>& rgb, uint32_t& width, uint32_t& height);

#endif
