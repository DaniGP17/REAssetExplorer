#ifndef REASSETEXPLORER_FOLREADER_H
#define REASSETEXPLORER_FOLREADER_H
#include <span>

#include "Core/Assets/FoliageData.h"
#include "Core/Assets/IAssetReader.h"

class FolReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".fol.") != std::string_view::npos; }

    FoliageData Read(std::span<const uint8_t> data) const;
};

#endif
