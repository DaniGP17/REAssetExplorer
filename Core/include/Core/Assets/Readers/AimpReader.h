#ifndef REASSETEXPLORER_AIMPREADER_H
#define REASSETEXPLORER_AIMPREADER_H
#include <span>

#include "Core/Assets/AiMapData.h"
#include "Core/Assets/IAssetReader.h"

class AimpReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override;

    AiMapData Read(std::span<const uint8_t> data) const;
};

#endif
