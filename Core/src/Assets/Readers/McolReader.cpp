#include "Core/Assets/Readers/McolReader.h"

#include <stdexcept>
#include <string>

#include "Core/Assets/Readers/TerrReader.h"
#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t MCOL_MAGIC = 0x4C4F434D;
// REE-Lib McolFile: magic, BVH size, string count, pad 4, u64 offset of a BVH string table copy, pad 8.
constexpr std::size_t HEADER_SIZE = 32;

}

TerrainData McolReader::Read(std::span<const uint8_t> data, uint32_t version) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != MCOL_MAGIC) throw std::runtime_error("mcol: bad magic");
    TerrainData mcol;
    mcol.version = version;
    // Objects without collision end right after the header.
    if (data.size() > HEADER_SIZE) ReadCollisionBvh(data.subspan(HEADER_SIZE), version, mcol);
    return mcol;
}
