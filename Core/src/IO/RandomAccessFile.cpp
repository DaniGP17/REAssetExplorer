#include "Core/IO/RandomAccessFile.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32

namespace {

// Overlapped reads on a shared handle each need their own event.
struct ThreadEvent {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~ThreadEvent() {
        if (handle) CloseHandle(handle);
    }
};

}

RandomAccessFile::RandomAccessFile(const std::filesystem::path& path) : path(path) {
    handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Couldn't open: " + path.string());
}

RandomAccessFile::~RandomAccessFile() {
    CloseHandle(handle);
}

void RandomAccessFile::ReadAt(uint64_t offset, void* buffer, std::size_t size) const {
    thread_local ThreadEvent event;
    auto* out = static_cast<uint8_t*>(buffer);
    while (size > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        overlapped.hEvent = event.handle;
        DWORD read = 0;
        if (!ReadFile(handle, out, chunk, nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING) {
            throw std::runtime_error("read fail: " + path.string());
        }
        if (!GetOverlappedResult(handle, &overlapped, &read, TRUE) || read != chunk) {
            throw std::runtime_error("read fail: " + path.string());
        }
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
}

#else

RandomAccessFile::RandomAccessFile(const std::filesystem::path& path) : path(path) {
    descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) throw std::runtime_error("Couldn't open: " + path.string());
}

RandomAccessFile::~RandomAccessFile() {
    close(descriptor);
}

void RandomAccessFile::ReadAt(uint64_t offset, void* buffer, std::size_t size) const {
    auto* out = static_cast<uint8_t*>(buffer);
    while (size > 0) {
        ssize_t read = pread(descriptor, out, size, static_cast<off_t>(offset));
        if (read <= 0) throw std::runtime_error("read fail: " + path.string());
        out += read;
        offset += static_cast<uint64_t>(read);
        size -= static_cast<std::size_t>(read);
    }
}

#endif

std::vector<uint8_t> RandomAccessFile::ReadAt(uint64_t offset, std::size_t size) const {
    std::vector<uint8_t> data(size);
    ReadAt(offset, data.data(), size);
    return data;
}
