#ifndef REASSETEXPLORER_UVSREADER_H
#define REASSETEXPLORER_UVSREADER_H
#include <span>

#include "Core/Assets/EffectData.h"
#include "Core/Assets/IAssetReader.h"

class UvsReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".uvs.") != std::string_view::npos; }

    UvsData Read(std::span<const uint8_t> data) const;
};

#endif
