#ifndef REASSETEXPLORER_LOADEDGAME_H
#define REASSETEXPLORER_LOADEDGAME_H
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "IGame.h"
#include "IPakReader.h"
#include "PakFileList.h"

struct StoredFile {
    std::filesystem::path pak;
    uint64_t offset = 0;
    uint64_t size = 0;
};

class LoadedGame {
public:
    static LoadedGame Open(std::unique_ptr<IGame> game,
                           const std::filesystem::path& gameDir,
                           const std::filesystem::path& fileListsDir);

    const PakEntry* FindEntry(std::string_view path) const;
    std::vector<uint8_t> ExtractFile(std::string_view path) const;
    // Clamped to the file end. Uncompressed entries are read in place, without loading the whole file.
    std::vector<uint8_t> ReadFileRange(std::string_view path, uint64_t offset, uint64_t size) const;
    // Empty for compressed or missing files.
    std::optional<StoredFile> FindStored(std::string_view path) const;

    const IGame& Game() const { return *game; }
    const std::vector<PakFile>& Paks() const { return paks; }
    const AssetReaderRegistry& Readers() const { return readers; }

private:
    LoadedGame() = default;

    std::unique_ptr<IGame> game;
    PakFileList fileList;
    std::unique_ptr<IPakReader> pakReader;
    std::vector<PakFile> paks;
    AssetReaderRegistry readers;
};

#endif
