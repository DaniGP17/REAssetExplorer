#ifndef REASSETEXPLORER_SOUNDDATA_H
#define REASSETEXPLORER_SOUNDDATA_H
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Wwise 2019 as RE7/RE8 ship it: .bnk bank version 135, .pck AKPK version 1.

struct WemInfo {
    bool valid = false;
    uint16_t format = 0;  // 0xFFFF Wwise Vorbis
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t sampleCount = 0;  // 0 when the header does not give it
    uint32_t dataSize = 0;     // full audio payload, also for prefetch copies
};

WemInfo ParseWemHeader(std::span<const uint8_t> data);

enum class HircType : uint8_t {
    State = 1,
    Sound = 2,
    Action = 3,
    Event = 4,
    RanSeqContainer = 5,
    SwitchContainer = 6,
    ActorMixer = 7,
    Bus = 8,
    LayerContainer = 9,
    MusicSegment = 10,
    MusicTrack = 11,
    MusicSwitch = 12,
    MusicRanSeq = 13,
    Attenuation = 14,
    DialogueEvent = 15,
    FxShareSet = 16,
    FxCustom = 17,
    AuxBus = 18,
    Lfo = 19,
    Envelope = 20,
    AudioDevice = 21,
    TimeMod = 22
};

const char* HircTypeName(uint8_t type);

struct SoundBankSource {
    uint32_t pluginId;   // codec (low nibble 1) or source plugin (2)
    uint8_t streamType;  // 0 in the bank, 1 prefetch + stream, 2 streamed
    uint32_t sourceId;   // the .wem id
    uint32_t memorySize;
};

// HIRC object; only the fields that tie the hierarchy together are read.
struct SoundBankObject {
    uint8_t type;
    uint32_t id;
    uint32_t parentId = 0;             // actor-mixer hierarchy and music nodes
    std::vector<uint32_t> actionIds;   // events
    uint16_t actionType = 0;           // actions
    uint32_t targetId = 0;             // actions
    std::vector<SoundBankSource> sources;  // sounds and music tracks
};

struct SoundBankMedia {
    uint32_t id;
    uint32_t offset;  // in the DATA chunk
    uint32_t size;    // smaller than the audio when only a prefetch is embedded
    WemInfo info;
};

struct SoundBankData {
    uint32_t version = 0;
    uint32_t bankId = 0;
    uint32_t languageId = 0;
    uint32_t projectId = 0;
    std::vector<std::string> chunks;
    std::vector<SoundBankMedia> media;
    std::vector<SoundBankObject> objects;
    std::vector<std::pair<uint32_t, std::string>> bankNames;  // STID
    std::vector<std::string> warnings;
};

struct SoundPackageEntry {
    uint64_t id;
    uint32_t blockSize;
    uint32_t size;
    uint64_t offset;
    uint32_t languageId;
    WemInfo info;  // filled by the caller when the package holds the data
};

struct SoundPackageData {
    uint32_t version = 0;
    uint32_t headerSize = 0;  // bytes before the data, including the 8-byte tag
    std::vector<std::pair<uint32_t, std::string>> languages;
    std::vector<SoundPackageEntry> banks;
    std::vector<SoundPackageEntry> streams;
    std::vector<SoundPackageEntry> externals;
};

#endif
