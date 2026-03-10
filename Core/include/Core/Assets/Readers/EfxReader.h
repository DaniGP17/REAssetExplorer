#ifndef REASSETEXPLORER_EFXREADER_H
#define REASSETEXPLORER_EFXREADER_H
#include <span>

#include "Core/Assets/EffectData.h"
#include "Core/Assets/IAssetReader.h"

class EfxReader : public IAssetReader {
public:
    enum class Game { RE7, RE8 };

    explicit EfxReader(Game game) : game(game) {}

    bool SupportsPath(std::string_view path) const override { return path.find(".efx.") != std::string_view::npos; }

    EffectData Read(std::span<const uint8_t> data) const;

    const char* ItemName(uint32_t type) const;

private:
    Game game;
};

#endif
