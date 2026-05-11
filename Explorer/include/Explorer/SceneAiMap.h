#ifndef REASSETEXPLORER_SCENEAIMAP_H
#define REASSETEXPLORER_SCENEAIMAP_H
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Core/Assets/AiMapData.h"
#include "Core/LoadedGame.h"
#include "Core/Rsz/RszTypes.h"

struct DebugOverlay;

// Maps are stored in world space (their game objects sit at the origin).
struct SceneAiMap {
    std::string path;   // pak path
    std::string owner;  // SceneObjectKey of the game object
    std::shared_ptr<const AiMapData> data;
};

// Source paths, as the scene stores them.
std::vector<std::string> AiMapReferences(const RszData& rsz, const RszInstance& component);
// Empty when the game has no such map type.
std::string AiMapPakPath(const IGame& game, const std::string& path);
// Skips maps already in out.
void AppendAiMaps(const LoadedGame& game, const RszData& rsz, const RszInstance& component, const std::string& owner,
                  std::vector<SceneAiMap>& out);

const char* AiMapTypeName(AiMapData::Type type);
std::string AiMapGroupName(const AiMapGroup& group);
// Visibility keys the AI map editor toggles.
std::string AiMapGroupKey(bool secondary, std::size_t group);
std::string AiMapLinksKey(bool secondary);
std::string AiMapLayerKey(int layer);  // -1: nodes with no attribute
int AiMapNodeLayer(uint64_t attributes);

struct AiMapStyle {
    uint32_t color = 0xFF6EC83C;  // RGBA8: nodes with no attribute, links
    const std::set<std::string>* hidden = nullptr;
};
void AppendAiMapOverlay(const AiMapData& map, const AiMapStyle& style, DebugOverlay& overlay);

#endif
