#ifndef REASSETEXPLORER_PREFABREADER_H
#define REASSETEXPLORER_PREFABREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/SceneData.h"
#include "Core/Rsz/RszTypeDatabase.h"

// scene without folders
class PrefabReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".pfb.") != std::string_view::npos; }

    SceneData Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const;
};

#endif
