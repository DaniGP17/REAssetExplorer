#ifndef REASSETEXPLORER_GAMEFACTORY_H
#define REASSETEXPLORER_GAMEFACTORY_H
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/IGame.h"
#include "Games/RE7.h"
#include "Games/RE8.h"

enum class GameId {
    RE7,
    RE8
};

inline std::unique_ptr<IGame> CreateGame(GameId id) {
    switch (id) {
        case GameId::RE7: return std::make_unique<RE7>();
        case GameId::RE8: return std::make_unique<RE8>();
    }
    throw std::runtime_error("CreateGame: unknown game");
}

inline GameId ParseGameId(std::string_view id) {
    if (id == "re7") return GameId::RE7;
    if (id == "re8") return GameId::RE8;
    throw std::runtime_error("unknown game id: " + std::string(id));
}

#endif
