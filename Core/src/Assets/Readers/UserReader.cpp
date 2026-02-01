#include "Core/Assets/Readers/UserReader.h"

#include <stdexcept>

#include "Core/IO/MemoryReader.h"
#include "Core/Rsz/RszReader.h"

// "USR\0". Userdata records (16B): type id, crc, u64 path offset.
UserFileData UserReader::Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != 0x00525355) throw std::runtime_error("user: bad magic");

    int32_t resourceCount = r.ReadAt<int32_t>(4);
    int32_t userDataCount = r.ReadAt<int32_t>(8);
    uint64_t resourceInfoOffset = r.ReadAt<uint64_t>(16);
    uint64_t userDataInfoOffset = r.ReadAt<uint64_t>(24);
    uint64_t dataOffset = r.ReadAt<uint64_t>(32);

    UserFileData user;
    user.resources.reserve(resourceCount);
    for (int32_t i = 0; i < resourceCount; i++) {
        user.resources.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(resourceInfoOffset + i * 8)));
    }
    user.userData.reserve(userDataCount);
    for (int32_t i = 0; i < userDataCount; i++) {
        std::size_t at = userDataInfoOffset + i * 16;
        RszUserDataRef ref;
        ref.typeId = r.ReadAt<uint32_t>(at);
        ref.instanceId = 0;
        ref.path = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 8));
        user.userData.push_back(std::move(ref));
    }

    RszReader rszReader(db);
    user.rsz = rszReader.Read(r, dataOffset);
    return user;
}
