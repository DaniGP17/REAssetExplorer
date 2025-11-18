#ifndef REASSETEXPLORER_BINARYREADER_H
#define REASSETEXPLORER_BINARYREADER_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <type_traits>

class BinaryReader {
public:
    explicit BinaryReader(const std::string& path)
        : stream_(path, std::ios::binary) {
        if (!stream_) {
            throw std::runtime_error("Couldn't open: " + path);
        }
    }

    template <typename T>
    T Read() {
        static_assert(std::is_trivially_copyable_v<T>, "T should be trivially copyable");
        T value;
        stream_.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (!stream_) {
            throw std::runtime_error("BinaryReader: read fail");
        }
        return value;
    }

    std::vector<uint8_t> ReadBytes(std::size_t count) {
        std::vector<uint8_t> buffer(count);
        stream_.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
        if (!stream_) {
            throw std::runtime_error("BinaryReader: read fail");
        }
        return buffer;
    }

    void Seek(std::streamoff offset) { stream_.seekg(offset); }

private:
    std::ifstream stream_;
};

#endif
