#ifndef REASSETEXPLORER_TERRREADER_H
#define REASSETEXPLORER_TERRREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/TerrainData.h"

void ReadCollisionBvh(std::span<const uint8_t> bvh, uint32_t version, TerrainData& out);

class TerrReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".terr.") != std::string_view::npos; }

    TerrainData Read(std::span<const uint8_t> data, uint32_t version) const;
};

#endif
