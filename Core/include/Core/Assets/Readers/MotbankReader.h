#ifndef REASSETEXPLORER_MOTBANKREADER_H
#define REASSETEXPLORER_MOTBANKREADER_H
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Assets/IAssetReader.h"

// Motion FSM PlayMotion actions name a bankId and a motion id; bankId selects the motlist.
struct MotbankEntry {
    std::string path;  // motlist, as the file stores it
    int32_t bankId = 0;
    uint32_t bankType = 0;
    uint64_t bankTypeMask = 0;
};

struct MotbankData {
    uint32_t version = 0;
    std::string uvarPath;
    std::string jmapPath;
    std::vector<MotbankEntry> entries;
};

class MotbankReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".motbank.") != std::string_view::npos; }

    MotbankData Read(std::span<const uint8_t> data) const;
};

#endif
