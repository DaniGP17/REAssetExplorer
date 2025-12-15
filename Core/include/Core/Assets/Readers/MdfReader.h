#ifndef REASSETEXPLORER_MdfReader_H
#define REASSETEXPLORER_MdfReader_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/MaterialData.h"

class MdfReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".mdf2.") != std::string_view::npos; }

    MaterialData Read(std::span<const uint8_t> data) const;
};

#endif
