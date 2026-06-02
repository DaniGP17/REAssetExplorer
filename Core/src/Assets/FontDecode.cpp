#include "Core/Assets/FontDecode.h"

#include <cstring>
#include <stdexcept>

namespace {

constexpr uint8_t FONT_KEYS[2][8] = {
    { 0x8A, 0xB5, 0x39, 0x6E, 0xAE, 0x45, 0x5F, 0x35 },
    { 0xA3, 0x58, 0x9B, 0xE3, 0xE6, 0x5A, 0xF4, 0x55 },
};
constexpr std::size_t HEADER_SIZE = 4;
constexpr std::size_t MAX_TABLES = 64;

uint32_t BigEndian32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

bool ValidDirectory(const std::vector<uint8_t>& font, std::size_t size) {
    if (font.size() < 12) return false;
    uint32_t version = BigEndian32(font.data());
    if (version != 0x00010000 && version != 0x4F54544F && version != 0x74727565) return false;
    std::size_t tables = static_cast<std::size_t>(font[4]) << 8 | font[5];
    if (tables == 0 || tables > MAX_TABLES || 12 + tables * 16 > font.size()) return false;
    for (std::size_t t = 0; t < tables; t++) {
        const uint8_t* record = font.data() + 12 + t * 16;
        for (int c = 0; c < 4; c++) {
            if (record[c] < 32 || record[c] > 126) return false;
        }
        uint64_t end = static_cast<uint64_t>(BigEndian32(record + 8)) + BigEndian32(record + 12);
        if (end > size) return false;
    }
    return true;
}

}

std::vector<uint8_t> DecryptFont(std::span<const uint8_t> oft) {
    if (oft.size() < HEADER_SIZE + 12 || std::memcmp(oft.data(), "FBFO", 4) != 0) {
        throw std::runtime_error("not an encrypted font (no FBFO magic)");
    }
    std::span<const uint8_t> body = oft.subspan(HEADER_SIZE);
    std::size_t probe = std::min<std::size_t>(body.size(), 12 + MAX_TABLES * 16);
    std::vector<uint8_t> font(probe);
    for (const auto& key : FONT_KEYS) {
        for (std::size_t rotation = 0; rotation < 8; rotation++) {
            for (std::size_t i = 0; i < probe; i++) font[i] = body[i] ^ key[(i + rotation) % 8];
            if (!ValidDirectory(font, body.size())) continue;
            font.resize(body.size());
            for (std::size_t i = 0; i < body.size(); i++) font[i] = body[i] ^ key[(i + rotation) % 8];
            return font;
        }
    }
    throw std::runtime_error("font key not found");
}
