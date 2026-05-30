#ifndef REASSETEXPLORER_MESSAGEINDEX_H
#define REASSETEXPLORER_MESSAGEINDEX_H
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/LoadedGame.h"

// Includes unnamed pak entries whose data is a message file (all of RE8's).
class MessageIndex {
public:
    explicit MessageIndex(const LoadedGame& game);

    // language: engine id (1 = English). "" when missing.
    std::string Text(const std::string& guid, int32_t language) const;

private:
    // Texts indexed by language id.
    std::unordered_map<std::string, std::vector<std::string>> texts;
};

#endif
