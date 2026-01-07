#include "Core/Assets/Readers/MotlistReader.h"

#include <map>
#include <string>

#include "Core/Assets/Readers/MotReader.h"
#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t MOTLIST_MAGIC = 0x74736C6D;
constexpr uint32_t MOT_MAGIC = 0x20746F6D;
constexpr uint32_t MOTLIST_VERSION_RE7 = 60;
constexpr uint32_t MOTLIST_VERSION_RE8 = 486;
constexpr uint32_t MOTLIST_VERSION_ONIWS = 1036;

// Motion index entry: u64 override clip offset (after RE7), u16 motion id, u16 switch,
// then 12 bytes of masks and flags (RE8) plus 12 more u32 in later versions.
std::size_t MotionIndexStride(uint32_t version) {
    if (version <= MOTLIST_VERSION_RE7) return 4;
    if (version <= MOTLIST_VERSION_RE8) return 24;
    return 72;
}

std::string MagicText(uint32_t magic) {
    std::string text;
    for (int i = 0; i < 4; i++) {
        char c = static_cast<char>((magic >> (i * 8)) & 0xFF);
        text += c >= 0x20 && c < 0x7F ? c : '?';
    }
    return text;
}

}

MotlistData MotlistReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(4) != MOTLIST_MAGIC) throw std::runtime_error("motlist: bad magic");

    MotlistData list;
    list.version = r.ReadAt<uint32_t>(0);
    uint64_t pointersOffset = r.ReadAt<uint64_t>(16);
    uint64_t indicesOffset = r.ReadAt<uint64_t>(24);
    uint64_t nameOffset = r.ReadAt<uint64_t>(32);
    std::size_t countPos = 40;
    if (list.version > MOTLIST_VERSION_RE7) {
        uint64_t baseOffset = r.ReadAt<uint64_t>(40);
        if (baseOffset != 0) list.baseMotListPath = r.ReadWStringAt(baseOffset);
        countPos += 8;
    }
    if (list.version >= MOTLIST_VERSION_ONIWS) countPos += 8;
    uint32_t count = r.ReadAt<uint32_t>(countPos);
    if (nameOffset != 0) list.name = r.ReadWStringAt(nameOffset);

    // Several slots may point at the same mot.
    std::map<uint64_t, int32_t> motionByOffset;
    std::map<uint64_t, std::string> otherByOffset;
    std::shared_ptr<const std::vector<MotBone>> sharedBones;
    std::vector<uint64_t> offsets(count);
    for (uint32_t i = 0; i < count; i++) {
        uint64_t offset = r.ReadAt<uint64_t>(pointersOffset + i * 8);
        offsets[i] = offset;
        if (offset == 0 || motionByOffset.contains(offset)) continue;
        motionByOffset[offset] = -1;

        std::string where = "slot " + std::to_string(i) + ": ";
        if (offset + 8 > data.size()) {
            list.warnings.push_back(where + "offset out of range");
            otherByOffset[offset] = "out of range";
            continue;
        }
        if (r.ReadAt<uint32_t>(offset) == 1) {
            list.warnings.push_back(where + "external mot " + r.ReadWStringAt(offset + 4));
            otherByOffset[offset] = "external " + r.ReadWStringAt(offset + 4);
            continue;
        }
        uint32_t magic = r.ReadAt<uint32_t>(offset + 4);
        if (magic != MOT_MAGIC) {
            list.warnings.push_back(where + "unsupported entry '" + MagicText(magic) + "'");
            otherByOffset[offset] = MagicText(magic) == "mtre" ? "motion tree" : "'" + MagicText(magic) + "'";
            continue;
        }
        try {
            MotData mot = MotReader::ReadEmbedded(data.subspan(offset), sharedBones);
            if (!sharedBones && mot.bones) sharedBones = mot.bones;
            motionByOffset[offset] = static_cast<int32_t>(list.motions.size());
            list.motions.push_back(std::move(mot));
        } catch (const std::exception& e) {
            list.warnings.push_back(where + e.what());
            otherByOffset[offset] = "unreadable";
        }
    }

    std::size_t stride = MotionIndexStride(list.version);
    std::size_t idOffset = list.version > MOTLIST_VERSION_RE7 ? 8 : 0;
    for (uint32_t i = 0; i < count; i++) {
        MotlistEntry entry;
        if (indicesOffset != 0) entry.motionId = r.ReadAt<uint16_t>(indicesOffset + i * stride + idOffset);
        if (offsets[i] != 0) entry.motion = motionByOffset[offsets[i]];
        if (entry.motion < 0) entry.other = offsets[i] == 0 ? "empty" : otherByOffset[offsets[i]];
        list.entries.push_back(entry);
    }
    return list;
}
