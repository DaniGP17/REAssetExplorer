#include "Core/Assets/Readers/MsgReader.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t GMSG_MAGIC = 0x47534D47;
constexpr std::size_t HEADER_SIZE = 72;
constexpr uint8_t STRING_KEY[16] = { 207, 206, 251, 248, 236, 10, 51, 102, 147, 169, 29, 147, 80, 57, 95, 9 };

constexpr const char* LANGUAGE_NAMES[] = {
    "Japanese", "English", "French", "Italian", "German", "Spanish", "Russian", "Polish", "Dutch", "Portuguese",
    "Portuguese (Brazil)", "Korean", "Chinese (Traditional)", "Chinese (Simplified)", "Finnish", "Swedish", "Danish",
    "Norwegian", "Czech", "Hungarian", "Slovak", "Arabic", "Turkish", "Bulgarian", "Greek", "Romanian", "Thai",
    "Ukrainian", "Vietnamese", "Indonesian", "Fiction", "Hindi", "Spanish (Latin America)",
};

void Deobfuscate(std::span<uint8_t> data) {
    uint8_t previous = 0;
    for (std::size_t i = 0; i < data.size(); i++) {
        uint8_t current = data[i];
        data[i] = static_cast<uint8_t>(previous ^ current ^ STRING_KEY[i & 15]);
        previous = current;
    }
}

}

std::string MessageLanguageName(int32_t language) {
    if (language >= 0 && language < static_cast<int32_t>(std::size(LANGUAGE_NAMES))) return LANGUAGE_NAMES[language];
    return "Language " + std::to_string(language);
}

bool MsgReader::IsMessage(std::span<const uint8_t> data) {
    return data.size() >= HEADER_SIZE && MemoryReader(data).ReadAt<uint32_t>(4) == GMSG_MAGIC;
}

MessageData MsgReader::Read(std::span<const uint8_t> input) const {
    if (!IsMessage(input)) throw std::runtime_error("not a message file (no GMSG magic)");
    std::vector<uint8_t> data(input.begin(), input.end());
    MemoryReader header(data);
    MessageData msg;
    msg.version = header.ReadAt<uint32_t>(0);
    int32_t entryCount = header.ReadAt<int32_t>(16);
    int32_t attributeCount = header.ReadAt<int32_t>(20);
    int32_t languageCount = header.ReadAt<int32_t>(24);
    uint64_t stringsOffset = header.ReadAt<uint64_t>(32);
    uint64_t languageOffset = header.ReadAt<uint64_t>(48);
    uint64_t attributeOffset = header.ReadAt<uint64_t>(56);
    uint64_t attributeNameOffset = header.ReadAt<uint64_t>(64);
    if (entryCount < 0 || attributeCount < 0 || languageCount < 0 || languageCount > 64 ||
        stringsOffset > data.size()) {
        throw std::runtime_error("bad message header");
    }
    if (msg.version > 12) Deobfuscate(std::span(data).subspan(static_cast<std::size_t>(stringsOffset)));

    MemoryReader r(data);
    for (int32_t i = 0; i < languageCount; i++) msg.languages.push_back(r.ReadAt<int32_t>(languageOffset + i * 4));
    for (int32_t i = 0; i < attributeCount; i++) {
        MessageAttribute attribute;
        attribute.type = r.ReadAt<int32_t>(attributeOffset + i * 4);
        attribute.name = r.ReadWStringAt(r.ReadAt<uint64_t>(attributeNameOffset + i * 8));
        msg.attributes.push_back(std::move(attribute));
    }

    msg.entries.reserve(entryCount);
    for (int32_t i = 0; i < entryCount; i++) {
        std::size_t at = r.ReadAt<uint64_t>(HEADER_SIZE + i * 8);
        MessageEntry entry;
        auto guid = r.BytesAt(at, 16);
        std::copy(guid.begin(), guid.end(), entry.guid.begin());
        entry.hash = r.ReadAt<uint32_t>(at + 20);
        entry.name = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 24));
        uint64_t values = r.ReadAt<uint64_t>(at + 32);
        for (int32_t l = 0; l < languageCount; l++) {
            entry.texts.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(at + 40 + l * 8)));
        }
        for (int32_t a = 0; a < attributeCount; a++) {
            std::size_t slot = values + a * 8;
            char text[32];
            switch (msg.attributes[a].type) {
            case MessageAttribute::Long:
                std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(r.ReadAt<int64_t>(slot)));
                entry.attributes.emplace_back(text);
                break;
            case MessageAttribute::Double:
                std::snprintf(text, sizeof(text), "%g", r.ReadAt<double>(slot));
                entry.attributes.emplace_back(text);
                break;
            default:
                entry.attributes.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(slot)));
                break;
            }
        }
        msg.entries.push_back(std::move(entry));
    }
    return msg;
}
