#ifndef REASSETEXPLORER_FSMMOTIONS_H
#define REASSETEXPLORER_FSMMOTIONS_H
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "Core/Assets/FsmData.h"
#include "Core/LoadedGame.h"

// via.motion.Fsm2ActionPlayMotion and subclasses: v3 bank id, v4 motion id, v5 speed.
struct FsmPlayMotion {
    int32_t bank = 0;
    int32_t motionId = 0;
    float speed = 1;
};
std::optional<FsmPlayMotion> ReadPlayMotion(const RszInstance& action);

// The FSM does not name its motbank (the character's Motion component does), so it
// is guessed: from <character>/fsm/, the one in <character>/motbank/ sharing the
// longest name prefix with the FSM ("ch01_0000base" -> "ch01_0000bank").
class FsmMotions {
public:
    FsmMotions(const LoadedGame& game, const std::string& fsmPakPath);

    const std::string& Motbank() const { return motbankPath; }  // pak path, empty when none was found

    struct Motion {
        std::string motlist;  // pak path, empty when the bank is not in the motbank
        std::string name;     // empty when the motlist has no such id; "(motion tree)", "(empty)"... for slots without a mot
    };
    Motion Find(int32_t bank, int32_t motionId);

private:
    const LoadedGame& game;
    std::string motbankPath;
    std::multimap<int32_t, std::string> motlistsByBank;
    std::map<std::string, std::map<int32_t, std::string>> namesByMotlist;

    const std::map<int32_t, std::string>& Names(const std::string& motlist);
};

#endif
