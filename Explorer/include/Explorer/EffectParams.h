#ifndef REASSETEXPLORER_EFFECTPARAMS_H
#define REASSETEXPLORER_EFFECTPARAMS_H
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Assets/EffectData.h"

// Angles are in degrees and colors in 0..1, whatever the file stores.
struct EffectParam {
    std::string id;          // "spawn.count", "billboard.color"...
    std::string section;
    std::string label;
    std::string components;  // comma separated names, empty for a scalar
    std::vector<float> values;
};

std::vector<EffectParam> EffectParams(const EfxEmitter& emitter);
// Integer fields round.
bool SetEffectParam(EfxEmitter& emitter, std::string_view id, std::span<const float> values);

// Same shape as MaterialParamKey: "param:<efx path>|<emitter index>|<id>".
std::string EffectParamKey(const std::string& efxPakPath, std::size_t emitter, std::string_view id);
bool ParseEffectParamKey(std::string_view key, std::string_view efxPakPath, std::size_t& emitter, std::string& id);

#endif
