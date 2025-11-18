#ifndef REASSETEXPLORER_GAME_H
#define REASSETEXPLORER_GAME_H
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/AssetReaderRegistry.h"
#include "IPakReader.h"


class IGame {
public:
    virtual ~IGame() = default;

    virtual std::string GetId() const = 0;
    virtual std::string GetName() const = 0;
    virtual std::string GetFileListName() const {
        return this->GetId() + ".txt";
    }
    virtual std::string GetRszFile() const {
        return "rsz" + this->GetId() + ".json";
    }
    virtual std::vector<std::filesystem::path> GetPaksLocations() const = 0;
    virtual PakVersions GetPakReaderVersion() const = 0;
    virtual AssetReaderRegistry CreateAssetReaders() const = 0;

    virtual std::string GetTexExt() const = 0;
    virtual std::string GetMdfExt() const = 0;
    virtual std::string GetMeshExt() const = 0;
    virtual std::string GetMasterExt() const = 0;
    virtual std::string GetSceneExt() const { return ".20"; }
    virtual std::string GetUvsExt() const { return ""; }
    virtual std::string GetEfxExt() const { return ""; }
    virtual std::string GetPrefabExt() const { return ".17"; }
    virtual std::string GetMotlistExt() const = 0;
    virtual std::string GetMotExt() const = 0;
    virtual std::string GetTerrExt() const = 0;
    virtual std::string GetMcolExt() const = 0;
    virtual std::string GetFolExt() const { return ""; }  // empty: no foliage
    // Version suffix for type (".aimap", ".ainvm"...), empty when the game has none.
    virtual std::string GetAiMapExt(std::string_view type) const { return ""; }
    virtual std::string GetDefaultSkyPath() const = 0;
    virtual uint32_t GetInstanceStride() const = 0;
};


#endif
