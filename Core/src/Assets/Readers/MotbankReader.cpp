#include "Core/Assets/Readers/MotbankReader.h"

#include <stdexcept>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t MOTBANK_MAGIC = 0x6B6E626D;  // "mbnk"
constexpr int32_t MAX_ENTRIES = 1 << 16;

std::string StringAt(std::span<const uint8_t> data, uint64_t offset) {
    std::string text;
    if (offset == 0) return text;
    uint16_t pendingHigh = 0;
    for (uint64_t at = offset; at + 1 < data.size(); at += 2) {
        auto unit = static_cast<uint16_t>(data[at] | data[at + 1] << 8);
        if (unit == 0) break;
        MemoryReader::AppendUtf16(text, unit, pendingHigh);
    }
    return text;
}

}

MotbankData MotbankReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    MotbankData bank;
    bank.version = r.Read<uint32_t>();
    if (data.size() < 0x30 || r.Read<uint32_t>() != MOTBANK_MAGIC) throw std::runtime_error("not a motbank (no mbnk magic)");
    r.Skip(8);
    uint64_t entriesOffset = r.Read<uint64_t>();
    bank.uvarPath = StringAt(data, r.Read<uint64_t>());
    if (bank.version >= 3) bank.jmapPath = StringAt(data, r.Read<uint64_t>());
    int32_t count = r.Read<int32_t>();
    if (count < 0 || count > MAX_ENTRIES) throw std::runtime_error("bad motbank entry count");

    r.Seek(static_cast<std::size_t>(entriesOffset));
    for (int32_t i = 0; i < count; i++) {
        MotbankEntry entry;
        entry.path = StringAt(data, r.Read<uint64_t>());
        if (bank.version >= 3) {
            entry.bankId = r.Read<int32_t>();
            entry.bankType = r.Read<uint32_t>();
        } else {
            entry.bankId = static_cast<int32_t>(r.Read<int64_t>());
        }
        entry.bankTypeMask = r.Read<uint64_t>();
        bank.entries.push_back(std::move(entry));
    }
    return bank;
}
