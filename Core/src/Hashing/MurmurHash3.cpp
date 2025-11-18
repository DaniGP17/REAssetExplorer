#include "Core/Hashing/MurmurHash3.h"

#include <vector>

namespace {
    unsigned char ToUpperAscii(unsigned char c) {
        return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c;
    }

    unsigned char ToLowerAscii(unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }

    void ToLowerInPlace(std::string& str) {
        for (char& c : str) {
            c = static_cast<char>(ToLowerAscii(static_cast<unsigned char>(c)));
        }
    }

    void ToUpperInPlace(std::string& str) {
        for (char& c : str) {
            c = static_cast<char>(ToUpperAscii(static_cast<unsigned char>(c)));
        }
    }
}

std::uint32_t Murmur3::HashAscii(std::string_view str) {
    return InternalHash(reinterpret_cast<const std::uint8_t*>(str.data()), str.size(), DEFAULT_SEED);
}

std::uint32_t Murmur3::MakeHash(std::string_view str, bool lower, bool changeCase) {
    std::string normalized(str);
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    if (changeCase) {
        if (lower) ToLowerInPlace(normalized);
        else ToUpperInPlace(normalized);
    }
    std::vector<std::uint8_t> utf16;
    utf16.reserve(normalized.size() * 2);
    for (const unsigned char c : normalized) {
        utf16.push_back(c);
        utf16.push_back(0);
    }
    return InternalHash(utf16.data(), utf16.size(), DEFAULT_SEED);
}

std::uint32_t Murmur3::InternalHash(const std::uint8_t* data, std::size_t size, std::uint32_t seed) {
    std::uint32_t hash = seed;
    std::size_t currentIndex = 0;
    std::size_t blocks = size / CHUNK_SIZE;

    hash = ProcessBlocks(data, hash, blocks, currentIndex);
    hash = ProcessTail(data, hash, size, currentIndex);

    hash ^= static_cast<std::uint32_t>(size);
    hash = FMix32(hash);

    return hash;
}

std::uint32_t Murmur3::ProcessBlocks(const std::uint8_t* data, std::uint32_t hash, std::size_t blocks, std::size_t& currentIndex) {
    for (std::size_t i = 0; i < blocks; i++) {
        std::uint32_t k1 = static_cast<std::uint32_t>(data[currentIndex])
                          | (static_cast<std::uint32_t>(data[currentIndex + 1]) << 8)
                          | (static_cast<std::uint32_t>(data[currentIndex + 2]) << 16)
                          | (static_cast<std::uint32_t>(data[currentIndex + 3]) << 24);
        currentIndex += CHUNK_SIZE;

        k1 = MixKey(k1);
        hash = MixHash(hash, k1);
    }
    return hash;
}

std::uint32_t Murmur3::ProcessTail(const std::uint8_t* data, std::uint32_t hash, std::size_t size, std::size_t currentIndex) {
    std::size_t remaining = size - currentIndex;
    if (remaining == 0) return hash;

    std::uint32_t tailKey = ExtractTailKey(data, currentIndex, remaining);
    tailKey = MixKey(tailKey);

    return hash ^ tailKey;
}

std::uint32_t Murmur3::ExtractTailKey(const std::uint8_t* data, std::size_t startIndex, std::size_t count) {
    std::uint32_t key = 0;
    for (std::size_t i = count; i-- > 0;) {
        key <<= 8;
        key |= data[startIndex + i];
    }
    return key;
}

std::uint32_t Murmur3::MixKey(std::uint32_t key) {
    key *= C1;
    key = RotateLeft(key, ROTATION_AMOUNT);
    key *= C2;
    return key;
}

std::uint32_t Murmur3::MixHash(std::uint32_t hash, std::uint32_t key) {
    hash ^= key;
    hash = RotateLeft(hash, HASH_ROTATION);
    hash = hash * HASH_MULTIPLIER + MIX_CONSTANT;
    return hash;
}