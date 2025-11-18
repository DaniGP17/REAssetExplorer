#ifndef REASSETEXPLORER_PAKFILELIST_H
#define REASSETEXPLORER_PAKFILELIST_H
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Hashing/MurmurHash3.h"

struct HashListEntry {
    uint32_t lowerCaseHash;
    uint32_t upperCaseHash;
    std::string path;
};

class PakFileList {
public:
    void SetupFromFile(std::string filePath);
    const HashListEntry* GetByLowerHash(uint32_t lowerHash) const {
        auto it = hashDict.find(lowerHash);
        return (it != hashDict.end()) ? &it->second : nullptr;
    }

private:
    std::string ReadWholeFile(const std::string& path);
    std::vector<std::string_view> SplitLines(std::string_view content);

    HashListEntry CreateHashListEntry(std::string_view& path) {
        return HashListEntry {
            Murmur3::MakeHash(path, true, true),
            Murmur3::MakeHash(path, false, true),
            std::string(path)
        };
    }

    std::pmr::unordered_map<uint32_t, HashListEntry> hashDict;
};

#endif
