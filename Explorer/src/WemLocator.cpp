#include "Explorer/WemLocator.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "Core/Assets/Readers/WwiseReader.h"

namespace {

constexpr std::string_view SOUND_DIR = "/sound/wwise/";
constexpr std::string_view STREAMING_DIR = "/streaming/sound/wwise/";

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// Copies outside streaming/ carry only the package header.
std::string StreamingCopy(std::string path) {
    std::size_t at = path.find(SOUND_DIR);
    if (at != std::string::npos && !Contains(path, STREAMING_DIR)) path.replace(at, SOUND_DIR.size(), STREAMING_DIR);
    return path;
}

// Language tag after the platform suffix: x.pck.3.x64.ja -> "ja".
std::string LanguageSuffix(const std::string& path) {
    std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string tail = path.substr(dot + 1);
    return tail == "x64" || tail == "stm" || std::all_of(tail.begin(), tail.end(), ::isdigit) ? std::string() : tail;
}

bool IsWholeWem(std::span<const uint8_t> blob) {
    if (blob.size() < 8 || std::memcmp(blob.data(), "RIFF", 4) != 0) return false;
    uint32_t riffSize;
    std::memcpy(&riffSize, blob.data() + 4, 4);
    return static_cast<uint64_t>(riffSize) + 8 <= blob.size();
}

}

std::vector<uint8_t> WemLocator::Read(const std::string& container, uint32_t mediaId) {
    std::vector<uint8_t> out;
    if (Contains(container, ".wem.")) return game.ExtractFile(container);
    if (Contains(container, ".bnk.")) {
        if (FromBank(container, mediaId, out)) return out;
        std::string package = StreamingCopy(container);
        std::size_t at = package.find(".bnk.2.");
        if (at != std::string::npos) package.replace(at, 7, ".pck.3.");
        if (game.FindEntry(package) && FromPackage(package, mediaId, out)) return out;
    } else if (Contains(container, ".pck.")) {
        if (FromPackage(StreamingCopy(container), mediaId, out)) return out;
    }
    if (FromIndex(container, mediaId, out)) return out;
    throw std::runtime_error("media " + std::to_string(mediaId) + " not found in any sound package");
}

bool WemLocator::FromBank(const std::string& bank, uint32_t mediaId, std::vector<uint8_t>& out) const {
    std::vector<uint8_t> data = game.ExtractFile(bank);
    SoundBankData parsed = game.Readers().Get<BnkReader>()->Read(data);
    std::size_t dataChunk = 0;
    for (std::size_t pos = 0; pos + 8 <= data.size();) {
        uint32_t size;
        std::memcpy(&size, data.data() + pos + 4, 4);
        if (std::memcmp(data.data() + pos, "DATA", 4) == 0) {
            dataChunk = pos + 8;
            break;
        }
        pos += 8 + size;
    }
    if (dataChunk == 0) return false;
    for (const SoundBankMedia& media : parsed.media) {
        if (media.id != mediaId || dataChunk + media.offset + media.size > data.size()) continue;
        std::span<const uint8_t> blob(data.data() + dataChunk + media.offset, media.size);
        if (!IsWholeWem(blob)) return false;
        out.assign(blob.begin(), blob.end());
        return true;
    }
    return false;
}

bool WemLocator::FromPackage(const std::string& package, uint32_t mediaId, std::vector<uint8_t>& out) const {
    if (!game.FindEntry(package)) return false;
    std::vector<uint8_t> first = game.ReadFileRange(package, 0, 8);
    SoundPackageData parsed = game.Readers().Get<PckReader>()->ReadHeader(
        game.ReadFileRange(package, 0, PckReader::HeaderBytes(first)));
    for (const SoundPackageEntry& entry : parsed.streams) {
        if (entry.id != mediaId) continue;
        out = game.ReadFileRange(package, entry.offset, entry.size);
        return IsWholeWem(out);
    }
    return false;
}

// Localized media share ids across languages.
bool WemLocator::FromIndex(const std::string& container, uint32_t mediaId, std::vector<uint8_t>& out) {
    std::vector<Located> candidates;
    {
        std::lock_guard lock(indexMutex);
        if (!indexed) {
            indexed = true;
            for (const PakFile& pak : game.Paks()) {
                for (const PakEntry& entry : pak.Entries()) {
                    if (!Contains(entry.filePath, STREAMING_DIR) || !Contains(entry.filePath, ".pck.")) continue;
                    try {
                        std::vector<uint8_t> first = game.ReadFileRange(entry.filePath, 0, 8);
                        SoundPackageData parsed = game.Readers().Get<PckReader>()->ReadHeader(
                            game.ReadFileRange(entry.filePath, 0, PckReader::HeaderBytes(first)));
                        for (const SoundPackageEntry& stream : parsed.streams) {
                            index[static_cast<uint32_t>(stream.id)].push_back({ entry.filePath, stream });
                        }
                    } catch (const std::exception&) {
                    }
                }
            }
        }
        auto it = index.find(mediaId);
        if (it == index.end()) return false;
        candidates = it->second;
    }
    std::string language = LanguageSuffix(container);
    auto score = [&language](const Located& located) {
        std::string own = LanguageSuffix(located.package);
        if (!language.empty() && own == language) return 0;
        if (own.empty()) return 1;
        if (own == "en") return 2;
        return 3;
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&score](const Located& a, const Located& b) { return score(a) < score(b); });
    const Located& best = candidates.front();
    out = game.ReadFileRange(best.package, best.entry.offset, best.entry.size);
    return IsWholeWem(out);
}
