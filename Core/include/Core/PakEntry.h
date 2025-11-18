#ifndef REASSETEXPLORER_PAKENTRY_H
#define REASSETEXPLORER_PAKENTRY_H
#include <array>
#include <cstdint>
#include <string>

enum class CompressionType {
    Uncompressed,
    Deflated,
    ZStandard,
    Unknown
};

struct PakEntry {
    std::string filePath;
    uint32_t lowerCaseHash;
    uint32_t upperCaseHash;
    int64_t offset;
    int64_t compressedSize;
    int64_t uncompressedSize;
    std::array<std::uint8_t, 8> flags;
    int64_t checksum;

    bool IsCompressed() const {
        return compressedSize != uncompressedSize;
    }

    CompressionType GetCompressionType() const {
        std::uint8_t compressionFlag = flags[0] & 0x0F;
        switch (compressionFlag) {
            case 0: return CompressionType::Uncompressed;
            case 1: return CompressionType::Deflated;
            case 2: return CompressionType::ZStandard;
            default: return CompressionType::Unknown;
        }
    }
};

#endif
