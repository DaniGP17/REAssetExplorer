#ifndef REASSETEXPLORER_MURMURHASH3_H
#define REASSETEXPLORER_MURMURHASH3_H
#include <cstdint>
#include <string>
#include <string_view>

constexpr std::uint32_t DEFAULT_SEED = 0xFFFFFFFF;
constexpr std::uint32_t C1 = 0xcc9e2d51;
constexpr std::uint32_t C2 = 0x1b873593;
constexpr std::uint32_t MIX_CONSTANT = 0xe6546b64;
constexpr std::size_t   CHUNK_SIZE = 4;
constexpr int ROTATION_AMOUNT = 15;
constexpr int HASH_ROTATION = 13;
constexpr std::uint32_t HASH_MULTIPLIER = 5;

class Murmur3 {
public:
    static std::uint32_t MakeHash(std::string_view str, bool lower = true, bool changeCase = false);
    // The bytes as given: material property and texture slot names.
    static std::uint32_t HashAscii(std::string_view str);

private:
    static std::uint32_t InternalHash(const std::uint8_t* data, std::size_t size, std::uint32_t seed);
    static std::uint32_t ProcessBlocks(const std::uint8_t* data, std::uint32_t hash, std::size_t blocks, std::size_t& currentIndex);
    static std::uint32_t ProcessTail(const std::uint8_t* data, std::uint32_t hash, std::size_t size, std::size_t currentIndex);
    static std::uint32_t ExtractTailKey(const std::uint8_t* data, std::size_t startIndex, std::size_t count);
    static std::uint32_t MixKey(std::uint32_t key);
    static std::uint32_t MixHash(std::uint32_t hash, std::uint32_t key);

    static std::uint32_t RotateLeft(std::uint32_t x, int n) {
        return (x << n) | (x >> (32 - n));
    }

    static std::uint32_t FMix32(std::uint32_t h) {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }
};

#endif
