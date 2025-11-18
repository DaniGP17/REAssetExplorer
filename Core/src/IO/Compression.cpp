#include "Core/IO/Compression.h"

#include <stdexcept>
#include <string>
#include <zlib.h>
#include <zstd.h>

namespace Compression {

std::vector<uint8_t> InflateRaw(std::span<const uint8_t> src, uint64_t dstSize) {
    std::vector<uint8_t> dst(dstSize);
    z_stream strm{};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }
    strm.next_in = const_cast<Bytef*>(src.data());
    strm.avail_in = static_cast<uInt>(src.size());
    strm.next_out = dst.data();
    strm.avail_out = static_cast<uInt>(dst.size());
    int result = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (result != Z_STREAM_END) {
        throw std::runtime_error("inflate failed: " + std::to_string(result));
    }
    return dst;
}

std::vector<uint8_t> DecompressZstd(std::span<const uint8_t> src, uint64_t dstSize) {
    std::vector<uint8_t> dst(dstSize);
    std::size_t result = ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size());
    if (ZSTD_isError(result)) {
        throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(result));
    }
    dst.resize(result);
    return dst;
}

}
