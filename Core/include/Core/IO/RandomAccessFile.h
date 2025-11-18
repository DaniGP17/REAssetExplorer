#ifndef REASSETEXPLORER_RANDOMACCESSFILE_H
#define REASSETEXPLORER_RANDOMACCESSFILE_H
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// Opened once and read at explicit offsets; reads from several threads run concurrently.
class RandomAccessFile {
public:
    explicit RandomAccessFile(const std::filesystem::path& path);
    ~RandomAccessFile();
    RandomAccessFile(const RandomAccessFile&) = delete;
    RandomAccessFile& operator=(const RandomAccessFile&) = delete;

    void ReadAt(uint64_t offset, void* buffer, std::size_t size) const;
    std::vector<uint8_t> ReadAt(uint64_t offset, std::size_t size) const;

private:
    std::filesystem::path path;
#ifdef _WIN32
    void* handle = nullptr;
#else
    int descriptor = -1;
#endif
};

#endif
