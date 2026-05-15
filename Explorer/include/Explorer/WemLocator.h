#ifndef REASSETEXPLORER_WEMLOCATOR_H
#define REASSETEXPLORER_WEMLOCATOR_H
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Assets/SoundData.h"
#include "Core/LoadedGame.h"

// A bank holds whole files or only a prefetch; the rest is in the streaming
// package named like the bank. Failing that, every package is indexed once.
class WemLocator {
public:
    explicit WemLocator(const LoadedGame& game) : game(game) {}

    // container: pak path of a .bnk, .pck or .wem (the id is ignored for .wem).
    std::vector<uint8_t> Read(const std::string& container, uint32_t mediaId);

private:
    struct Located {
        std::string package;
        SoundPackageEntry entry;
    };

    bool FromBank(const std::string& bank, uint32_t mediaId, std::vector<uint8_t>& out) const;
    bool FromPackage(const std::string& package, uint32_t mediaId, std::vector<uint8_t>& out) const;
    bool FromIndex(const std::string& container, uint32_t mediaId, std::vector<uint8_t>& out);

    const LoadedGame& game;
    std::mutex indexMutex;
    bool indexed = false;
    std::unordered_map<uint32_t, std::vector<Located>> index;
};

#endif
