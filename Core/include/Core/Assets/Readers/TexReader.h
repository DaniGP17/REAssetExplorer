#ifndef REASSETEXPLORER_TexReader_H
#define REASSETEXPLORER_TexReader_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/TextureData.h"

class TexReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".tex.") != std::string_view::npos; }

    TextureData Read(std::span<const uint8_t> data) const;
};

#endif
