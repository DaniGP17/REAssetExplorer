#ifndef REASSETEXPLORER_MCOLREADER_H
#define REASSETEXPLORER_MCOLREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/TerrainData.h"

class McolReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".mcol.") != std::string_view::npos; }

    // version: the number after .mcol in the path (13020 RE7 RT, 10019 RE8).
    TerrainData Read(std::span<const uint8_t> data, uint32_t version) const;
};

#endif
