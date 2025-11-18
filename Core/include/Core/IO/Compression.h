#ifndef REASSETEXPLORER_COMPRESSION_H
#define REASSETEXPLORER_COMPRESSION_H
#include <cstdint>
#include <span>
#include <vector>

namespace Compression {
    std::vector<uint8_t> InflateRaw(std::span<const uint8_t> src, uint64_t dstSize);
    std::vector<uint8_t> DecompressZstd(std::span<const uint8_t> src, uint64_t dstSize);
}

#endif
