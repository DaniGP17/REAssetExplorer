#include "Explorer/GuiFonts.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Core/IO/MemoryReader.h"
#include "Explorer/Log.h"

namespace {

// config.gcf: after the header, the .ift path, then one .fslt path per
// language, then RE7's inline slot table (languages x slots record pointers).
constexpr std::size_t CONFIG_FSLT_TABLE = 0x58;
constexpr std::size_t CONFIG_LANGUAGES = 33;
constexpr std::size_t CONFIG_SLOTS = 16;
constexpr std::size_t FSLT_SLOTS = 15;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string FindConfig(const LoadedGame& game) {
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            const std::string& path = entry.filePath;
            if ((path.starts_with("natives/stm/ui/config.gcf.") || path.starts_with("natives/stm/gui/config.gcf."))) return path;
        }
    }
    return {};
}

}

std::string FindVersionedPath(const LoadedGame& game, const std::string& sourcePath) {
    std::string prefix = Lower("natives/stm/" + sourcePath) + ".";
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            if (entry.filePath.size() > prefix.size() && Lower(entry.filePath.substr(0, prefix.size())) == prefix) return entry.filePath;
        }
    }
    return {};
}

std::vector<std::vector<GuiFontFace>> GuiFontSlots(const LoadedGame& game, int32_t language) {
    std::vector<std::vector<GuiFontFace>> slots;
    std::string configPath = FindConfig(game);
    if (configPath.empty() || language < 0 || static_cast<std::size_t>(language) >= CONFIG_LANGUAGES) return slots;
    std::vector<uint8_t> config = game.ExtractFile(configPath);
    MemoryReader r(config);
    if (config.size() < 8 || std::memcmp(config.data() + 4, "GCFG", 4) != 0) throw std::runtime_error("not a GUI config (no GCFG)");

    uint64_t fsltOffset = r.ReadAt<uint64_t>(CONFIG_FSLT_TABLE + language * 8);
    std::string fslt = fsltOffset != 0 ? r.ReadWStringAt(fsltOffset) : std::string();
    if (!fslt.empty()) {
        // .fslt: slot offsets, then per slot a count and that many
        // { offset x, offset y, scale x, scale y, path } records.
        std::string fsltPath = FindVersionedPath(game, fslt.starts_with("@") ? fslt.substr(1) : fslt);
        if (fsltPath.empty()) {
            LogWarning("GUI fonts: %s not found", fslt.c_str());
            return slots;
        }
        std::vector<uint8_t> data = game.ExtractFile(fsltPath);
        MemoryReader f(data);
        for (std::size_t s = 0; s < FSLT_SLOTS; s++) {
            std::size_t at = f.ReadAt<uint64_t>(8 + s * 8);
            uint64_t count = f.ReadAt<uint64_t>(at);
            std::vector<GuiFontFace> chain;
            for (uint64_t k = 0; k < count && k < 8; k++) {
                std::size_t record = at + 8 + k * 24;
                GuiFontFace face;
                face.offset[0] = f.ReadAt<float>(record);
                face.offset[1] = f.ReadAt<float>(record + 4);
                face.scale[0] = f.ReadAt<float>(record + 8);
                face.scale[1] = f.ReadAt<float>(record + 12);
                face.path = f.ReadWStringAt(f.ReadAt<uint64_t>(record + 16));
                chain.push_back(std::move(face));
            }
            slots.push_back(std::move(chain));
        }
        return slots;
    }

    // Inline: per slot a count, a scale (x, y), then that many path offsets.
    std::size_t table = CONFIG_FSLT_TABLE + CONFIG_LANGUAGES * 8;
    for (std::size_t s = 0; s < CONFIG_SLOTS; s++) {
        std::size_t at = r.ReadAt<uint64_t>(table + (language * CONFIG_SLOTS + s) * 8);
        uint64_t count = r.ReadAt<uint64_t>(at);
        std::vector<GuiFontFace> chain;
        for (uint64_t k = 0; k < count && k < 8; k++) {
            GuiFontFace face;
            face.scale[0] = r.ReadAt<float>(at + 8);
            face.scale[1] = r.ReadAt<float>(at + 12);
            face.path = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 16 + k * 8));
            chain.push_back(std::move(face));
        }
        slots.push_back(std::move(chain));
    }
    return slots;
}
