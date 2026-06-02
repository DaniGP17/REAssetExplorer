#ifndef REASSETEXPLORER_GUIFONTS_H
#define REASSETEXPLORER_GUIFONTS_H
#include <cstdint>
#include <string>
#include <vector>

#include "Core/LoadedGame.h"

// path: .oft source path.
struct GuiFontFace {
    std::string path;
    float offset[2] = { 0, 0 };
    float scale[2] = { 1, 1 };
};

// Per Text FontSlot "SlotN": the font chain, fallbacks after the first.
// RE7 keeps the chains in config.gcf; RE8 names a .fslt per language there.
std::vector<std::vector<GuiFontFace>> GuiFontSlots(const LoadedGame& game, int32_t language);

// "UI/x/y.oft" -> "natives/stm/ui/x/y.oft.1", whatever the version suffix; "" if none.
std::string FindVersionedPath(const LoadedGame& game, const std::string& sourcePath);

#endif
