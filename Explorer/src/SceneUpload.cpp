#include "Explorer/SceneUpload.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <limits>
#include <map>
#include <numbers>
#include <optional>

#include "Core/Assets/Readers/LightProbeReader.h"
#include "Core/Assets/Readers/SdfReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/TextureDecode.h"
#include "Explorer/Log.h"

namespace {

constexpr uint32_t FILTERED_IBL_WIDTH = 512;
constexpr uint32_t FILTERED_IBL_MIN_WIDTH = 8;
constexpr uint32_t DXGI_R32G32B32A32_FLOAT = 2;

struct FilteredIbl {
    std::vector<std::vector<float>> mips;  // RGBA
    uint32_t width = 0;
    uint32_t height = 0;
};

// Box-filtered mips stand in for the engine's cubemap filter: rough surfaces read a lower one.
FilteredIbl FilterIbl(const std::vector<float>& sky, const std::vector<float>* add, uint32_t sourceWidth,
                      uint32_t sourceHeight, const SceneIbl& ibl) {
    FilteredIbl filtered;
    filtered.width = std::min(FILTERED_IBL_WIDTH, sourceWidth);
    filtered.height = std::max<uint32_t>(filtered.width / 2, 1);
    uint32_t stepX = sourceWidth / filtered.width;
    uint32_t stepY = std::max<uint32_t>(sourceHeight / filtered.height, 1);
    std::vector<float> level(static_cast<std::size_t>(filtered.width) * filtered.height * 4, 0.0f);
    for (uint32_t y = 0; y < filtered.height; y++) {
        for (uint32_t x = 0; x < filtered.width; x++) {
            float sum[3] = {};
            for (uint32_t sy = y * stepY; sy < (y + 1) * stepY && sy < sourceHeight; sy++) {
                for (uint32_t sx = x * stepX; sx < (x + 1) * stepX; sx++) {
                    std::size_t at = (static_cast<std::size_t>(sy) * sourceWidth + sx) * 3;
                    for (int c = 0; c < 3; c++) {
                        sum[c] += sky[at + c] * ibl.exposure + (add ? (*add)[at + c] * ibl.addBlendIntensity : 0.0f);
                    }
                }
            }
            float* out = &level[(static_cast<std::size_t>(y) * filtered.width + x) * 4];
            for (int c = 0; c < 3; c++) out[c] = sum[c] / static_cast<float>(stepX * stepY);
            out[3] = 1;
        }
    }
    filtered.mips.push_back(std::move(level));
    for (uint32_t w = filtered.width / 2, h = filtered.height / 2; w >= FILTERED_IBL_MIN_WIDTH && h >= 1; w /= 2, h /= 2) {
        const std::vector<float>& above = filtered.mips.back();
        uint32_t aboveWidth = w * 2;
        std::vector<float> next(static_cast<std::size_t>(w) * h * 4);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                for (int c = 0; c < 4; c++) {
                    auto texel = [&](uint32_t ax, uint32_t ay) { return above[(static_cast<std::size_t>(ay) * aboveWidth + ax) * 4 + c]; };
                    next[(static_cast<std::size_t>(y) * w + x) * 4 + c] =
                        0.25f * (texel(x * 2, y * 2) + texel(x * 2 + 1, y * 2) + texel(x * 2, y * 2 + 1) + texel(x * 2 + 1, y * 2 + 1));
                }
            }
        }
        filtered.mips.push_back(std::move(next));
    }
    return filtered;
}

// World-space irradiance SH9; texel directions follow CubemapFarPlane2D's mapping.
void ProjectIrradiance(const FilteredIbl& filtered, float rotation, float sh[9][4]) {
    constexpr float pi = std::numbers::pi_v<float>;
    const std::vector<float>& level = filtered.mips.front();
    double sums[9][3] = {};
    float texelAngle = (pi / static_cast<float>(filtered.height)) * (2 * pi / static_cast<float>(filtered.width));
    for (uint32_t y = 0; y < filtered.height; y++) {
        float theta = (static_cast<float>(y) + 0.5f) / static_cast<float>(filtered.height) * pi;
        float weight = std::sin(theta) * texelAngle;
        for (uint32_t x = 0; x < filtered.width; x++) {
            float phi = ((static_cast<float>(x) + 0.5f) / static_cast<float>(filtered.width) - 0.5f + rotation) * 2 * pi;
            float d[3] = { std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
            float basis[9] = { 0.282095f,
                               0.488603f * d[1],
                               0.488603f * d[2],
                               0.488603f * d[0],
                               1.092548f * d[0] * d[1],
                               1.092548f * d[1] * d[2],
                               0.315392f * (3 * d[2] * d[2] - 1),
                               1.092548f * d[0] * d[2],
                               0.546274f * (d[0] * d[0] - d[1] * d[1]) };
            const float* radiance = &level[(static_cast<std::size_t>(y) * filtered.width + x) * 4];
            for (int i = 0; i < 9; i++) {
                for (int c = 0; c < 3; c++) sums[i][c] += radiance[c] * basis[i] * weight;
            }
        }
    }
    const float lobe[9] = { pi, 2 * pi / 3, 2 * pi / 3, 2 * pi / 3, pi / 4, pi / 4, pi / 4, pi / 4, pi / 4 };
    for (int i = 0; i < 9; i++) {
        for (int c = 0; c < 3; c++) sh[i][c] = static_cast<float>(sums[i][c]) * lobe[i];
        sh[i][3] = 0;
    }
}

// slotRaw: byte 5 is the compute register, byte 4 the pixel one, byte 6 the stage mask (0x20 CS, 0x10 PS).
GameComputeDesc ComputeDesc(const SdfProgram& program, bool pixel = false) {
    static const std::pair<std::string_view, GameComputeResource> NAMES[] = {
        { "SceneInfo", GameComputeResource::SceneInfo },
        { "EnvironmentInfo", GameComputeResource::Environment },
        { "LightInfo", GameComputeResource::LightInfo },
        { "CheckerBoardInfo", GameComputeResource::CheckerBoard },
        { "ReadonlyDepth", GameComputeResource::Depth },
        { "LightCullingVolumeSRV", GameComputeResource::CullingVolume },
        { "LightCullingListSRV", GameComputeResource::CullingList },
        { "AmbientBRDF", GameComputeResource::AmbientBrdf },
        { "NormalXNormalYRoughnessMiscSRV", GameComputeResource::Normal },
        { "VelocityXVelocityYOcclusionSubSurfaceSRV", GameComputeResource::Occlusion },
        { "BSPTree", GameComputeResource::ProbeBspTree },
        { "TetraCoordinate", GameComputeResource::ProbeTetrahedra },
        { "IndirectProbe", GameComputeResource::ProbeValues },
        { "IBLCubemap2DArraySRV", GameComputeResource::CubemapArray },
        { "IBLCubemapArrayList2SRV", GameComputeResource::CubemapList },
        { "CubemapSRV", GameComputeResource::Cubemap },
        { "IBLCubemap", GameComputeResource::Cubemap },
        { "IBL2DOct", GameComputeResource::CubemapArray },
        { "ProbeIndexCacheUAV", GameComputeResource::ProbeIndexCache },
        { "GIDUAV", GameComputeResource::Gid },
        { "GISUAV", GameComputeResource::Gis },
    };
    auto resource = [](const std::string& name) {
        for (const auto& [known, value] : NAMES) {
            if (name == known) return value;
        }
        return GameComputeResource::Other;
    };
    GameComputeDesc desc{ pixel ? program.ps : program.cs, {} };
    auto add = [&](uint64_t slotRaw, const std::string& name, GameComputeSlot::Kind kind) {
        if (((slotRaw >> 48) & (pixel ? 0x10 : 0x20)) == 0) return;
        GameComputeSlot slot{ kind, static_cast<uint8_t>((slotRaw >> (pixel ? 32 : 40)) & 0xFF), resource(name) };
        slot.name = name;
        if (kind == GameComputeSlot::Kind::Sampler) SamplerFromName(name, slot.filter, slot.address);
        // As the DeferredLight pass samples it (RE8 capture).
        if (name == "LinearCompare") {
            slot.filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            slot.comparison = D3D12_COMPARISON_FUNC_LESS;
        }
        desc.slots.push_back(slot);
    };
    for (const SdfConstantBuffer& c : program.constantBuffers) add(c.slotRaw, c.name, GameComputeSlot::Kind::Cbv);
    for (const SdfResource& r : program.srvs) add(r.slotRaw, r.name, GameComputeSlot::Kind::Srv);
    for (const SdfResource& r : program.uavs) add(r.slotRaw, r.name, GameComputeSlot::Kind::Uav);
    for (const SdfResource& r : program.samplers) add(r.slotRaw, r.name, GameComputeSlot::Kind::Sampler);
    return desc;
}

std::vector<GameLightingSlot> LightingSlots(const SdfProgram& program) {
    static const std::pair<std::string_view, GameLightingResource> NAMES[] = {
        { "BlueNoise16", GameLightingResource::BlueNoise },
        { "ReadonlyDepth", GameLightingResource::Depth },
        { "LightParameterSRV", GameLightingResource::LightParams },
        { "ShadowParameterSRV", GameLightingResource::ShadowParams },
        { "LightCullingVolumeSRV", GameLightingResource::CullingVolume },
        { "LightCullingListSRV", GameLightingResource::CullingList },
        { "AmbientBRDF", GameLightingResource::AmbientBrdf },
        { "BaseColorMetallicSRV", GameLightingResource::BaseColor },
        { "NormalXNormalYRoughnessMiscSRV", GameLightingResource::Normal },
        { "VelocityXVelocityYOcclusionSubSurfaceSRV", GameLightingResource::Occlusion },
        { "StaticShadowMapSRV", GameLightingResource::StaticShadow },
        { "ShadowMapSRV", GameLightingResource::Shadow },
        { "IESLightTableSRV", GameLightingResource::Ies },
        { "GIDSRV", GameLightingResource::Gid },
        { "GISSRV", GameLightingResource::Gis },
        { "LinearCompare", GameLightingResource::CompareSampler },
        { "ShadowSamplingRotation", GameLightingResource::ShadowRotation },
    };
    auto resource = [](const std::string& name) {
        for (const auto& [known, value] : NAMES) {
            if (name == known) return value;
        }
        return GameLightingResource::Other;
    };
    // slotRaw: byte 4 is the PS register, byte 6 the stage mask (0x10 PS).
    std::vector<GameLightingSlot> slots;
    auto add = [&](const SdfResource& r, bool sampler) {
        if (((r.slotRaw >> 48) & 0x10) == 0) return;
        slots.push_back({ sampler, static_cast<uint8_t>((r.slotRaw >> 32) & 0xFF), resource(r.name) });
    };
    for (const SdfResource& r : program.srvs) add(r, false);
    for (const SdfResource& r : program.samplers) add(r, true);
    for (const SdfConstantBuffer& c : program.constantBuffers) {
        if (((c.slotRaw >> 48) & 0x10) == 0) continue;
        slots.push_back({ false, static_cast<uint8_t>((c.slotRaw >> 32) & 0xFF), resource(c.name), true });
    }
    return slots;
}

struct PreparedIbl {
    TextureData sky;
    std::optional<TextureData> add;
    FilteredIbl filtered;
    ViewerSceneIbl params;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::optional<PreparedIbl> PrepareSceneIbl(const LoadedGame& game, const SceneIbl& ibl) {
    const TexReader* reader = game.Readers().Get<TexReader>();
    std::string skyPath = ToPakPath(ibl.texture, game.Game().GetTexExt().c_str());
    if (!game.FindEntry(skyPath)) return std::nullopt;
    PreparedIbl prepared;
    prepared.sky = reader->Read(game.ExtractFile(skyPath));
    std::vector<float> skyRgb;
    uint32_t& width = prepared.width;
    uint32_t& height = prepared.height;
    if (!DecodeTextureHdr(prepared.sky, 0, skyRgb, width, height)) return std::nullopt;

    std::optional<TextureData>& add = prepared.add;
    std::vector<float> addRgb;
    if (!ibl.addBlendTexture.empty() && ibl.addBlendIntensity > 0) {
        std::string addPath = ToPakPath(ibl.addBlendTexture, game.Game().GetTexExt().c_str());
        uint32_t addWidth = 0;
        uint32_t addHeight = 0;
        if (game.FindEntry(addPath)) {
            add = reader->Read(game.ExtractFile(addPath));
            if (!DecodeTextureHdr(*add, 0, addRgb, addWidth, addHeight) || addWidth != width || addHeight != height) {
                add.reset();
            }
        }
    }

    prepared.filtered = FilterIbl(skyRgb, add ? &addRgb : nullptr, width, height, ibl);
    ViewerSceneIbl& params = prepared.params;
    params.exposure = ibl.exposure;
    params.rotation = ibl.rotation / 180.0f * std::numbers::pi_v<float>;
    params.addBlend = add ? ibl.addBlendIntensity : 0.0f;
    params.virtualOffset = ibl.virtualOffset;
    float trimWidth = ibl.trim[2] - ibl.trim[0];
    float trimHeight = ibl.trim[3] - ibl.trim[1];
    if (trimWidth > 0 && trimHeight > 0) {
        params.trimScale[0] = 1.0f / trimWidth;
        params.trimScale[1] = 1.0f / trimHeight;
        params.trimOffset[0] = -ibl.trim[0] / trimWidth;
        params.trimOffset[1] = -ibl.trim[1] / trimHeight;
    }
    ProjectIrradiance(prepared.filtered, params.rotation, params.irradianceSh);
    return prepared;
}

bool UploadSceneIbl(Viewer& viewer, const SceneIbl& ibl, const std::optional<PreparedIbl>& prepared) {
    if (!prepared) return false;
    const FilteredIbl& filtered = prepared->filtered;
    GameTextureDesc filteredDesc{ filtered.width, filtered.height, DXGI_R32G32B32A32_FLOAT, 1, {} };
    for (std::size_t i = 0; i < filtered.mips.size(); i++) {
        uint32_t mipWidth = std::max<uint32_t>(filtered.width >> i, 1);
        filteredDesc.mips.push_back({ std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(filtered.mips[i].data()),
                                                                filtered.mips[i].size() * sizeof(float)),
                                      mipWidth * 16 });
    }
    const ViewerSceneIbl& params = prepared->params;
    GameTextureDesc addDesc{};
    if (prepared->add) addDesc = ToGameTexture(*prepared->add);
    viewer.LoadSceneIbl(ToGameTexture(prepared->sky), prepared->add ? &addDesc : nullptr, filteredDesc, params);
    LogInfo("scene IBL: %s (%ux%u) exposure %g, add x%g, mean irradiance %g %g %g", ibl.texture.c_str(), prepared->width,
            prepared->height, ibl.exposure, params.addBlend, params.irradianceSh[0][0] * 0.282095f,
            params.irradianceSh[0][1] * 0.282095f, params.irradianceSh[0][2] * 0.282095f);
    return true;
}

constexpr std::size_t PROBE_GRID_MAX_CELLS = 1u << 22;
constexpr float PROBE_GRID_MIN_CELL = 2.0f;
constexpr uint32_t PROBE_WALK_STEPS = 1u << 16;

// Steps toward p through the neighbor across the face with the most negative weight.
uint32_t WalkTetrahedra(const ProbeNetworkData& network, uint32_t t, const float p[3]) {
    for (uint32_t step = 0; step < PROBE_WALK_STEPS; step++) {
        const ProbeTetrahedron& tetra = network.tetrahedra[t];
        float w[4];
        float d[3] = { p[0] - tetra.rows[0][3], p[1] - tetra.rows[1][3], p[2] - tetra.rows[2][3] };
        for (int i = 0; i < 3; i++) w[i] = tetra.rows[i][0] * d[0] + tetra.rows[i][1] * d[1] + tetra.rows[i][2] * d[2];
        w[3] = 1 - w[0] - w[1] - w[2];
        int lowest = static_cast<int>(std::min_element(w, w + 4) - w);
        if (w[lowest] >= -1e-4f) return t;
        uint32_t next = tetra.neighbors[lowest];
        if (next >= network.tetrahedra.size()) return t;
        t = next;
    }
    return t;
}

// Header: origin xyz, cell size, dims xyz, 0; then per cell the tetrahedron containing its center.
std::vector<uint32_t> BuildProbeGrid(const ProbeNetworkData& network) {
    float lo[3] = { 1e30f, 1e30f, 1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const std::array<float, 3>& p : network.positions) {
        for (int c = 0; c < 3; c++) {
            lo[c] = std::min(lo[c], p[c]);
            hi[c] = std::max(hi[c], p[c]);
        }
    }
    float cell = PROBE_GRID_MIN_CELL;
    uint32_t dims[3];
    for (;;) {
        for (int c = 0; c < 3; c++) dims[c] = std::max<uint32_t>(static_cast<uint32_t>(std::ceil((hi[c] - lo[c]) / cell)), 1);
        if (static_cast<std::size_t>(dims[0]) * dims[1] * dims[2] <= PROBE_GRID_MAX_CELLS) break;
        cell *= 1.25f;
    }
    std::vector<uint32_t> grid(8 + static_cast<std::size_t>(dims[0]) * dims[1] * dims[2], UINT32_MAX);
    std::memcpy(grid.data(), lo, 12);
    std::memcpy(grid.data() + 3, &cell, 4);
    std::memcpy(grid.data() + 4, dims, 12);
    grid[7] = 0;
    uint32_t planeStart = 0;
    for (uint32_t z = 0; z < dims[2]; z++) {
        uint32_t rowStart = planeStart;
        for (uint32_t y = 0; y < dims[1]; y++) {
            uint32_t t = rowStart;
            for (uint32_t x = 0; x < dims[0]; x++) {
                float p[3] = { lo[0] + (x + 0.5f) * cell, lo[1] + (y + 0.5f) * cell, lo[2] + (z + 0.5f) * cell };
                t = WalkTetrahedra(network, t, p);
                grid[8 + (static_cast<std::size_t>(z) * dims[1] + y) * dims[0] + x] = t;
                if (x == 0) rowStart = t;
                if (x == 0 && y == 0) planeStart = t;
            }
        }
    }
    return grid;
}

float HalfToFloat(uint32_t h) {
    h = std::min<uint32_t>(h, 0x7BFF);
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;
    if (exponent == 0) return std::ldexp(static_cast<float>(mantissa), -24);
    uint32_t bits = ((exponent + 112) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// f32tof16 with round to nearest even, as the GPU's LegacyF32ToF16.
uint32_t FloatToHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int exponent = static_cast<int>((bits >> 23) & 0xFF) - 112;
    uint32_t mantissa = bits & 0x7FFFFF;
    if (exponent >= 31) return sign | 0x7C00;
    if (exponent <= 0) {
        if (exponent < -10) return sign;
        mantissa |= 0x800000;
        uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        uint32_t rest = mantissa & ((1u << shift) - 1);
        uint32_t midpoint = 1u << (shift - 1);
        if (rest > midpoint || (rest == midpoint && (half & 1))) half++;
        return sign | half;
    }
    uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
    uint32_t rest = mantissa & 0x1FFF;
    if (rest > 0x1000 || (rest == 0x1000 && (half & 1))) half++;
    return half;
}

bool InsideObb(const float obb[20], const std::array<float, 3>& p) {
    float rel[3] = { p[0] - obb[12], p[1] - obb[13], p[2] - obb[14] };
    for (int axis = 0; axis < 3; axis++) {
        float d = (rel[0] * obb[axis * 4] + rel[1] * obb[axis * 4 + 1] + rel[2] * obb[axis * 4 + 2]) / obb[16 + axis];
        if (!(std::fabs(d) < 1)) return false;
    }
    return true;
}

}

std::vector<uint32_t> BuildLightProbePool(const LoadedGame& game, const std::vector<SceneLightProbes>& components,
                                          const ProbeNetworkData& network) {
    std::vector<uint32_t> pool(network.positions.size() * 12, 0);
    if (components.empty()) return pool;
    std::vector<const SceneLightProbes*> ordered;
    for (const SceneLightProbes& c : components) {
        if (c.network == components.front().network) ordered.push_back(&c);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->priority < b->priority; });
    std::map<std::string, std::vector<uint32_t>> sources;
    for (const SceneLightProbes* c : ordered) {
        auto it = sources.find(c->probes);
        if (it == sources.end()) {
            std::string path = ToPakPath(c->probes, ".6");
            std::vector<uint32_t> values;
            if (game.FindEntry(path)) values = game.Readers().Get<LightProbeReader>()->Read(game.ExtractFile(path)).values;
            it = sources.emplace(c->probes, std::move(values)).first;
        }
        if (it->second.size() != pool.size()) continue;
        float scale[3];
        float floor[3];
        for (int i = 0; i < 3; i++) {
            scale[i] = c->probeColor[i] * c->intensity;
            floor[i] = c->ambientColor[i] * c->ambientColorIntensity;
        }
        for (std::size_t p = 0; p < network.positions.size(); p++) {
            if (!InsideObb(c->obb, network.positions[p])) continue;
            for (std::size_t v = p * 12; v < p * 12 + 12; v++) {
                uint32_t src = it->second[v];
                float rgb[3] = { HalfToFloat((src << 4) & 0x7FF0), HalfToFloat((src >> 7) & 0x7FFF), HalfToFloat(src >> 17) };
                for (int i = 0; i < 3; i++) rgb[i] = std::max(rgb[i] * scale[i], floor[i]);
                float luminance = rgb[0] * 0.25f + rgb[1] * 0.5f + rgb[2] * 0.25f;
                for (float& x : rgb) x = luminance + (x - luminance) * c->saturate;
                pool[v] = ((std::min<uint32_t>(FloatToHalf(rgb[0]), 0x7BFF) >> 4) & 0x7FF) |
                          ((std::min<uint32_t>(FloatToHalf(rgb[1]), 0x7BFF) << 7) & 0x3FF800) |
                          ((std::min<uint32_t>(FloatToHalf(rgb[2]), 0x7BFF) >> 5) << 22);
            }
        }
    }
    return pool;
}

namespace {

struct PreparedProbes {
    ProbeNetworkData network;
    std::vector<uint32_t> values;
    std::vector<uint32_t> grid;
};

std::optional<PreparedProbes> PrepareLightProbes(const LoadedGame& game, const std::vector<SceneLightProbes>& components) {
    std::string networkPath = ToPakPath(components.front().network, ".9");
    if (!game.FindEntry(networkPath)) return std::nullopt;
    PreparedProbes prepared;
    prepared.network = game.Readers().Get<ProbeNetworkReader>()->Read(game.ExtractFile(networkPath));
    if (prepared.network.tetrahedra.empty()) return std::nullopt;
    prepared.values = BuildLightProbePool(game, components, prepared.network);
    prepared.grid = BuildProbeGrid(prepared.network);
    return prepared;
}

bool UploadLightProbes(Viewer& viewer, const std::vector<SceneLightProbes>& components,
                       const std::optional<PreparedProbes>& prepared) {
    if (!prepared) return false;
    const ProbeNetworkData& network = prepared->network;
    const std::vector<uint32_t>& values = prepared->values;
    const std::vector<uint32_t>& grid = prepared->grid;
    std::span<const uint32_t> tetrahedra(reinterpret_cast<const uint32_t*>(network.tetrahedra.data()),
                                         network.tetrahedra.size() * sizeof(ProbeTetrahedron) / 4);
    viewer.LoadLightProbes(tetrahedra, values, grid, network.bspTree);
    float cell;
    std::memcpy(&cell, grid.data() + 3, 4);
    LogInfo("light probes: %s (%zu probes, %zu tetrahedra, grid %ux%ux%u of %.1f m)", components.front().network.c_str(),
            network.positions.size(), network.tetrahedra.size(), grid[4], grid[5], grid[6], cell);
    return true;
}

}

struct LightProbeNetworks::Network {
    std::vector<SceneLightProbes> components;
    std::optional<PreparedProbes> prepared;
    bool loaded = false;
};

LightProbeNetworks::LightProbeNetworks(const LoadedGame& game, const std::vector<SceneLightProbes>& components) : game(game) {
    for (const SceneLightProbes& c : components) {
        auto it = std::find_if(networks.begin(), networks.end(), [&](const auto& n) { return n->components.front().network == c.network; });
        if (it == networks.end()) {
            networks.push_back(std::make_unique<Network>());
            it = networks.end() - 1;
        }
        (*it)->components.push_back(c);
    }
}

LightProbeNetworks::~LightProbeNetworks() = default;

void LightProbeNetworks::Update(Viewer& viewer, const float point[3]) {
    std::array<float, 3> p{ point[0], point[1], point[2] };
    auto contains = [&](const Network& n) {
        return std::any_of(n.components.begin(), n.components.end(), [&](const SceneLightProbes& c) { return InsideObb(c.obb, p); });
    };
    if (networks.empty() || contains(*networks[current])) return;
    for (std::size_t i = 0; i < networks.size(); i++) {
        Network& n = *networks[i];
        if (i == current || !contains(n)) continue;
        try {
            if (!n.loaded) {
                n.prepared = PrepareLightProbes(game, n.components);
                n.loaded = true;
            }
            if (UploadLightProbes(viewer, n.components, n.prepared)) current = i;
        } catch (const std::exception& e) {
            LogWarning("light probes not loaded: %s", e.what());
        }
        return;
    }
}

namespace {

// LocalCubemap::setOBB's GPU OBB (world to [-1, 1] box rows, the OBB or its bounding AABB), the
// GameObject position and LightRenderer::updateLocalCubemap's f16 pair: 1 / max(1 - BlendWeight, 0.001)
// and slot + 1, negative when distant.
struct PreparedCubemaps {
    SdfData develop;
    std::vector<TextureData> textures;
    std::vector<float> records;
};

PreparedCubemaps PrepareLocalCubemaps(const LoadedGame& game, const std::vector<SceneLocalCubemap>& cubemaps) {
    PreparedCubemaps prepared;
    prepared.develop = game.Readers().Get<SdfReader>()->Read(
        game.ExtractFile("natives/stm/systems/shader/systemdevelop.sdf" + game.Game().GetMasterExt()));
    std::vector<TextureData>& textures = prepared.textures;
    std::vector<float>& records = prepared.records;
    for (const SceneLocalCubemap& c : cubemaps) {
        if (c.texture.empty()) continue;
        std::string path = ToPakPath(c.texture, game.Game().GetTexExt().c_str());
        if (!game.FindEntry(path)) continue;
        TextureData tex = game.Readers().Get<TexReader>()->Read(game.ExtractFile(path));
        if (tex.NumImages() != 6) continue;
        textures.push_back(std::move(tex));
        const float* o = c.obb;
        float row[12] = {};
        if (c.obbBoundary) {
            for (int a = 0; a < 3; a++) {
                float extent = o[16 + a];
                for (int k = 0; k < 3; k++) row[a * 4 + k] = o[a * 4 + k] / extent;
                row[a * 4 + 3] = -(o[12] * o[a * 4] + o[13] * o[a * 4 + 1] + o[14] * o[a * 4 + 2]) / extent;
            }
        } else {
            for (int a = 0; a < 3; a++) {
                float radius = 0;
                for (int k = 0; k < 3; k++) radius += std::fabs(o[k * 4 + a]) * o[16 + k];
                float lo = o[12 + a] - radius;
                float hi = o[12 + a] + radius;
                float center = (lo + hi) * 0.5f;
                row[a * 4 + a] = 1 / (hi - center);
                row[a * 4 + 3] = -center / (hi - center);
            }
        }
        float slot = static_cast<float>(records.size() / 16 + 1);
        uint32_t halves = FloatToHalf(1 / std::max(1 - c.blendWeight, 0.001f)) | FloatToHalf(c.distant ? -slot : slot) << 16;
        float packed;
        std::memcpy(&packed, &halves, 4);
        records.insert(records.end(), row, row + 12);
        records.insert(records.end(), { c.position[0], c.position[1], c.position[2], packed });
    }
    return prepared;
}

void UploadLocalCubemaps(Viewer& viewer, const std::vector<SceneLocalCubemap>& cubemaps, const PreparedCubemaps& prepared) {
    const SdfProgram* convert = prepared.develop.FindProgram("CubemapTo2DOct");
    if (convert == nullptr || convert->cs.empty()) throw std::runtime_error("CubemapTo2DOct not found");
    std::vector<GameTextureDesc> cubes;
    for (const TextureData& tex : prepared.textures) cubes.push_back(ToGameTexture(tex));
    viewer.LoadLocalCubemaps(ComputeDesc(*convert), cubes, prepared.records);
    LogInfo("local cubemaps: %zu of %zu", cubes.size(), cubemaps.size());
}

// CACAOImplement's FFX_CACAO_DEFAULT_SETTINGS (re8.exe constructor) at the highest quality (Q3 base + adaptive Q3);
// ApplySsao sets the per-frame radius and shadow multiplier.
void UploadCacao(Viewer& viewer, const LoadedGame& game, const SceneSsao& ssao) {
    if (!ssao.controlFound || !ssao.controlEnabled) return;
    const SdfData cacao = game.Readers().Get<SdfReader>()->Read(
        game.ExtractFile("natives/stm/systems/shader/ffx_cacao.sdf" + game.Game().GetMasterExt()));
    GameCacaoSettings settings{};
    settings.shadowPower = 1.0f;
    settings.shadowClamp = 0.98f;
    settings.horizonAngleThreshold = 0.06f;
    settings.fadeOutFrom = 50.0f;
    settings.fadeOutTo = 300.0f;
    settings.adaptiveQualityLimit = 0.45f;
    settings.sharpness = 0.98f;
    settings.detailShadowStrength = 0.5f;
    settings.bilateralSigmaSquared = 5.0f;
    settings.bilateralSimilarityDistanceSigma = 0.1f;
    settings.blurPassCount = 2;
    auto program = [&](const std::string& name) {
        const SdfProgram* p = cacao.FindProgram(name);
        if (p == nullptr || p->cs.empty()) throw std::runtime_error(name + " not found");
        return ComputeDesc(*p);
    };
    // GenerateSSAO0..4 are CACAO's Q0..Q3 and Q3 base; EdgeSensitiveBlurN runs N + 1 passes.
    GameCacaoDesc desc{ program("PrepareDownsampledDepthsAndMips"), program("PrepareDownsampledNormalsFromInputNormals"),
                        program("GenerateSSAO4"), program("GenerateSSAO3"), program("GenerateImportanceMap"),
                        program("PostprocessImportanceMapA"), program("PostprocessImportanceMapB"),
                        program("EdgeSensitiveBlur" + std::to_string(settings.blurPassCount - 1)), program("UpscaleBilateral5x5"),
                        settings };
    viewer.CreateCacao(desc);
    ApplySsao(viewer, ssao, ssao);
    LogInfo("CACAO: radius %g, shadow multiplier %g", std::min(0.05f * ssao.radius, 2.0f), 5.0f * ssao.intensity);
}

// The MainCamera's via.render.Fog, drawn as Fog::draw does.
void UploadFog(Viewer& viewer, const LoadedGame& game, const SceneBuild& build) {
    if (!build.fog.found) return;
    const SceneFog& fog = build.fog;
    if (fog.separateSky) LogWarning("fog: SeparateSky (FogTerrain + FogSky) not supported, drawing Fog");
    if (fog.maskTextureEnabled && !fog.maskTexture.empty()) LogWarning("fog: mask texture not supported");
    if (fog.fsBlendRate > 0) LogWarning("fog: FogScattering not supported");
    const SdfData sdf = game.Readers().Get<SdfReader>()->Read(
        game.ExtractFile("natives/stm/systems/shader/fog.sdf" + game.Game().GetMasterExt()));
    const SdfProgram* program = sdf.FindProgram("Fog");
    if (program == nullptr || program->ps.empty()) throw std::runtime_error("Fog not found");
    viewer.CreateFog({ program->vs, ComputeDesc(*program, true) });
    ApplyFog(viewer, fog, build.fogZone);
}

// VolumetricFogControl: the programs updateShaderResource names and the blue noise tables the Implementation
// constructor loads (scrambling and ranking at 4 spp).
void UploadVolumetricFog(Viewer& viewer, const LoadedGame& game, const SceneBuild& build) {
    const SceneVolumetricFogControl& control = build.volumetricFogControl;
    if (!control.found) return;
    const SdfData sdf = game.Readers().Get<SdfReader>()->Read(
        game.ExtractFile("natives/stm/systems/shader/volumetricrendering.sdf" + game.Game().GetMasterExt()));
    auto program = [&](const std::string& name) {
        const SdfProgram* p = sdf.FindProgram(name);
        if (p == nullptr || p->cs.empty()) throw std::runtime_error(name + " not found");
        return ComputeDesc(*p);
    };
    std::string inject = std::string("InjectShadedVolumeData") + (control.shadowEnabled ? "_PunctualShadow" : "_DirectionalShadow") +
                         (control.shadowQuality == 1 ? "_ShadowHigh" : "_ShadowMiddle") +
                         (control.ambientLightEnabled ? "_AmbientLight" : "");
    std::string integrate = std::string("IntegrateFroxelContribution") + (control.integrationType == 1 ? "_Accurate" : "_Blurry");
    auto table = [&](const std::string& name) {
        std::string path = "natives/stm/systems/rendering/bluenoise/" + name + ".gpbf.1";
        if (!game.FindEntry(path)) throw std::runtime_error(path + " not found");
        return game.ExtractFile(path);
    };
    std::vector<uint8_t> sobol = table("sobol_256spp_256d");
    std::vector<uint8_t> scrambling = table("scramblingtile_4spp");
    std::vector<uint8_t> ranking = table("rankingtile_4spp");
    viewer.CreateVolumetricFog({ program(inject), program(integrate), 4, sobol, scrambling, ranking });
    std::vector<float> scattering = build.lightScattering;
    scattering.insert(scattering.end(), build.cameraLightScattering.begin(), build.cameraLightScattering.end());
    viewer.SetVolumetricScattering(scattering);
    ApplyVolumetricFog(viewer, control, build.applicationVolumetricFog, build.volumetricFogs, build.volumetricFogZone);
    LogInfo("volumetric fog: %s + %s, %zu scene media", inject.c_str(), integrate.c_str(), build.volumetricFogs.size());
}

// ToneMappingImplement's LDR tail: LwLDRPostProcess (WithToneMap, C4L when Vignetting is Enable or KerarePlus,
// 3Section as the RE8 capture draws it), FXAA, CAS and the sRGB screen output; LDRColorCorrect's color cubes.
void UploadPostProcess(Viewer& viewer, const LoadedGame& game, const SceneBuild& build) {
    if (!build.toneMapping.found || !build.colorCorrect.found) return;
    auto load = [&](const std::string& file) {
        return game.Readers().Get<SdfReader>()->Read(
            game.ExtractFile("natives/stm/systems/shader/" + file + ".sdf" + game.Game().GetMasterExt()));
    };
    const SdfData advanced = load("advancedsystem");
    const SdfData tonemap = load("tonemap");
    const SdfData output = load("screenoutput");
    auto program = [](const SdfData& sdf, const std::string& name) {
        const SdfProgram* p = sdf.FindProgram(name);
        if (p == nullptr) throw std::runtime_error(name + " not found");
        return p;
    };
    auto vertex = [](const SdfProgram& p) {
        GameComputeDesc desc{ p.vs, {} };
        for (const SdfConstantBuffer& c : p.constantBuffers) {
            if ((c.slotRaw >> 48) & 0x01) desc.slots.push_back({ GameComputeSlot::Kind::Cbv, static_cast<uint8_t>(c.slotRaw & 0xFF), {}, {}, {}, {}, c.name });
        }
        for (const SdfResource& r : p.srvs) {
            if ((r.slotRaw >> 48) & 0x01) desc.slots.push_back({ GameComputeSlot::Kind::Srv, static_cast<uint8_t>(r.slotRaw & 0xFF), {}, {}, {}, {}, r.name });
        }
        return desc;
    };
    std::string ldrName = std::string("LwLDRPostProcessWithToneMap") + (build.toneMapping.vignetting <= 1 ? "C4L" : "") + "3Section";
    const SdfProgram* ldr = program(advanced, ldrName);
    const SdfProgram* fxaa = program(advanced, "FXAA");
    const SdfProgram* cas = program(tonemap, "CASWithoutScaling");
    const SdfProgram* screen = program(output, "ScreenOutputSRGB");
    GamePostProcessDesc desc{ ComputeDesc(*program(tonemap, "Histogram")), ComputeDesc(*program(tonemap, "WhitePoint")), vertex(*ldr),
                              ComputeDesc(*ldr, true), vertex(*fxaa), ComputeDesc(*fxaa, true), ComputeDesc(*cas), vertex(*screen),
                              ComputeDesc(*screen, true) };
    const SdfProgram* bloomFinal = program(advanced, "SoftBloom_NewFinal");
    desc.bloomVs = vertex(*bloomFinal);
    desc.bloomReduction = ComputeDesc(*program(advanced, "SoftBloom_NewReduction"), true);
    desc.bloomFilter = ComputeDesc(*program(advanced, "SoftBloom_NewFilter"), true);
    static const char* TIERS[7] = { "Zero", "One", "Two", "Three", "Four", "Five", "Six" };
    for (int i = 0; i < 7; i++) {
        desc.bloomBlending[i] = ComputeDesc(*program(advanced, std::string("SoftBloom_NewBlendingTier") + TIERS[i]), true);
    }
    desc.bloomFinal = ComputeDesc(*bloomFinal, true);
    static const char* TEMPORAL_ALGORITHMS[4] = { "PreTonemap", "NewPreTonemap", "NewV2PreTonemap", "SharpPreTonemap" };
    const SceneToneMapping& toneMapping = build.toneMapping;
    if (toneMapping.temporalAA != 0 && toneMapping.temporalAAAlgorithm < 4) {
        std::string name = std::string(TEMPORAL_ALGORITHMS[toneMapping.temporalAAAlgorithm]) +
                           (toneMapping.neighborhoodClamp ? "VarianceClip" : "");
        const SdfProgram* temporal = program(tonemap, name);
        desc.temporalVs = vertex(*temporal);
        desc.temporalPs = ComputeDesc(*temporal, true);
    }
    viewer.CreatePostProcess(desc);
    ApplyTemporalAA(viewer, toneMapping);
    const SceneSoftBloom& bloom = build.softBloom;
    if (bloom.found && (bloom.algorithm != 2 || bloom.lwMode || bloom.highPrecision || bloom.dirtMaskIntensity > 0 && !bloom.dirtMask.empty())) {
        LogWarning("SoftBloom algorithm %u (LW %d, high precision %d, dirt mask '%s') is not supported", bloom.algorithm,
                   bloom.lwMode ? 1 : 0, bloom.highPrecision ? 1 : 0, bloom.dirtMask.c_str());
    }
    ApplySoftBloom(viewer, bloom, build.softBloomZone);
    std::string metering = ToPakPath(build.toneMapping.meteringTexture, game.Game().GetTexExt().c_str());
    if (!build.toneMapping.meteringTexture.empty() && game.FindEntry(metering)) {
        viewer.LoadMeteringTexture(ToGameTexture(game.Readers().Get<TexReader>()->Read(game.ExtractFile(metering))));
    } else {
        LogWarning("metering texture '%s' not found", build.toneMapping.meteringTexture.c_str());
    }
    const SceneColorCorrect& cc = build.colorCorrect;
    std::vector<TextureData> cubes;
    for (const std::string& path : ColorCubePalette(cc)) {
        std::string pak = ToPakPath(path, game.Game().GetTexExt().c_str());
        if (!game.FindEntry(pak)) {
            LogWarning("color cube %s not found", pak.c_str());
            pak = ToPakPath(cc.cube0, game.Game().GetTexExt().c_str());
        }
        cubes.push_back(game.Readers().Get<TexReader>()->Read(game.ExtractFile(pak)));
    }
    std::vector<GameTextureDesc> descs;
    for (const TextureData& cube : cubes) descs.push_back(ToGameTexture(cube));
    viewer.LoadColorCubes(descs);
    std::string target = ColorCubeTarget(cc, build.colorCorrectZone);
    if (target.empty()) ApplyPostProcess(viewer, build.toneMapping, cc, build.toneMap, cc.cube0, cc.cube1, cc.blendRate);
    else ApplyPostProcess(viewer, build.toneMapping, cc, build.toneMap, target, target, 1.0f);
    LogInfo("post process: %s, color cube %s", ldrName.c_str(), target.empty() ? cc.cube1.c_str() : target.c_str());
}

}

void ApplyToneMap(Viewer& viewer, const SceneToneMap& map, bool resetExposure) {
    viewer.SetToneMap({ std::exp2(-map.ev), map.contrast, map.linearBegin, map.linearLength, map.toe, map.autoExposure,
                        map.minWhitePoint, map.maxWhitePoint, map.whiteRange, map.brightAdaptationRate, map.darkAdaptationRate },
                      resetExposure);
}

// app.SSAOController: the zone's _Enabled and _AOIntensity on SSAOControl; renderCACAO's radius min(0.05 * AORadius, 2)
// and shadow multiplier 5 * AOIntensity.
void ApplySsao(Viewer& viewer, const SceneSsao& control, const SceneSsao& zone) {
    float intensity = zone.zoneFound ? zone.intensity : control.intensity;
    bool enabled = control.controlEnabled && (!zone.zoneFound || zone.zoneEnabled);
    viewer.SetCacaoFrame(std::min(0.05f * control.radius, 2.0f), 5.0f * intensity, enabled);
}

// app.FogController sets the zone's fields on via.render.Fog; FogParam as Fog::update fills it.
void ApplyFog(Viewer& viewer, const SceneFog& component, const SceneFog& zone) {
    SceneFog fog = component;
    if (zone.found) {
        fog.enabled = zone.enabled;
        std::copy_n(zone.color, 3, fog.color);
        fog.intensity = zone.intensity;
        fog.density = zone.density;
        fog.heightFalloff = zone.heightFalloff;
        fog.maxOpacity = zone.maxOpacity;
        fog.startDistance = zone.startDistance;
        fog.startHeight = zone.startHeight;
    }
    if (!fog.enabled) {
        viewer.SetFogParam(nullptr);
        return;
    }
    auto pow4 = [](float v) { return v * v * v * v; };
    ViewerFogParam p{};
    for (int c = 0; c < 3; c++) p.fogInscatteringColor[c] = fog.color[c] * fog.intensity;
    p.fogDensity = pow4(fog.density);
    p.fogHeightFalloff = fog.heightFalloff;
    p.fogMaxOpacity = pow4(fog.maxOpacity);
    p.fogStartDistance = fog.startDistance;
    p.fogHeightStartDistance = fog.startHeight;
    if (fog.blendEnabled) {
        for (int c = 0; c < 3; c++) p.fogBlendInscatteringColor[c] = fog.blendColor[c] * fog.blendIntensity;
        p.fogBlendEndDistance = fog.blendEndDistance - fog.startDistance - fog.blendRange;
        p.fogBlendInvRange = fog.blendRange != 0 ? 1.0f / fog.blendRange : 0.0f;
    } else {
        std::copy_n(p.fogInscatteringColor, 3, p.fogBlendInscatteringColor);
        p.fogBlendEndDistance = std::numeric_limits<float>::min();
        p.fogBlendInvRange = 1.0f;
    }
    p.fogSkyMaxOpacity = pow4(fog.skyMaxOpacity);
    p.fogSkyHeightFalloff = fog.skyHeightFalloff;
    p.fogSkyHeightStartDistance = fog.skyStartHeight;
    std::copy_n(fog.fmtColor, 3, p.fmtInscatteringColor);
    p.fmtDensity = fog.fmtDensity;
    p.fmtHeightFalloff = fog.fmtHeightFalloff;
    p.fmtStartDistance = fog.fmtStartDistance;
    p.fmtHeightStartDistance = fog.fmtStartHeight;
    p.maskDistanceFalloff = fog.maskDistanceFalloff;
    p.fmtMaxDistance = fog.fmtMaxDistance;
    p.fsSunMaskDetectSize = fog.fsSunMaskDetectSize;
    p.fsBlendRate = fog.fsBlendRate;
    p.fsRayleigh = fog.fsRayleighCoefficient;
    std::copy_n(fog.fsRayleighColor, 3, p.fsBetaR);
    p.fsMie = fog.fsMieCoefficient;
    std::copy_n(fog.fsMieColor, 3, p.fsBetaM);
    p.fsAsymmetryFactor = fog.fsAsymmetryFactor;
    viewer.SetFogParam(&p);
}

// app.VolumetricFogControllApp sets the zone's _AnimationParam on the MainCamera's global VolumetricFog and on
// VolumetricFogControl, and _ControlParam's leak bias; updateVolumetricFogList walks the enabled media.
void ApplyVolumetricFog(Viewer& viewer, const SceneVolumetricFogControl& component,
                        const std::optional<SceneVolumetricFog>& applicationFog, const std::vector<SceneVolumetricFog>& sceneFogs,
                        const SceneVolumetricFogZone& zone) {
    SceneVolumetricFogControl control = component;
    std::optional<SceneVolumetricFog> global = applicationFog;
    if (zone.found) {
        control.enabled = zone.enabled;
        control.cullingDistance = zone.cullingDistance;
        control.depthDecodingParam = zone.depthDecodingParam;
        control.rejection = zone.rejection;
        control.rejectSensitivity = zone.rejectSensitivity;
        if (global) {
            global->enabled = zone.enabled;
            global->color = zone.color;
            global->density = zone.density;
            global->eccentricity = zone.scatteringDistribution;
            global->attenuationByHeight = zone.attenuationByHeight;
        }
    }
    if (zone.controlFound) control.leakBias = zone.leakBias;
    ViewerVolumetricFogControl viewerControl;
    viewerControl.enabled = control.enabled;
    viewerControl.depthSlices = control.textureSize == 1 ? 128 : 64;
    viewerControl.cullingDistance = control.cullingDistance;
    viewerControl.depthDecodingParam = control.depthDecodingParam;
    viewerControl.softness = control.softness * 0.05f;
    viewerControl.blendFactor = std::min(control.prevFrameBlendFactor, 0.99f);
    viewerControl.rejection = control.rejection;
    viewerControl.rejectSensitivity = control.rejectSensitivity;
    viewerControl.rejectSensitivityFactor = control.rejectSensitivityFactor;
    viewerControl.leakBias = control.leakBias;
    viewerControl.jitterNoise = control.jitterNoise;
    std::vector<ViewerVolumetricFog> media;
    auto add = [&](const SceneVolumetricFog& fog) {
        if (!fog.enabled) return;
        ViewerVolumetricFog medium;
        medium.type = fog.type;
        medium.albedo = fog.color;
        medium.density = fog.density;
        medium.eccentricity = fog.eccentricity;
        medium.attenuationByHeight = fog.attenuationByHeight;
        medium.referenceAltitude = fog.referenceAltitude[1];
        // updateShape keeps the serialized OBB until the transform leaves the identity it caches first; then the
        // OBB is the unit box the transform scales and places.
        bool identity = true;
        for (int i = 0; i < 16; i++) identity = identity && fog.world[i] == ((i % 5 == 0) ? 1.0f : 0.0f);
        if (identity) {
            for (int r = 0; r < 3; r++) {
                for (int k = 0; k < 4; k++) medium.world[r * 4 + k] = fog.obb[r * 4 + k] * fog.obb[16 + r];
            }
            std::copy_n(fog.obb + 12, 4, medium.world + 12);
        } else {
            std::copy_n(fog.world, 16, medium.world);
        }
        media.push_back(medium);
    };
    if (global) add(*global);
    for (const SceneVolumetricFog& fog : sceneFogs) add(fog);
    viewer.SetVolumetricFog(viewerControl, std::move(media));
}

// TonemapParam (ToneMappingImplement::preUpdate, SDR: maxNit 1), CameraKerare, ColorCorrectTexture, cbCAS
// (executeCASFilter: FidelityFX CasSetup at the TemporalAA mode's sharpness, no scaling) and OutputColorAdjustment
// (DisplaySettings::update) at the standard gamma level (GameOptionManager::applyDefaultGammaLevel: 1) with the full
// output range. RAE_DISPLAY_SETTINGS="gamma lower upper" replaces the last three, as a player's options do.
// ToneMappingImplement::getSharpness: TemporalAA Mild (3) and Strong (4) fix it; without TemporalAA it is 0.
float TemporalSharpness(const SceneToneMapping& toneMapping) {
    switch (toneMapping.temporalAA) {
    case 0: return 0.0f;
    case 3: return 0.333f;
    case 4: return 0.5f;
    default: return toneMapping.sharpness;
    }
}

// ToneMappingImplement::preUpdate: AABlend by TemporalAA mode, capped by the frames accumulated since the last cut.
void ApplyTemporalAA(Viewer& viewer, const SceneToneMapping& toneMapping) {
    ViewerTemporalAA p;
    p.enabled = toneMapping.found && toneMapping.temporalAA != 0;
    p.blend = toneMapping.temporalAA == 2   ? 0.8333333f
              : toneMapping.temporalAA == 3 ? 0.9166667f
              : toneMapping.temporalAA == 4 ? 0.96875f
                                            : 0.0f;
    p.sharpness = toneMapping.temporalAA == 5 ? 0.0f : TemporalSharpness(toneMapping);
    p.tonemapRange = toneMapping.tonemapRange;
    p.preTonemapRange = toneMapping.preTonemapRange;
    p.subPixel = toneMapping.subPixel;
    p.responsiveRate = toneMapping.responsiveAARate;
    p.jitterScale = toneMapping.jitterScale;
    viewer.SetTemporalAA(p);
}

// SoftBloomImplement::render's constant buffers from the component, with the zone's app.SoftBloomController values.
void ApplySoftBloom(Viewer& viewer, const SceneSoftBloom& component, const SceneSoftBloomZone& zone) {
    ViewerSoftBloomParams p{};
    bool enabled = component.found && component.enabled && (!zone.found || zone.enabled);
    bool supported = component.algorithm == 2 && !component.lwMode && !component.highPrecision;
    uint32_t levels = zone.found ? zone.reductionLevel : component.reductionLevel;
    float threshold = zone.found ? zone.threshold : component.threshold;
    float dispersion = zone.found ? zone.dispersion : component.dispersion;
    float outputRatio = zone.found ? zone.outputRatio : component.outputRatio;
    uint32_t color = zone.found ? zone.blurColor : component.blurColor;
    p.levels = enabled && supported ? levels : 0;
    auto channel = [](uint32_t rgba, int c) { return static_cast<float>((rgba >> (c * 8)) & 0xFF) / 255.0f; };
    for (float* cb : { p.reduction, p.output }) {
        for (int c = 0; c < 3; c++) {
            cb[c] = channel(color, c);
            cb[4 + c] = channel(component.dirtMaskTintColor, c);
        }
        cb[3] = threshold;
        cb[8] = component.dirtMaskIntensity;
        cb[9] = 1.0f / (component.dirtMaskThreshold + 1.0f);
    }
    p.reduction[7] = 1.0f;
    p.output[7] = outputRatio;
    p.cone[0] = p.cone[1] = dispersion + 0.5f;
    for (std::size_t i = 0; i < 7; i++) p.scale[i] = i < component.sizeScale.size() ? component.sizeScale[i] * component.sizeRate : 1.0f;
    p.scale[7] = levels > 0 ? 1.0f / static_cast<float>(levels) : 0.0f;
    viewer.SetSoftBloom(p);
}

std::vector<std::string> ColorCubePalette(const SceneColorCorrect& colorCorrect) {
    std::vector<std::string> palette = { colorCorrect.cube0, colorCorrect.cube1, colorCorrect.cube2 };
    for (const std::string& element : colorCorrect.elements) {
        if (!element.empty() && std::find(palette.begin(), palette.end(), element) == palette.end()) palette.push_back(element);
    }
    return palette;
}

std::string ColorCubeTarget(const SceneColorCorrect& colorCorrect, const SceneColorCorrectZone& zone) {
    if (!zone.found || zone.blendTargetIndex >= colorCorrect.elements.size()) return {};
    return colorCorrect.elements[zone.blendTargetIndex];
}

void ApplyPostProcess(Viewer& viewer, const SceneToneMapping& toneMapping, const SceneColorCorrect& colorCorrect,
                      const SceneToneMap& zone, const std::string& cubeFrom, const std::string& cubeTo, float rate) {
    ViewerPostProcessParams p{};
    const float maxNit = 1.0f;
    float contrast = zone.found ? zone.contrast : 1.0f;
    float begin = zone.found ? zone.linearBegin : 0.22f;
    float length = zone.found ? zone.linearLength : 0.4f;
    float toe = zone.found ? zone.toe : 1.0f;
    float x = (maxNit - begin) * length / contrast;
    float linearStart = begin + x;
    float shoulder = maxNit - (contrast * x + begin);
    float contrastFactor = contrast * maxNit / shoulder * (-1.0f / maxNit) * 1.442695f;
    const float tonemap[11] = { contrast, begin, length, toe, maxNit, linearStart, shoulder, contrastFactor,
                                -(contrastFactor * linearStart), 1.0f / begin, begin - begin * contrast };
    std::copy_n(tonemap, 11, p.tonemapParam);
    if (toneMapping.vignetting == 1 && toneMapping.kerareEnd != toneMapping.kerareBegin) {
        p.cameraKerare[0] = 1.0f / (toneMapping.kerareEnd - toneMapping.kerareBegin);
        p.cameraKerare[1] = -toneMapping.kerareBegin / (toneMapping.kerareEnd - toneMapping.kerareBegin);
    }
    p.cameraKerare[2] = zone.found ? zone.vignettingBrightness : toneMapping.vignettingBrightness;
    const float cubeSize = 32.0f;
    p.colorCorrect[0] = cubeSize;
    p.colorCorrect[1] = rate;
    p.colorCorrect[2] = colorCorrect.cube2BlendRate;
    p.colorCorrect[3] = 1.0f / cubeSize;
    for (int i = 0; i < 4; i++) p.colorCorrect[4 + i * 5] = 1.0f;
    float gamma = 1.0f;
    float lower = 0.0f;
    float upper = 1.0f;
    if (const char* display = std::getenv("RAE_DISPLAY_SETTINGS")) std::sscanf(display, "%f %f %f", &gamma, &lower, &upper);
    const float outputAdjustment[13] = { gamma, lower, upper, upper - lower, 0, 0, 0, 0, 0, 0, 0, 1, 1 };
    std::copy_n(outputAdjustment, 13, p.outputColorAdjustment);
    float sharpness = std::min(1.0f, TemporalSharpness(toneMapping) * 2.0f);
    float sharp = -1.0f / (8.0f + (5.0f - 8.0f) * sharpness);
    auto bits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };
    p.cas[0] = bits(1.0f);
    p.cas[1] = bits(1.0f);
    p.cas[4] = bits(sharp);
    p.cas[5] = FloatToHalf(sharp);
    p.cas[6] = bits(8.0f);
    std::vector<std::string> palette = ColorCubePalette(colorCorrect);
    auto slot = [&](const std::string& path) {
        auto found = std::find(palette.begin(), palette.end(), path);
        return found == palette.end() ? 0u : static_cast<uint32_t>(found - palette.begin());
    };
    p.colorCubes[0] = slot(cubeFrom);
    p.colorCubes[1] = slot(cubeTo);
    p.colorCubes[2] = slot(colorCorrect.cube2);
    viewer.SetPostProcess(p);
}

void UploadScene(Viewer& viewer, const LoadedGame& game, const SceneBuild& build, const TextureCache& textures,
                 const SceneLook& look) {
    // The CPU side of these runs while the pipelines are created.
    std::future<std::optional<PreparedIbl>> preparedIbl;
    if (!build.ibls.empty()) {
        preparedIbl = std::async(std::launch::async, [&] { return PrepareSceneIbl(game, build.ibls.front()); });
    }
    std::future<std::optional<PreparedProbes>> preparedProbes;
    if (!build.lightProbes.empty()) {
        preparedProbes = std::async(std::launch::async, [&] { return PrepareLightProbes(game, build.lightProbes); });
    }
    std::future<PreparedCubemaps> preparedCubemaps;
    if (!build.localCubemaps.empty() && !build.lightProbes.empty()) {
        preparedCubemaps = std::async(std::launch::async, [&] { return PrepareLocalCubemaps(game, build.localCubemaps); });
    }

    viewer.LoadMesh(MakeViewerMesh(build));
    viewer.CreateGamePrepass(MakePrepassDesc(build));
    viewer.CreateGameDeferred(build.masters.deferredDescs);

    SdfData lighting = game.Readers().Get<SdfReader>()->Read(
        game.ExtractFile("natives/stm/systems/shader/lighting.sdf" + game.Game().GetMasterExt()));
    // What the engine draws for pixels without subsurface scattering: lights, GIDSRV and GISSRV lit
    // with the base color in one pass, blended over the GBuffer's emissive output.
    const SdfProgram* lightProgram = lighting.FindProgram("DeferredLightSingleIndirectCombine");
    if (lightProgram == nullptr) throw std::runtime_error("DeferredLightSingleIndirectCombine not found");
    viewer.CreateGameLighting({ lightProgram->vs, lightProgram->ps, 12, 1, LightingSlots(*lightProgram), lightProgram->blendState });
    if (const SdfProgram* indirect = lighting.FindProgram("IndirectIllumination"); indirect && !indirect->cs.empty()) {
        viewer.CreateIndirectIllumination(ComputeDesc(*indirect));
    }
    if (std::string primitivePath = "natives/stm/systems/shader/primitive.sdf" + game.Game().GetMasterExt(); game.FindEntry(primitivePath)) {
        SdfData primitive = game.Readers().Get<SdfReader>()->Read(game.ExtractFile(primitivePath));
        if (const SdfProgram* precalc = primitive.FindProgram("PreCalculateLightingNoBackDiffuse"); precalc && !precalc->cs.empty()) {
            viewer.CreateParticleLighting(ComputeDesc(*precalc));
        }
    }

    bool sceneLighting = !build.directionalLights.empty() || !build.ibls.empty() || build.toneMap.found;
    if (!build.directionalLights.empty()) {
        const SceneDirectionalLight& light = build.directionalLights.front();
        float color[3];
        for (int c = 0; c < 3; c++) color[c] = light.common.color[c] * light.common.intensity;
        float scattering[3];
        float scatteringIntensity = light.common.usingSameIntensity ? light.common.intensity : light.common.volumetricScatteringIntensity;
        for (int c = 0; c < 3; c++) scattering[c] = light.common.color[c] * scatteringIntensity;
        viewer.SetDirectionalLight(light.direction, color, light.common.minRoughness * light.common.minRoughness, scattering);
        ViewerDirectionalShadow shadow;
        shadow.enabled = light.shadowEnable;
        shadow.distance = light.shadowDistance;
        shadow.minimumAreaSize = light.shadowMinimumAreaSize;
        shadow.minimumFov = light.minimumFov;
        std::copy_n(light.partition, 4, shadow.partition);
        shadow.depthBias = light.shadowDepthBias;
        shadow.slopeBias = light.shadowSlopeBias;
        shadow.bias = light.shadowBias;
        shadow.variance = light.shadowVariance;
        shadow.aoEfficiency = 1.0f - light.common.aoEfficiency;
        std::copy_n(light.shadowBoundary, 20, shadow.boundary);
        shadow.cameraNear = build.mainCamera.found ? build.mainCamera.nearPlane : 0.0f;
        shadow.cameraFar = build.mainCamera.found ? build.mainCamera.farPlane : 0.0f;
        viewer.SetDirectionalShadow(shadow);
    } else if (sceneLighting) {
        viewer.ClearDirectionalLight();
    } else {
        float dlDirection[3] = { 0.25f, 0.45f, -0.86f };
        float dlColor[3] = { look.directionalIntensity, look.directionalIntensity, look.directionalIntensity };
        viewer.SetDirectionalLight(dlDirection, dlColor);
    }
    if (build.toneMap.found) {
        const SceneToneMap& map = build.toneMap;
        ApplyToneMap(viewer, map, true);
        LogInfo("tone map: EV %g contrast %g linear %g+%g toe %g, auto exposure %s (white %g..%g at %g)", map.ev, map.contrast,
                map.linearBegin, map.linearLength, map.toe, map.autoExposure ? "on" : "off", map.minWhitePoint,
                map.maxWhitePoint, map.whiteRange);
    }
    try {
        UploadPostProcess(viewer, game, build);
    } catch (const std::exception& e) {
        LogWarning("post process not loaded: %s", e.what());
    }
    try {
        UploadFog(viewer, game, build);
    } catch (const std::exception& e) {
        LogWarning("fog not loaded: %s", e.what());
    }
    try {
        std::string brdfPath = "natives/stm/systems/rendering/newambientbrdf.tex" + game.Game().GetTexExt();
        if (game.FindEntry(brdfPath)) {
            TextureData brdf = game.Readers().Get<TexReader>()->Read(game.ExtractFile(brdfPath));
            viewer.LoadAmbientBrdf(ToGameTexture(brdf));
        }
    } catch (const std::exception& e) {
        LogWarning("ambient BRDF not loaded: %s", e.what());
    }
    bool sceneSky = false;
    if (!build.ibls.empty()) {
        try {
            sceneSky = UploadSceneIbl(viewer, build.ibls.front(), preparedIbl.get());
            if (!sceneSky) LogWarning("scene IBL not loaded: %s", build.ibls.front().texture.c_str());
        } catch (const std::exception& e) {
            LogWarning("scene IBL not loaded: %s", e.what());
        }
    }
    if (!build.lightProbes.empty()) {
        try {
            if (!UploadLightProbes(viewer, build.lightProbes, preparedProbes.get())) {
                LogWarning("light probes not loaded: %s", build.lightProbes.front().network.c_str());
            }
        } catch (const std::exception& e) {
            LogWarning("light probes not loaded: %s", e.what());
        }
    }
    viewer.LoadInstances(build.worlds, game.Game().GetInstanceStride());
    std::vector<GameLightParam> lights = build.lights;
    lights.insert(lights.end(), build.cameraLights.begin(), build.cameraLights.end());
    viewer.LoadLights(lights);
    viewer.SetCameraLights(static_cast<uint32_t>(build.lights.size()), build.cameraLights);
    try {
        UploadVolumetricFog(viewer, game, build);
    } catch (const std::exception& e) {
        LogWarning("volumetric fog not loaded: %s", e.what());
    }
    try {
        UploadCacao(viewer, game, build.ssao);
    } catch (const std::exception& e) {
        LogWarning("CACAO not loaded: %s", e.what());
    }
    if (!build.localCubemaps.empty() && !build.lightProbes.empty()) {
        try {
            UploadLocalCubemaps(viewer, build.localCubemaps, preparedCubemaps.get());
        } catch (const std::exception& e) {
            LogWarning("local cubemaps not loaded: %s", e.what());
        }
    }
    viewer.LoadGameMaterials(build.materials, textures.descs, build.instanceMaterials);

    if (!look.skyPath.empty() && !sceneSky) {
        try {
            TextureData skyTex = game.Readers().Get<TexReader>()->Read(game.ExtractFile(look.skyPath));
            viewer.LoadSky(ToGameTexture(skyTex));
            viewer.SetSkyIntensity(look.skyIntensity);
            LogInfo("sky: %s (%ux%u)", look.skyPath.c_str(), skyTex.width, skyTex.height);
        } catch (const std::exception& e) {
            LogWarning("sky not loaded: %s", e.what());
        }
    }
    // Skinned draws read SkinningMatrices even when nothing animates them.
    if (build.skinMatrixCount > 0) {
        std::vector<float> matrices(static_cast<std::size_t>(build.skinMatrixCount) * 12);
        for (const SkinnedInstance& instance : build.skinned) {
            SkeletalAnimator(instance.skeleton).Evaluate(0, instance.world, matrices.data() + instance.jointOffset * 12);
        }
        viewer.SetSkinningMatrices(matrices);
    }
    viewer.ApplySceneMatrix();
    viewer.SetUnlit(look.unlit);
    LogInfo("viewer ready: draws=%zu instances=%zu materials=%zu textures=%zu pipelines=%zu",
            build.draws.size(), build.worlds.size(), build.materials.size(), textures.descs.size(),
            build.masters.deferredDescs.size());
}

SceneFraming FrameScene(Viewer& viewer, const std::vector<ViewerInstanceWorld>& worlds, bool bulk) {
    SceneFraming framing{};
    float boundsRadius;
    viewer.GetMeshBounds(framing.center, boundsRadius);
    framing.radius = boundsRadius;
    LogInfo("bounds: center=(%.1f, %.1f, %.1f) radius=%.1f", framing.center[0], framing.center[1],
            framing.center[2], boundsRadius);

    // Scene AABBs are dominated by outliers (terrain tiles kilometres out); framing
    // them shrinks the scene to a few pixels and the radius-scaled near plane clips rooms.
    if (bulk && worlds.size() > 1) {
        std::vector<float> axis(worlds.size());
        float median[3];
        for (int c = 0; c < 3; c++) {
            for (std::size_t i = 0; i < worlds.size(); i++) axis[i] = worlds[i].m[c * 4 + 3];
            std::nth_element(axis.begin(), axis.begin() + axis.size() / 2, axis.end());
            median[c] = axis[axis.size() / 2];
        }
        std::vector<float> dist(worlds.size());
        for (std::size_t i = 0; i < worlds.size(); i++) {
            float d[3] = { worlds[i].m[3] - median[0], worlds[i].m[7] - median[1], worlds[i].m[11] - median[2] };
            dist[i] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }
        std::size_t p90 = dist.size() * 9 / 10;
        std::nth_element(dist.begin(), dist.begin() + p90, dist.end());
        framing.radius = dist[p90] < 5.0f ? 5.0f : dist[p90];
        std::copy(median, median + 3, framing.center);
        viewer.SetClipPlanes(framing.radius * 0.002f < 0.05f ? 0.05f : framing.radius * 0.002f,
                             boundsRadius * 4.0f);
        LogInfo("frame: center=(%.1f, %.1f, %.1f) radius=%.1f", median[0], median[1], median[2], framing.radius);
    }
    return framing;
}
