#include "Explorer/FsmMotions.h"

#include <cstdint>
#include <cstring>

#include "Core/Assets/Readers/MotbankReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"

namespace {

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string BaseName(const std::string& path) {
    std::string file = path.substr(path.find_last_of('/') + 1);
    return Lower(file.substr(0, file.find('.')));
}

uint32_t U32(const RszValue& value) {
    const RszBytes* bytes = value.As<RszBytes>();
    if (bytes == nullptr || bytes->size() < 4) return 0;
    uint32_t v;
    std::memcpy(&v, bytes->data(), 4);
    return v;
}

}

std::optional<FsmPlayMotion> ReadPlayMotion(const RszInstance& action) {
    if (action.typeName.find("PlayMotion") == std::string::npos || action.fields.size() < 6) return std::nullopt;
    FsmPlayMotion play;
    play.bank = static_cast<int32_t>(U32(action.fields[3].value));
    play.motionId = static_cast<int32_t>(U32(action.fields[4].value));
    uint32_t speed = U32(action.fields[5].value);
    std::memcpy(&play.speed, &speed, 4);
    return play;
}

FsmMotions::FsmMotions(const LoadedGame& game, const std::string& fsmPakPath) : game(game) {
    std::string lower = Lower(fsmPakPath);
    std::size_t fsmFolder = lower.rfind("/fsm/");
    std::string character = fsmFolder != std::string::npos ? lower.substr(0, fsmFolder + 1)
                                                            : lower.substr(0, lower.find_last_of('/') + 1);
    std::string prefix = character + "motbank/";
    std::string name = BaseName(lower);
    std::size_t bestShared = 0;
    std::size_t bestExtra = SIZE_MAX;
    bool bestIsBank = false;
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            const std::string& path = entry.filePath;
            if (path.size() < prefix.size() || Lower(path.substr(0, prefix.size())) != prefix ||
                path.find(".motbank.") == std::string::npos) continue;
            std::string candidate = BaseName(path);
            std::size_t shared = 0;
            while (shared < candidate.size() && shared < name.size() && candidate[shared] == name[shared]) shared++;
            // Ties go to the name with the fewest extra characters, then to "...bank".
            std::size_t extra = candidate.size() - shared;
            bool isBank = candidate.ends_with("bank");
            if (shared > bestShared || (shared == bestShared && (extra < bestExtra || (extra == bestExtra && isBank && !bestIsBank)))) {
                bestShared = shared;
                bestExtra = extra;
                bestIsBank = isBank;
                motbankPath = path;
            }
        }
    }
    if (motbankPath.empty()) return;
    try {
        MotbankData bank = game.Readers().Get<MotbankReader>()->Read(game.ExtractFile(motbankPath));
        for (const MotbankEntry& entry : bank.entries) {
            motlistsByBank.emplace(entry.bankId, ToPakPath(entry.path, game.Game().GetMotlistExt().c_str()));
        }
    } catch (const std::exception& e) {
        LogWarning("fsm: motbank %s not read: %s", motbankPath.c_str(), e.what());
        motbankPath.clear();
    }
}

const std::map<int32_t, std::string>& FsmMotions::Names(const std::string& motlist) {
    auto [names, inserted] = namesByMotlist.try_emplace(motlist);
    if (!inserted) return names->second;
    try {
        MotlistData list = game.Readers().Get<MotlistReader>()->Read(game.ExtractFile(motlist));
        for (const MotlistEntry& entry : list.entries) {
            if (entry.motion >= 0 && static_cast<std::size_t>(entry.motion) < list.motions.size()) {
                names->second.emplace(entry.motionId, list.motions[entry.motion].name);
            } else {
                names->second.emplace(entry.motionId, "(" + entry.other + ")");
            }
        }
    } catch (const std::exception& e) {
        LogWarning("fsm: motlist %s not read: %s", motlist.c_str(), e.what());
    }
    return names->second;
}

// A real motion wins over a motion tree or empty slot of another motlist of the bank.
FsmMotions::Motion FsmMotions::Find(int32_t bank, int32_t motionId) {
    Motion found;
    auto [first, last] = motlistsByBank.equal_range(bank);
    for (auto it = first; it != last; ++it) {
        const std::map<int32_t, std::string>& names = Names(it->second);
        auto name = names.find(motionId);
        if (name == names.end()) continue;
        if (found.name.empty() || (found.name[0] == '(' && name->second[0] != '(')) {
            found.motlist = it->second;
            found.name = name->second;
        }
    }
    if (found.motlist.empty() && first != last) found.motlist = first->second;
    return found;
}
