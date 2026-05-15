#include "Core/Assets/Readers/WwiseReader.h"

#include <stdexcept>

#include "Core/IO/MemoryReader.h"

namespace {

// The only bank version whose HIRC layout was checked against game files.
constexpr uint32_t HIRC_VERSION = 135;

uint32_t Tag(const char (&text)[5]) {
    return static_cast<uint32_t>(text[0]) | static_cast<uint32_t>(text[1]) << 8 |
           static_cast<uint32_t>(text[2]) << 16 | static_cast<uint32_t>(text[3]) << 24;
}

std::string TagText(uint32_t tag) {
    std::string text(4, ' ');
    for (int i = 0; i < 4; i++) text[i] = static_cast<char>((tag >> (i * 8)) & 0xFF);
    return text;
}

uint32_t ReadVar(const MemoryReader& r, std::size_t& pos) {
    uint32_t value = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t b = r.ReadAt<uint8_t>(pos++);
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return value;
}

SoundBankSource ReadSource(const MemoryReader& r, std::size_t& pos) {
    SoundBankSource source{};
    source.pluginId = r.ReadAt<uint32_t>(pos);
    source.streamType = r.ReadAt<uint8_t>(pos + 4);
    source.sourceId = r.ReadAt<uint32_t>(pos + 5);
    source.memorySize = r.ReadAt<uint32_t>(pos + 9);
    pos += 14;  // + uSourceBits
    // Source plugins (tone generator, silence...) carry their parameters inline.
    if ((source.pluginId & 0xF) == 2) pos += 4 + r.ReadAt<uint32_t>(pos);
    return source;
}

// NodeBaseParams up to DirectParentID: NodeInitialFxParams (override flag,
// count, bypass bits, 7 bytes per effect), bOverrideAttachmentParams,
// OverrideBusId, DirectParentID.
uint32_t ReadParentId(const MemoryReader& r, std::size_t pos) {
    pos += 1;
    uint8_t fxCount = r.ReadAt<uint8_t>(pos++);
    if (fxCount > 0) pos += 1 + fxCount * 7u;
    pos += 1 + 4;
    return r.ReadAt<uint32_t>(pos);
}

bool ParseObject(const MemoryReader& r, std::size_t pos, std::size_t end, SoundBankObject& obj) {
    switch (static_cast<HircType>(obj.type)) {
        case HircType::Event: {
            uint32_t count = ReadVar(r, pos);
            if (pos + count * 4ull > end) return false;
            for (uint32_t i = 0; i < count; i++) obj.actionIds.push_back(r.ReadAt<uint32_t>(pos + i * 4));
            return true;
        }
        case HircType::Action:
            obj.actionType = r.ReadAt<uint16_t>(pos);
            obj.targetId = r.ReadAt<uint32_t>(pos + 2);
            return true;
        case HircType::Sound:
            obj.sources.push_back(ReadSource(r, pos));
            obj.parentId = ReadParentId(r, pos);
            return pos < end;
        case HircType::RanSeqContainer:
        case HircType::SwitchContainer:
        case HircType::ActorMixer:
        case HircType::LayerContainer:
            obj.parentId = ReadParentId(r, pos);
            return true;
        case HircType::MusicSegment:
        case HircType::MusicSwitch:
        case HircType::MusicRanSeq:
            obj.parentId = ReadParentId(r, pos + 1);  // after MusicNodeParams.uFlags
            return true;
        case HircType::MusicTrack: {
            pos += 1;  // uFlags
            uint32_t sourceCount = r.ReadAt<uint32_t>(pos);
            pos += 4;
            for (uint32_t i = 0; i < sourceCount && pos < end; i++) obj.sources.push_back(ReadSource(r, pos));
            // AkTrackSrcInfo: trackID, sourceID, cacheID, then four doubles.
            uint32_t playlistCount = r.ReadAt<uint32_t>(pos);
            pos += 4 + playlistCount * 44ull;
            if (playlistCount > 0) pos += 4;  // numSubTrack
            uint32_t automationCount = r.ReadAt<uint32_t>(pos);
            pos += 4;
            for (uint32_t i = 0; i < automationCount && pos < end; i++) {
                uint32_t points = r.ReadAt<uint32_t>(pos + 8);
                pos += 12 + points * 12ull;
            }
            if (pos >= end) return false;
            obj.parentId = ReadParentId(r, pos);
            return true;
        }
        default:
            return true;
    }
}

}

const char* HircTypeName(uint8_t type) {
    static const char* const NAMES[] = {
        "Unknown", "State", "Sound", "Action", "Event", "Random/Sequence Container", "Switch Container",
        "Actor-Mixer", "Bus", "Blend Container", "Music Segment", "Music Track", "Music Switch",
        "Music Playlist", "Attenuation", "Dialogue Event", "Effect Share Set", "Effect", "Auxiliary Bus",
        "LFO Modulator", "Envelope Modulator", "Audio Device", "Time Modulator"
    };
    return type < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[type] : NAMES[0];
}

WemInfo ParseWemHeader(std::span<const uint8_t> data) {
    WemInfo info;
    if (data.size() < 12) return info;
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != Tag("RIFF")) return info;
    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        uint32_t tag = r.ReadAt<uint32_t>(pos);
        uint32_t size = r.ReadAt<uint32_t>(pos + 4);
        std::size_t body = pos + 8;
        if (tag == Tag("fmt ") && body + 16 <= data.size()) {
            info.format = r.ReadAt<uint16_t>(body);
            info.channels = r.ReadAt<uint16_t>(body + 2);
            info.sampleRate = r.ReadAt<uint32_t>(body + 4);
            // Wwise Vorbis keeps its sample count inside the extended fmt.
            if (info.format == 0xFFFF && size >= 0x1C && body + 0x1C <= data.size()) {
                info.sampleCount = r.ReadAt<uint32_t>(body + 0x18);
            }
            info.valid = true;
        } else if (tag == Tag("vorb") && body + 4 <= data.size() && info.sampleCount == 0) {
            info.sampleCount = r.ReadAt<uint32_t>(body);
        } else if (tag == Tag("data")) {
            info.dataSize = size;
            break;
        }
        pos = body + size + (size & 1);
    }
    return info;
}

SoundBankData BnkReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (data.size() < 8 || r.ReadAt<uint32_t>(0) != Tag("BKHD")) throw std::runtime_error("bnk: no BKHD");

    SoundBankData bank;
    std::size_t dataChunk = 0;
    std::size_t pos = 0;
    while (pos + 8 <= data.size()) {
        uint32_t tag = r.ReadAt<uint32_t>(pos);
        uint32_t size = r.ReadAt<uint32_t>(pos + 4);
        std::size_t body = pos + 8;
        std::size_t end = body + size;
        if (end > data.size()) {
            bank.warnings.push_back(TagText(tag) + " runs past the end of the file");
            break;
        }
        bank.chunks.push_back(TagText(tag));

        if (tag == Tag("BKHD")) {
            bank.version = r.ReadAt<uint32_t>(body);
            bank.bankId = r.ReadAt<uint32_t>(body + 4);
            bank.languageId = r.ReadAt<uint32_t>(body + 8);
            if (size >= 20) bank.projectId = r.ReadAt<uint32_t>(body + 16);
        } else if (tag == Tag("DIDX")) {
            for (std::size_t e = body; e + 12 <= end; e += 12) {
                bank.media.push_back({ r.ReadAt<uint32_t>(e), r.ReadAt<uint32_t>(e + 4), r.ReadAt<uint32_t>(e + 8), {} });
            }
        } else if (tag == Tag("DATA")) {
            dataChunk = body;
        } else if (tag == Tag("HIRC")) {
            uint32_t count = r.ReadAt<uint32_t>(body);
            std::size_t o = body + 4;
            for (uint32_t i = 0; i < count && o + 9 <= end; i++) {
                SoundBankObject obj{};
                obj.type = r.ReadAt<uint8_t>(o);
                uint32_t objSize = r.ReadAt<uint32_t>(o + 1);
                obj.id = r.ReadAt<uint32_t>(o + 5);
                std::size_t objEnd = o + 5 + objSize;
                if (bank.version == HIRC_VERSION) {
                    SoundBankObject parsed = obj;
                    try {
                        if (ParseObject(r, o + 9, objEnd, parsed)) obj = std::move(parsed);
                    } catch (const std::exception&) {
                        // Keep type and id only.
                    }
                }
                bank.objects.push_back(std::move(obj));
                o = objEnd;
            }
            if (bank.version != HIRC_VERSION) {
                bank.warnings.push_back("HIRC details are only read for bank version 135");
            }
        } else if (tag == Tag("STID")) {
            uint32_t count = r.ReadAt<uint32_t>(body + 4);
            std::size_t e = body + 8;
            for (uint32_t i = 0; i < count && e + 5 <= end; i++) {
                uint32_t id = r.ReadAt<uint32_t>(e);
                uint8_t length = r.ReadAt<uint8_t>(e + 4);
                std::span<const uint8_t> text = r.BytesAt(e + 5, length);
                bank.bankNames.emplace_back(id, std::string(text.begin(), text.end()));
                e += 5 + length;
            }
        }
        pos = end;
    }

    if (dataChunk != 0) {
        for (SoundBankMedia& media : bank.media) {
            if (dataChunk + media.offset + media.size <= data.size()) {
                media.info = ParseWemHeader(data.subspan(dataChunk + media.offset, media.size));
            }
        }
    }
    return bank;
}

uint32_t PckReader::HeaderBytes(std::span<const uint8_t> first8) {
    MemoryReader r(first8);
    if (first8.size() < 8 || r.ReadAt<uint32_t>(0) != Tag("AKPK")) throw std::runtime_error("pck: no AKPK");
    return r.ReadAt<uint32_t>(4) + 8;
}

// AKPK: version, then the sizes of the language map and of the bank,
// streamed-file and external lookup tables, which follow in that order.
SoundPackageData PckReader::ReadHeader(std::span<const uint8_t> header) const {
    MemoryReader r(header);
    SoundPackageData package;
    package.headerSize = HeaderBytes(header);
    package.version = r.ReadAt<uint32_t>(8);
    uint32_t languagesSize = r.ReadAt<uint32_t>(12);
    uint32_t banksSize = r.ReadAt<uint32_t>(16);
    uint32_t streamsSize = r.ReadAt<uint32_t>(20);
    // Older packages have no externals table; the sizes add up either way.
    uint32_t externalsSize = r.ReadAt<uint32_t>(24);
    bool hasExternals = 28ull + languagesSize + banksSize + streamsSize + externalsSize == package.headerSize;
    if (!hasExternals) externalsSize = 0;
    std::size_t pos = hasExternals ? 28 : 24;

    uint32_t languageCount = r.ReadAt<uint32_t>(pos);
    for (uint32_t i = 0; i < languageCount; i++) {
        uint32_t offset = r.ReadAt<uint32_t>(pos + 4 + i * 8);
        uint32_t id = r.ReadAt<uint32_t>(pos + 8 + i * 8);
        package.languages.emplace_back(id, r.ReadWStringAt(pos + offset));
    }
    pos += languagesSize;

    auto readTable = [&r](std::size_t at, bool wideIds, std::vector<SoundPackageEntry>& out) {
        uint32_t count = r.ReadAt<uint32_t>(at);
        std::size_t e = at + 4;
        for (uint32_t i = 0; i < count; i++) {
            SoundPackageEntry entry{};
            entry.id = wideIds ? r.ReadAt<uint64_t>(e) : r.ReadAt<uint32_t>(e);
            e += wideIds ? 8 : 4;
            entry.blockSize = r.ReadAt<uint32_t>(e);
            entry.size = r.ReadAt<uint32_t>(e + 4);
            entry.offset = static_cast<uint64_t>(r.ReadAt<uint32_t>(e + 8)) * (entry.blockSize ? entry.blockSize : 1);
            entry.languageId = r.ReadAt<uint32_t>(e + 12);
            e += 16;
            out.push_back(entry);
        }
    };
    readTable(pos, false, package.banks);
    pos += banksSize;
    readTable(pos, false, package.streams);
    pos += streamsSize;
    if (externalsSize >= 4) readTable(pos, true, package.externals);
    return package;
}
