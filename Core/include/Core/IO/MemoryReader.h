#ifndef REASSETEXPLORER_MEMORYREADER_H
#define REASSETEXPLORER_MEMORYREADER_H
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

class MemoryReader {
public:
    explicit MemoryReader(std::span<const uint8_t> data) : data_(data) {}

    template <typename T>
    T Read() {
        T value = ReadAt<T>(pos_);
        pos_ += sizeof(T);
        return value;
    }

    template <typename T>
    T ReadAt(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>, "T should be trivially copyable");
        CheckRange(offset, sizeof(T));
        T value;
        std::memcpy(&value, data_.data() + offset, sizeof(T));
        return value;
    }

    std::span<const uint8_t> ReadBytes(std::size_t count) {
        auto view = BytesAt(pos_, count);
        pos_ += count;
        return view;
    }

    std::span<const uint8_t> BytesAt(std::size_t offset, std::size_t count) const {
        CheckRange(offset, count);
        return data_.subspan(offset, count);
    }

    std::string ReadCStringAt(std::size_t offset) const {
        std::string result;
        while (offset < data_.size() && data_[offset] != 0) {
            result += static_cast<char>(data_[offset++]);
        }
        return result;
    }

    std::string ReadWStringAt(std::size_t offset) const {
        std::string result;
        uint16_t pendingHigh = 0;
        while (offset + 1 < data_.size()) {
            uint16_t ch = static_cast<uint16_t>(data_[offset] | (data_[offset + 1] << 8));
            if (ch == 0) break;
            AppendUtf16(result, ch, pendingHigh);
            offset += 2;
        }
        return result;
    }

    // pendingHigh holds a high surrogate until its pair arrives.
    static void AppendUtf16(std::string& out, uint16_t unit, uint16_t& pendingHigh) {
        uint32_t code = unit;
        if (unit >= 0xD800 && unit < 0xDC00) {
            pendingHigh = unit;
            return;
        }
        if (unit >= 0xDC00 && unit < 0xE000) {
            if (pendingHigh == 0) return;
            code = 0x10000 + ((static_cast<uint32_t>(pendingHigh) - 0xD800) << 10) + (unit - 0xDC00);
        }
        pendingHigh = 0;
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    void Skip(std::size_t count) {
        CheckRange(pos_, count);
        pos_ += count;
    }

    void AlignTo(std::size_t alignment) { pos_ = (pos_ + alignment - 1) / alignment * alignment; }

    void Seek(std::size_t offset) {
        CheckRange(offset, 0);
        pos_ = offset;
    }

    std::size_t Tell() const { return pos_; }
    std::size_t Size() const { return data_.size(); }

private:
    void CheckRange(std::size_t offset, std::size_t count) const {
        if (offset + count > data_.size()) {
            throw std::runtime_error("MemoryReader: out of range");
        }
    }

    std::span<const uint8_t> data_;
    std::size_t pos_ = 0;
};

#endif
