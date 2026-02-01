#ifndef REASSETEXPLORER_SCENESAVE_H
#define REASSETEXPLORER_SCENESAVE_H
#include <string>
#include <utility>
#include <vector>

#include "Core/LoadedGame.h"
#include "Core/Rsz/RszTypeDatabase.h"

// Writes every scene an edit touches under outDir/<pak path>, a copy of the game's file with the edited
// via.Transform values patched in place. edits are Transform parameter keys (TransformParamKey) and their
// "x,y,z" values. Returns the files written; error names the first failure.
std::size_t SaveEditedScenes(const LoadedGame& game, const RszTypeDatabase& db,
                             const std::vector<std::pair<std::string, std::string>>& edits, const std::string& outDir,
                             std::string& error);

#endif
