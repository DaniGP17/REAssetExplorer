#include "Explorer/MessageIndex.h"

#include <cstdio>

#include "Core/Assets/Readers/MsgReader.h"
#include "Explorer/Log.h"

namespace {

std::string GuidText(const std::array<uint8_t, 16>& g) {
    char text[40];
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2],
                  g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return text;
}

}

MessageIndex::MessageIndex(const LoadedGame& game) {
    std::size_t files = 0;
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            const std::string& path = entry.filePath;
            if (path.find(".msg.") == std::string::npos && !path.starts_with("Unknown_")) continue;
            try {
                std::vector<uint8_t> data = game.ExtractFile(path);
                if (!MsgReader::IsMessage(data)) continue;
                MessageData msg = game.Readers().Get<MsgReader>()->Read(data);
                files++;
                for (const MessageEntry& e : msg.entries) {
                    std::vector<std::string>& byLanguage = texts[GuidText(e.guid)];
                    for (std::size_t l = 0; l < msg.languages.size() && l < e.texts.size(); l++) {
                        int32_t language = msg.languages[l];
                        if (language < 0 || language > 64) continue;
                        if (byLanguage.size() <= static_cast<std::size_t>(language)) byLanguage.resize(language + 1);
                        byLanguage[language] = e.texts[l];
                    }
                }
            } catch (const std::exception& e) {
                LogWarning("message index: skip %s: %s", path.c_str(), e.what());
            }
        }
    }
    LogInfo("message index: %zu messages from %zu files", texts.size(), files);
}

std::string MessageIndex::Text(const std::string& guid, int32_t language) const {
    auto it = texts.find(guid);
    if (it == texts.end() || language < 0 || static_cast<std::size_t>(language) >= it->second.size()) return {};
    return it->second[language];
}
