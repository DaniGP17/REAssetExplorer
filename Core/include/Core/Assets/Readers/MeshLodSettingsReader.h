#ifndef REASSETEXPLORER_MESHLODSETTINGSREADER_H
#define REASSETEXPLORER_MESHLODSETTINGSREADER_H
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "Core/Assets/IAssetReader.h"

struct MeshLodParameter {
    bool present = false;
    float occupancyMin = 0;
    float occupancyMax = 0;
    std::vector<float> occupancyRates;
};

struct MeshLodSettings {
    // Keyed by the u32 at 0x0C of a .mesh header; index = LOD count - 2.
    std::map<uint32_t, std::array<MeshLodParameter, 7>> parameters;

    const MeshLodParameter* Find(uint32_t key, uint32_t lodCount) const;
};

// systems/rendering/meshlodsettings.lod.3, read as via::render::LodResource::initialize does.
class MeshLodSettingsReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".lod.") != std::string_view::npos; }

    MeshLodSettings Read(std::span<const uint8_t> data) const;
};

#endif
