#ifndef REASSETEXPLORER_PAKFILE_H
#define REASSETEXPLORER_PAKFILE_H
#include <charconv>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Hashing/MurmurHash3.h"
#include "IO/RandomAccessFile.h"
#include "PakEntry.h"

class PakFile {
public:
    explicit PakFile(std::filesystem::path path) : path(std::move(path)), file(std::make_shared<RandomAccessFile>(this->path)) {}

    void AddEntry(PakEntry entry) {
        uint32_t hash = entry.lowerCaseHash;
        entries.push_back(std::move(entry));
        index[hash] = entries.size() - 1;
    }

    // Entries missing from the file list are named "Unknown_<lower-case hash>".
    const PakEntry* FindByPath(std::string_view path) const {
        uint32_t hash = 0;
        constexpr std::string_view UNKNOWN = "Unknown_";
        if (path.starts_with(UNKNOWN) &&
            std::from_chars(path.data() + UNKNOWN.size(), path.data() + path.size(), hash).ec == std::errc()) {
            auto it = index.find(hash);
            return it == index.end() ? nullptr : &entries[it->second];
        }
        hash = Murmur3::MakeHash(path, true, true);
        auto it = index.find(hash);
        if (it == index.end()) return nullptr;
        return &entries[it->second];
    }

    const std::vector<PakEntry>& Entries() const { return entries; }
    const std::filesystem::path& Path() const { return path; }
    const RandomAccessFile& File() const { return *file; }

private:
    std::filesystem::path path;
    std::shared_ptr<const RandomAccessFile> file;
    std::vector<PakEntry> entries;
    std::pmr::unordered_map<uint32_t, size_t> index;
};

#endif
