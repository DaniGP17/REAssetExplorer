#ifndef REASSETEXPLORER_SceneReader_H
#define REASSETEXPLORER_SceneReader_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/SceneData.h"
#include "Core/Rsz/RszTypeDatabase.h"

class MemoryReader;

class SceneReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".scn.") != std::string_view::npos; }

    SceneData Read(std::span<const uint8_t> data, const RszTypeDatabase& db) const;

    static void BuildHierarchy(SceneData& scn);
};

#endif
