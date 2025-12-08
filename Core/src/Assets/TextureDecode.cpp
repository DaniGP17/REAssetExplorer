#include "Core/Assets/TextureDecode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace {

float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 1;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;
    float value;
    if (exponent == 0) value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31) value = mantissa ? 0.0f : 65504.0f;
    else value = std::ldexp(static_cast<float>(mantissa | 0x400), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

uint8_t ToByte(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

}

bool DecodeTextureRgba(const TextureData& tex, uint32_t mip, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
    if (mip >= tex.mips.size()) return false;
    width = std::max<uint32_t>(tex.width >> mip, 1);
    height = std::max<uint32_t>(tex.height >> mip, 1);
    const TextureMip& level = tex.mips[mip];
    const uint8_t* data = level.data.data();
    std::size_t size = level.data.size();
    out.assign(static_cast<std::size_t>(width) * height * 4, 0);

    auto blocks = [&](uint32_t blockBytes, auto decode) {
        uint32_t across = (width + 3) / 4;
        uint32_t down = (height + 3) / 4;
        uint32_t pitch = level.pitch ? level.pitch : across * blockBytes;
        uint8_t tile[4 * 4 * 4];
        for (uint32_t by = 0; by < down; by++) {
            for (uint32_t bx = 0; bx < across; bx++) {
                std::size_t at = static_cast<std::size_t>(by) * pitch + static_cast<std::size_t>(bx) * blockBytes;
                if (at + blockBytes > size) return;
                decode(data + at, tile);
                for (uint32_t y = 0; y < 4 && by * 4 + y < height; y++) {
                    for (uint32_t x = 0; x < 4 && bx * 4 + x < width; x++) {
                        std::memcpy(&out[((by * 4 + y) * static_cast<std::size_t>(width) + bx * 4 + x) * 4], &tile[(y * 4 + x) * 4], 4);
                    }
                }
            }
        }
    };
    auto rows = [&](uint32_t bytes, auto pixel) {
        uint32_t pitch = level.pitch ? level.pitch : width * bytes;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                std::size_t at = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * bytes;
                if (at + bytes > size) return;
                pixel(data + at, &out[(static_cast<std::size_t>(y) * width + x) * 4]);
            }
        }
    };

    switch (tex.format) {
    case 28:
    case 29:
        rows(4, [](const uint8_t* s, uint8_t* d) { std::memcpy(d, s, 4); });
        return true;
    case 87:
    case 91:
        rows(4, [](const uint8_t* s, uint8_t* d) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
        });
        return true;
    case 61:
        rows(1, [](const uint8_t* s, uint8_t* d) {
            d[0] = d[1] = d[2] = s[0];
            d[3] = 255;
        });
        return true;
    case 49:
        rows(2, [](const uint8_t* s, uint8_t* d) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = 0;
            d[3] = 255;
        });
        return true;
    case 10:
        rows(8, [](const uint8_t* s, uint8_t* d) {
            for (int c = 0; c < 4; c++) {
                uint16_t h;
                std::memcpy(&h, s + c * 2, 2);
                d[c] = ToByte(HalfToFloat(h));
            }
        });
        return true;
    case 71:
    case 72:
        blocks(8, [](const uint8_t* s, uint8_t* t) { bcdec_bc1(s, t, 16); });
        return true;
    case 74:
    case 75:
        blocks(16, [](const uint8_t* s, uint8_t* t) { bcdec_bc2(s, t, 16); });
        return true;
    case 77:
    case 78:
        blocks(16, [](const uint8_t* s, uint8_t* t) { bcdec_bc3(s, t, 16); });
        return true;
    case 80:
        blocks(8, [](const uint8_t* s, uint8_t* t) {
            uint8_t r[16];
            bcdec_bc4(s, r, 4);
            for (int i = 0; i < 16; i++) {
                t[i * 4] = t[i * 4 + 1] = t[i * 4 + 2] = r[i];
                t[i * 4 + 3] = 255;
            }
        });
        return true;
    case 83:
        blocks(16, [](const uint8_t* s, uint8_t* t) {
            uint8_t rg[32];
            bcdec_bc5(s, rg, 8);
            for (int i = 0; i < 16; i++) {
                t[i * 4] = rg[i * 2];
                t[i * 4 + 1] = rg[i * 2 + 1];
                t[i * 4 + 2] = 0;
                t[i * 4 + 3] = 255;
            }
        });
        return true;
    case 95:
    case 96: {
        bool isSigned = tex.format == 96;
        blocks(16, [isSigned](const uint8_t* s, uint8_t* t) {
            float rgb[48];
            bcdec_bc6h_float(s, rgb, 12, isSigned);
            for (int i = 0; i < 16; i++) {
                for (int c = 0; c < 3; c++) t[i * 4 + c] = ToByte(rgb[i * 3 + c]);
                t[i * 4 + 3] = 255;
            }
        });
        return true;
    }
    case 98:
    case 99:
        blocks(16, [](const uint8_t* s, uint8_t* t) { bcdec_bc7(s, t, 16); });
        return true;
    default:
        return false;
    }
}

bool DecodeTextureHdr(const TextureData& tex, uint32_t mip, std::vector<float>& rgb, uint32_t& width, uint32_t& height) {
    if (mip >= tex.mips.size()) return false;
    width = std::max<uint32_t>(tex.width >> mip, 1);
    height = std::max<uint32_t>(tex.height >> mip, 1);
    const TextureMip& level = tex.mips[mip];
    const uint8_t* data = level.data.data();
    std::size_t size = level.data.size();
    rgb.assign(static_cast<std::size_t>(width) * height * 3, 0.0f);
    if (tex.format == 95 || tex.format == 96) {
        bool isSigned = tex.format == 96;
        uint32_t across = (width + 3) / 4;
        uint32_t down = (height + 3) / 4;
        uint32_t pitch = level.pitch ? level.pitch : across * 16;
        float tile[48];
        for (uint32_t by = 0; by < down; by++) {
            for (uint32_t bx = 0; bx < across; bx++) {
                std::size_t at = static_cast<std::size_t>(by) * pitch + static_cast<std::size_t>(bx) * 16;
                if (at + 16 > size) return true;
                bcdec_bc6h_float(data + at, tile, 12, isSigned);
                for (uint32_t y = 0; y < 4 && by * 4 + y < height; y++) {
                    for (uint32_t x = 0; x < 4 && bx * 4 + x < width; x++) {
                        std::memcpy(&rgb[((by * 4 + y) * static_cast<std::size_t>(width) + bx * 4 + x) * 3], &tile[(y * 4 + x) * 3], 12);
                    }
                }
            }
        }
        return true;
    }
    if (tex.format == 10) {
        uint32_t pitch = level.pitch ? level.pitch : width * 8;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                std::size_t at = static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 8;
                if (at + 8 > size) return true;
                for (int c = 0; c < 3; c++) {
                    uint16_t h;
                    std::memcpy(&h, data + at + c * 2, 2);
                    rgb[(static_cast<std::size_t>(y) * width + x) * 3 + c] = HalfToFloat(h);
                }
            }
        }
        return true;
    }
    return false;
}
