#include "Core/LoadedGame.h"

#include <algorithm>

#include "Core/PakReaderFactory.h"

LoadedGame LoadedGame::Open(std::unique_ptr<IGame> game,
                            const std::filesystem::path& gameDir,
                            const std::filesystem::path& fileListsDir) {
    LoadedGame loaded;
    loaded.game = std::move(game);
    loaded.fileList.SetupFromFile((fileListsDir / loaded.game->GetFileListName()).string());
    loaded.pakReader = CreatePakReader(loaded.game->GetPakReaderVersion());
    loaded.readers = loaded.game->CreateAssetReaders();

    for (const std::filesystem::path& location : loaded.game->GetPaksLocations()) {
        std::filesystem::path pakPath = gameDir / location;
        if (!std::filesystem::exists(pakPath)) continue;
        loaded.paks.push_back(loaded.pakReader->Open(pakPath, loaded.fileList));
    }

    return loaded;
}

std::vector<uint8_t> LoadedGame::ReadFileRange(std::string_view path, uint64_t offset, uint64_t size) const {
    for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
        const PakEntry* entry = it->FindByPath(path);
        if (entry == nullptr) continue;
        uint64_t total = static_cast<uint64_t>(entry->uncompressedSize);
        if (offset >= total) return {};
        uint64_t count = std::min(size, total - offset);
        if (!entry->IsCompressed()) {
            return it->File().ReadAt(static_cast<uint64_t>(entry->offset) + offset, static_cast<std::size_t>(count));
        }
        std::vector<uint8_t> all = pakReader->ExtractFile(*it, *entry);
        return std::vector<uint8_t>(all.begin() + static_cast<std::ptrdiff_t>(offset),
                                    all.begin() + static_cast<std::ptrdiff_t>(offset + count));
    }
    throw std::runtime_error("ReadFileRange: not found: " + std::string(path));
}

std::optional<StoredFile> LoadedGame::FindStored(std::string_view path) const {
    for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
        const PakEntry* entry = it->FindByPath(path);
        if (entry == nullptr) continue;
        if (entry->IsCompressed()) return std::nullopt;
        return StoredFile{ it->Path(), static_cast<uint64_t>(entry->offset), static_cast<uint64_t>(entry->uncompressedSize) };
    }
    return std::nullopt;
}

const PakEntry* LoadedGame::FindEntry(std::string_view path) const {
    for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
        if (const PakEntry* entry = it->FindByPath(path)) {
            return entry;
        }
    }
    return nullptr;
}

std::vector<uint8_t> LoadedGame::ExtractFile(std::string_view path) const {
    for (auto it = paks.rbegin(); it != paks.rend(); ++it) {
        if (const PakEntry* entry = it->FindByPath(path)) {
            return pakReader->ExtractFile(*it, *entry);
        }
    }
    throw std::runtime_error("ExtractFile: not found: " + std::string(path));
}
