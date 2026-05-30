#ifndef REASSETEXPLORER_MESSAGEDATA_H
#define REASSETEXPLORER_MESSAGEDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct MessageAttribute {
    enum Type : int32_t { Empty = -1, Long = 0, Double = 1, String = 2 };
    std::string name;
    int32_t type = Empty;
};

struct MessageEntry {
    std::array<uint8_t, 16> guid{};
    uint32_t hash = 0;  // murmur3 of the name from version 16 on, an index before
    std::string name;
    std::vector<std::string> attributes;  // one per MessageData::attributes, as text
    std::vector<std::string> texts;       // UTF-8, one per MessageData::languages
};

struct MessageData {
    uint32_t version = 0;
    std::vector<int32_t> languages;  // engine language ids (0 Japanese, 1 English...)
    std::vector<MessageAttribute> attributes;
    std::vector<MessageEntry> entries;
};

std::string MessageLanguageName(int32_t language);

#endif
