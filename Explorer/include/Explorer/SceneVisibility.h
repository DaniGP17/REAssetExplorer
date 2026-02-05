#ifndef REASSETEXPLORER_SCENEVISIBILITY_H
#define REASSETEXPLORER_SCENEVISIBILITY_H
#include <cstdint>
#include <vector>

#include "Core/Assets/SceneData.h"

// Per node of scene.nodes: 1 when the game starts with that node hidden. Descendants
// of a hidden node are hidden too, but are not marked here.
std::vector<uint8_t> HiddenByDefault(const SceneData& scene);

#endif
