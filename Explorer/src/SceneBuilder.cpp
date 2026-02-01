#include "Explorer/SceneBuilder.h"

#include <algorithm>
#include "Core/Assets/Readers/GuiReader.h"
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <thread>

#include "Core/Assets/Readers/EfxReader.h"
#include "Core/Assets/Readers/MdfReader.h"
#include "Core/Assets/Readers/MeshLodSettingsReader.h"
#include "Core/Assets/Readers/MeshReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Core/Assets/Readers/PrefabReader.h"
#include "Core/Assets/Readers/SceneReader.h"
#include "Core/Assets/Readers/SdfReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/Readers/UvsReader.h"
#include "Core/Hashing/MurmurHash3.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Explorer/AsyncLoads.h"
#include "Explorer/Log.h"
#include "Explorer/SceneVisibility.h"

static constexpr const char* MESH_LOD_SETTINGS = "natives/stm/systems/rendering/meshlodsettings.lod.3";
// chapter > light > area time > state > room lights > shared room lights
static constexpr int MAX_SCENE_DEPTH = 6;

std::string FindPath(const LoadedGame& game, std::string_view contains, std::string_view suffix) {
    std::string tail = std::string(contains) + std::string(suffix);
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            if (entry.filePath.ends_with(tail)) return entry.filePath;
        }
    }
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            if (entry.filePath.ends_with(suffix) && entry.filePath.find(contains) != std::string::npos) {
                return entry.filePath;
            }
        }
    }
    throw std::runtime_error("no entry containing '" + std::string(contains) + "'");
}

std::string ComponentFieldKey(const std::string& objectKey, int32_t componentIndex, std::string_view field) {
    return objectKey + "|c" + std::to_string(componentIndex) + '|' + std::string(field);
}

std::string MaterialTextureKey(const std::string& mdfPakPath, const std::string& material, const std::string& type) {
    return mdfPakPath + '|' + material + '|' + type;
}

std::string MeshMaterialKey(const std::string& meshPakPath) {
    return meshPakPath + "|mdf";
}

std::string MaterialParamKey(const std::string& mdfPakPath, const std::string& material, const std::string& param) {
    return "param:" + mdfPakPath + '|' + material + '|' + param;
}

static constexpr std::string_view TRANSFORM_PARAM_PREFIX = "param:xform:";

std::string TransformParamKey(const std::string& objectKey, std::string_view field) {
    return std::string(TRANSFORM_PARAM_PREFIX) + objectKey + '#' + std::string(field);
}

bool ParseTransformParamKey(std::string_view key, std::string& objectKey, std::string& field) {
    if (!key.starts_with(TRANSFORM_PARAM_PREFIX)) return false;
    key.remove_prefix(TRANSFORM_PARAM_PREFIX.size());
    std::size_t hash = key.rfind('#');
    if (hash == std::string_view::npos) return false;
    objectKey = std::string(key.substr(0, hash));
    field = std::string(key.substr(hash + 1));
    return true;
}


std::string SourceAssetPath(const std::string& pakPath) {
    std::string path = pakPath;
    for (std::string_view prefix : { "natives/stm/", "streaming/" }) {
        if (path.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), path.begin(),
                       [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); })) {
            path.erase(0, prefix.size());
        }
    }
    for (std::size_t dot = path.find_last_of('.'); dot != std::string::npos; dot = path.find_last_of('.')) {
        std::string_view tail = std::string_view(path).substr(dot + 1);
        bool numeric = !tail.empty() && tail.find_first_not_of("0123456789") == std::string_view::npos;
        if (!numeric && tail != "x64" && tail != "X64" && tail != "stm" && tail != "STM") break;
        path.resize(dot);
    }
    return path;
}

static const std::string* FindOverride(const AssetOverrides* overrides, const std::string& key) {
    if (overrides == nullptr) return nullptr;
    auto it = overrides->find(key);
    return it == overrides->end() ? nullptr : &it->second;
}

static const std::string* FindOverride(const SceneBuild& build, const std::string& key) {
    return FindOverride(build.overrides, key);
}

static bool TransformOverride(const AssetOverrides* overrides, const std::string& objectKey, std::string_view field,
                              float out[3]) {
    const std::string* text = FindOverride(overrides, TransformParamKey(objectKey, field));
    return text && std::sscanf(text->c_str(), "%f,%f,%f", &out[0], &out[1], &out[2]) == 3;
}

// A scene tree's files, read on worker threads ahead of the build that consumes them.
struct ScenePrefetch {
    AsyncLoads<SceneData> scenes;
    AsyncLoads<MeshData> meshes;
    AsyncLoads<MaterialData> materials;
    AsyncLoads<SdfData> masters;
    AsyncLoads<TextureData> textures;
    AsyncLoads<TerrainData> collisionMeshes;
    // Last, so its workers stop before the loads they fill go away.
    LoadPool pool;
};

std::string ToPakPath(std::string path, const char* version) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return "natives/stm/" + path + version;
}

static std::span<const uint8_t> MeshStreamRange(const MeshData& mesh, VertexStreamSlot slot) {
    for (std::size_t i = 0; i < mesh.streams.size(); i++) {
        if (mesh.streams[i].slot != slot) continue;
        std::size_t end = i + 1 < mesh.streams.size() ? mesh.streams[i + 1].offset : mesh.vertexBuffer.size();
        return std::span(mesh.vertexBuffer).subspan(mesh.streams[i].offset, end - mesh.streams[i].offset);
    }
    return {};
}

static uint32_t MaterialSlotByName(const MaterialData& mdf, const std::string& name) {
    for (std::size_t i = 0; i < mdf.materials.size(); i++) {
        if (mdf.materials[i].name == name) return static_cast<uint32_t>(i);
    }
    return 0;
}

static std::string ClusterMaterialName(const MeshData& mesh, const MeshCluster& cluster) {
    return cluster.materialId < mesh.materialNames.size() ? mesh.materialNames[cluster.materialId] : std::string();
}

static const char* SemanticName(uint8_t semantic) {
    switch (semantic) {
        case 0: return "POSITION";
        case 1: return "NORMAL";
        case 2: return "BINORMAL";
        case 3: return "TANGENT";
        case 4: return "TEXCOORD";
        case 5: return "INDEX";
        case 6: return "WEIGHT";
        case 7: return "COLOR";
        default: throw std::runtime_error("unknown semantic " + std::to_string(semantic));
    }
}

static DXGI_FORMAT MapVertexFormat(uint8_t format) {
    switch (format) {
        case 2: return DXGI_FORMAT_R32G32B32_FLOAT;
        case 4: return DXGI_FORMAT_R16G16_FLOAT;
        case 6: return DXGI_FORMAT_R8G8B8A8_UINT;
        case 8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case 9: return DXGI_FORMAT_R8G8B8A8_SNORM;
        default: throw std::runtime_error("unknown vertex format " + std::to_string(format));
    }
}

std::vector<GameInputElement> BuildInputLayout(const SdfProgram& program) {
    std::vector<GameInputElement> layout;
    for (const SdfInputElement& e : program.inputLayout) {
        layout.push_back({ SemanticName(e.semantic), e.semanticIndex, MapVertexFormat(e.format),
                           e.inputSlot, e.offset });
    }
    return layout;
}

GameTextureDesc ToGameTexture(const TextureData& tex) {
    GameTextureDesc desc{ tex.width, tex.height, tex.format, tex.NumImages(), {} };
    for (const TextureMip& mip : tex.mips) {
        desc.mips.push_back({ std::span<const uint8_t>(mip.data), mip.pitch });
    }
    desc.depth = std::max<uint32_t>(tex.depthAndType, 1);
    return desc;
}

static TextureData MakeFallbackTexture() {
    TextureData tex{};
    tex.width = 4;
    tex.height = 4;
    tex.format = 28;
    TextureMip mip;
    mip.pitch = 16;
    mip.size = 64;
    mip.data.assign(64, 0x00);
    tex.mips.push_back(std::move(mip));
    return tex;
}

// Bit 0 of the header dword at 0x1C (TextureData.streamingFlags) marks a streaming/
// counterpart; the pak index answers the same without reading the base file.
static std::string TextureFilePath(const LoadedGame& game, const std::string& path) {
    std::string texExt = game.Game().GetTexExt();
    std::string streamingPath = ToPakPath("streaming/" + path, texExt.c_str());
    return game.FindEntry(streamingPath) ? streamingPath : ToPakPath(path, texExt.c_str());
}

uint32_t LoadTexture(const LoadedGame& game, TextureCache& cache, const std::string& path) {
    auto it = cache.byPath.find(path);
    if (it != cache.byPath.end()) return it->second;

    std::string usePath = TextureFilePath(game, path);
    if (cache.deferred) {
        uint32_t index = static_cast<uint32_t>(cache.descs.size());
        cache.storage.emplace_back();
        cache.descs.emplace_back();
        cache.byPath.emplace(path, index);
        cache.pending.emplace_back(index, std::move(usePath));
        return index;
    }
    try {
        cache.storage.push_back(game.Readers().Get<TexReader>()->Read(game.ExtractFile(usePath)));
        uint32_t index = static_cast<uint32_t>(cache.descs.size());
        cache.descs.push_back(ToGameTexture(cache.storage.back()));
        cache.byPath.emplace(path, index);
        return index;
    } catch (const std::exception&) {
    }

    LogWarning("fallback texture: %s", path.c_str());
    cache.byPath.emplace(path, 0);
    return 0;
}

void ResolveTextures(const LoadedGame& game, TextureCache& cache, AsyncLoads<TextureData>* prefetched) {
    std::vector<uint8_t> failed(cache.pending.size(), 0);
    std::atomic<std::size_t> next{ 0 };
    const TexReader* reader = game.Readers().Get<TexReader>();
    auto work = [&] {
        for (std::size_t i = next++; i < cache.pending.size(); i = next++) {
            const auto& [index, path] = cache.pending[i];
            try {
                std::shared_ptr<TextureData> loaded = prefetched ? prefetched->Take(path) : nullptr;
                cache.storage[index] = loaded ? std::move(*loaded) : reader->Read(game.ExtractFile(path));
            } catch (const std::exception&) {
                failed[i] = 1;
            }
        }
    };
    std::vector<std::thread> workers(std::max(1u, std::thread::hardware_concurrency()));
    for (std::thread& worker : workers) worker = std::thread(work);
    for (std::thread& worker : workers) worker.join();
    for (std::size_t i = 0; i < cache.pending.size(); i++) {
        uint32_t index = cache.pending[i].first;
        if (failed[i]) {
            LogWarning("fallback texture: %s", cache.pending[i].second.c_str());
            cache.descs[index] = cache.descs[0];
        } else {
            cache.descs[index] = ToGameTexture(cache.storage[index]);
        }
    }
    cache.pending.clear();
}

std::string StreamingCopyPath(const LoadedGame& game, const std::string& pakPath) {
    const std::string prefix = "natives/stm/";
    if (!pakPath.starts_with(prefix) || pakPath.starts_with(prefix + "streaming/")) return pakPath;
    std::string streaming = prefix + "streaming/" + pakPath.substr(prefix.size());
    return game.FindEntry(streaming) ? streaming : pakPath;
}

uint32_t SoftDotTexture(TextureCache& cache) {
    auto it = cache.byPath.find("<softdot>");
    if (it != cache.byPath.end()) return it->second;

    constexpr uint32_t SIZE = 64;
    TextureData tex{};
    tex.width = SIZE;
    tex.height = SIZE;
    tex.format = 28;
    TextureMip mip;
    mip.pitch = SIZE * 4;
    mip.size = SIZE * SIZE * 4;
    mip.data.resize(mip.size);
    for (uint32_t y = 0; y < SIZE; y++) {
        for (uint32_t x = 0; x < SIZE; x++) {
            float dx = (x + 0.5f) / SIZE * 2 - 1;
            float dy = (y + 0.5f) / SIZE * 2 - 1;
            float t = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
            uint8_t* px = mip.data.data() + (y * SIZE + x) * 4;
            px[0] = px[1] = px[2] = 255;
            px[3] = static_cast<uint8_t>(t * t * (3 - 2 * t) * 255);
        }
    }
    tex.mips.push_back(std::move(mip));
    cache.storage.push_back(std::move(tex));
    uint32_t index = static_cast<uint32_t>(cache.descs.size());
    cache.descs.push_back(ToGameTexture(cache.storage.back()));
    cache.byPath.emplace("<softdot>", index);
    return index;
}

// A stage's SDF slots: slotRaw byte 0 is the VS register, byte 4 the PS one; byte 6 the stage mask (0x01 VS, 0x10 PS).
static std::vector<GameComputeSlot> StageSlots(const SdfProgram& program, bool pixel) {
    std::vector<GameComputeSlot> slots;
    auto add = [&](uint64_t slotRaw, const std::string& name, GameComputeSlot::Kind kind) {
        if (((slotRaw >> 48) & (pixel ? 0x10 : 0x01)) == 0) return;
        GameComputeSlot slot{ kind, static_cast<uint8_t>((slotRaw >> (pixel ? 32 : 0)) & 0xFF), GameComputeResource::Other };
        slot.name = name;
        if (kind == GameComputeSlot::Kind::Sampler) SamplerFromName(name, slot.filter, slot.address);
        slots.push_back(slot);
    };
    for (const SdfConstantBuffer& c : program.constantBuffers) add(c.slotRaw, c.name, GameComputeSlot::Kind::Cbv);
    for (const SdfResource& r : program.srvs) add(r.slotRaw, r.name, GameComputeSlot::Kind::Srv);
    for (const SdfResource& r : program.uavs) add(r.slotRaw, r.name, GameComputeSlot::Kind::Uav);
    for (const SdfResource& r : program.samplers) add(r.slotRaw, r.name, GameComputeSlot::Kind::Sampler);
    return slots;
}

// TypeCustomMaterialBase: the master material's Polygon or Ribbon program (their *Lighting variant when
// ShaderSettings lights the particles), the mdf2's UserMaterial block and textures, with the item's parameters
// (matched by name hash) on top.
static std::optional<EffectMaterialAssets> LoadEffectMaterial(const LoadedGame& game, TextureCache& cache, const EfxEmitter& emitter,
                                                              LoadedEffect& fx) {
    const EfxCustomMaterial& custom = *emitter.material;
    std::string programName = emitter.polygon ? "Polygon" : "Ribbon";
    if (emitter.shaderSettings && emitter.shaderSettings->lightingType != 0) programName += "Lighting";
    std::string masterPak = ToPakPath(custom.masterPath, game.Game().GetMasterExt().c_str());
    std::string key = masterPak + '|' + programName;
    EffectMaterialAssets material;
    auto known = std::find(fx.programKeys.begin(), fx.programKeys.end(), key);
    try {
        if (known == fx.programKeys.end()) {
            SdfData sdf = game.Readers().Get<SdfReader>()->Read(game.ExtractFile(masterPak));
            const SdfProgram* program = sdf.FindProgram(programName);
            if ((!program || program->vs.empty() || program->ps.empty()) && programName.ends_with("Lighting")) {
                programName.resize(programName.size() - 8);
                key = masterPak + '|' + programName;
                program = sdf.FindProgram(programName);
            }
            if (!program || program->vs.empty() || program->ps.empty()) {
                LogWarning("effect material %s: no %s program", masterPak.c_str(), programName.c_str());
                return std::nullopt;
            }
            ViewerMaterialProgram out;
            out.vs = program->vs;
            out.ps = program->ps;
            out.vsSlots = StageSlots(*program, false);
            out.psSlots = StageSlots(*program, true);
            out.blend = program->blendState;
            out.depth = program->depthStencilState;
            out.raster = program->rasterizerState;
            fx.programs.push_back(std::move(out));
            fx.programKeys.push_back(key);
            known = fx.programKeys.end() - 1;
        }
        material.program = static_cast<uint32_t>(known - fx.programKeys.begin());
        material.lit = known->ends_with("Lighting");

        MaterialData mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(ToPakPath(custom.mdfPath, game.Game().GetMdfExt().c_str())));
        if (mdf.materials.empty()) return std::nullopt;
        const MaterialEntry& entry = mdf.materials.front();
        material.userMaterial.assign(entry.propertyBlockSize, 0);
        for (const MaterialProperty& prop : entry.properties) {
            std::size_t bytes = prop.values.size() * 4;
            if (prop.dataOffset + bytes <= material.userMaterial.size()) {
                std::memcpy(material.userMaterial.data() + prop.dataOffset, prop.values.data(), bytes);
            }
        }
        for (const MaterialTexture& texture : entry.textures) {
            std::string path = texture.path;
            uint32_t hash = Murmur3::HashAscii(texture.type);
            for (const EfxMaterialParam& param : custom.params) {
                if (param.type == 3 && param.nameHash == hash && param.textureIndex >= 0 &&
                    static_cast<std::size_t>(param.textureIndex) < custom.textures.size()) {
                    path = custom.textures[static_cast<std::size_t>(param.textureIndex)];
                }
            }
            material.textures.emplace_back(texture.type, LoadTexture(game, cache, path));
        }
        for (const MaterialProperty& prop : entry.properties) {
            uint32_t hash = Murmur3::HashAscii(prop.name);
            for (const EfxMaterialParam& param : custom.params) {
                if (param.nameHash != hash) continue;
                if (param.type == 1) {
                    std::size_t count = std::min<std::size_t>(prop.values.size(), 4);
                    if (prop.dataOffset + count * 4 <= material.userMaterial.size()) {
                        std::memcpy(material.userMaterial.data() + prop.dataOffset, param.values, count * 4);
                    }
                } else if (param.type == 2) {
                    material.randomFloats.push_back({ prop.dataOffset, param.values[0], param.values[2], param.values[3] });
                }
            }
        }
    } catch (const std::exception& e) {
        LogWarning("effect material %s: %s", custom.mdfPath.c_str(), e.what());
        return std::nullopt;
    }
    return material;
}

LoadedEffect LoadEffect(const LoadedGame& game, TextureCache& cache, const std::string& pakPath) {
    const EfxReader* efxReader = game.Readers().Get<EfxReader>();
    const UvsReader* uvsReader = game.Readers().Get<UvsReader>();
    if (efxReader == nullptr || uvsReader == nullptr) {
        throw std::runtime_error("effects are not supported for " + game.Game().GetId());
    }

    LoadedEffect fx;
    fx.path = pakPath;
    fx.data = efxReader->Read(game.ExtractFile(fx.path));

    std::map<std::string, std::vector<std::vector<EffectSprite>>> uvsCache;
    for (const EfxEmitter& emitter : fx.data.emitters) {
        EffectEmitterAssets assets;
        assets.fallback.textureIndex = SoftDotTexture(cache);
        if (emitter.uvSequence && !emitter.uvSequence->uvsPath.empty()) {
            const std::string& uvsPath = emitter.uvSequence->uvsPath;
            auto cached = uvsCache.find(uvsPath);
            if (cached == uvsCache.end()) {
                std::vector<std::vector<EffectSprite>> sequences;
                try {
                    UvsData uvs = uvsReader->Read(game.ExtractFile(ToPakPath(uvsPath, game.Game().GetUvsExt().c_str())));
                    std::vector<uint32_t> textureIndices;
                    for (const std::string& texPath : uvs.textures) {
                        textureIndices.push_back(LoadTexture(game, cache, texPath));
                    }
                    for (const UvsSequence& seq : uvs.sequences) {
                        std::vector<EffectSprite> sprites;
                        for (uint32_t p = 0; p < seq.patternCount && seq.firstPattern + p < uvs.patterns.size(); p++) {
                            const UvsPattern& pattern = uvs.patterns[seq.firstPattern + p];
                            EffectSprite sprite;
                            std::copy(pattern.rect, pattern.rect + 4, sprite.uvRect);
                            sprite.textureIndex = pattern.textureIndex < textureIndices.size()
                                ? textureIndices[pattern.textureIndex] : assets.fallback.textureIndex;
                            sprites.push_back(sprite);
                        }
                        sequences.push_back(std::move(sprites));
                    }
                } catch (const std::exception& e) {
                    LogWarning("uvs not loaded: %s: %s", uvsPath.c_str(), e.what());
                }
                cached = uvsCache.emplace(uvsPath, std::move(sequences)).first;
            }
            assets.sequences = cached->second;
        }
        fx.assets.push_back(std::move(assets));
    }
    for (std::size_t i = 0; i < fx.data.emitters.size(); i++) {
        if (IsMaterialEmitter(fx.data.emitters[i])) fx.assets[i].material = LoadEffectMaterial(game, cache, fx.data.emitters[i], fx);
    }
    if (!cache.deferred) ResolveEffectTextureFlags(fx, cache);
    return fx;
}

void ResolveEffectTextureFlags(LoadedEffect& fx, const TextureCache& cache) {
    for (EffectEmitterAssets& assets : fx.assets) {
        for (std::vector<EffectSprite>& sequence : assets.sequences) {
            for (EffectSprite& sprite : sequence) {
                if (sprite.textureIndex >= cache.storage.size()) continue;
                const TextureData& texture = cache.storage[sprite.textureIndex];
                sprite.alphaGamma = texture.AlphaGamma();
                sprite.evPow2 = std::exp2(texture.EffectEv());
            }
        }
    }
}

std::vector<GameMaterialDesc> BuildGameMaterials(const LoadedGame& game, const MaterialData& mdf,
                                                        TextureCache& cache) {
    std::vector<GameMaterialDesc> materials;
    for (const MaterialEntry& mat : mdf.materials) {
        GameMaterialDesc desc;
        desc.paramBlock.resize(mat.propertyBlockSize, 0);
        for (const MaterialProperty& prop : mat.properties) {
            std::size_t bytes = prop.values.size() * 4;
            if (prop.dataOffset + bytes > desc.paramBlock.size()) continue;
            std::memcpy(desc.paramBlock.data() + prop.dataOffset, prop.values.data(), bytes);
        }
        for (const MaterialTexture& texRef : mat.textures) {
            desc.textureIndices.push_back(LoadTexture(game, cache, texRef.path));
        }
        materials.push_back(std::move(desc));
    }
    return materials;
}

void AppendTerrainOverlay(const TerrainData& terr, DebugOverlay& overlay) {
    static const float PALETTE[][3] = {
        { 0.10f, 0.75f, 0.95f }, { 0.95f, 0.45f, 0.10f }, { 0.35f, 0.90f, 0.25f }, { 0.90f, 0.20f, 0.60f },
        { 0.95f, 0.85f, 0.15f }, { 0.55f, 0.35f, 0.95f }, { 0.15f, 0.95f, 0.70f }, { 0.95f, 0.30f, 0.30f },
    };
    auto pack = [](const float c[3], float scale, float alpha) {
        auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return byte(c[0] * scale) | byte(c[1] * scale) << 8 | byte(c[2] * scale) << 16 | byte(alpha) << 24;
    };
    const float light[3] = { 0.30f, 0.85f, 0.43f };
    for (const TerrTriangle& tri : terr.triangles) {
        if (tri.vertices[0] >= terr.vertices.size() || tri.vertices[1] >= terr.vertices.size() ||
            tri.vertices[2] >= terr.vertices.size()) continue;
        const auto& a = terr.vertices[tri.vertices[0]];
        const auto& b = terr.vertices[tri.vertices[1]];
        const auto& c = terr.vertices[tri.vertices[2]];
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        float shade = len > 0 ? 0.45f + 0.55f * std::fabs(n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / len : 1.0f;
        const float* color = PALETTE[static_cast<uint32_t>(tri.layer) % 8];
        uint32_t face = pack(color, shade, 0.45f);
        uint32_t edge = pack(color, 1.2f, 0.95f);
        const std::array<float, 3>* corners[3] = { &a, &b, &c };
        for (int k = 0; k < 3; k++) {
            const auto& p = *corners[k];
            overlay.triangles.push_back({ { p[0], p[1], p[2] }, face });
            const auto& q = *corners[(k + 1) % 3];
            overlay.lines.push_back({ { p[0], p[1], p[2] }, edge });
            overlay.lines.push_back({ { q[0], q[1], q[2] }, edge });
            for (int axis = 0; axis < 3; axis++) {
                overlay.boundsMin[axis] = std::min(overlay.boundsMin[axis], p[axis]);
                overlay.boundsMax[axis] = std::max(overlay.boundsMax[axis], p[axis]);
            }
        }
    }

    constexpr int SEGMENTS = 24;
    constexpr float PI = 3.14159265f;
    auto line = [&](const float p[3], const float q[3], uint32_t color) {
        overlay.lines.push_back({ { p[0], p[1], p[2] }, color });
        overlay.lines.push_back({ { q[0], q[1], q[2] }, color });
        for (const float* v : { p, q }) {
            for (int axis = 0; axis < 3; axis++) {
                overlay.boundsMin[axis] = std::min(overlay.boundsMin[axis], v[axis]);
                overlay.boundsMax[axis] = std::max(overlay.boundsMax[axis], v[axis]);
            }
        }
    };
    auto arc = [&](const float center[3], const float u[3], const float v[3], float radius, float from, float to,
                   uint32_t color) {
        float previous[3];
        for (int s = 0; s <= SEGMENTS; s++) {
            float angle = from + (to - from) * static_cast<float>(s) / SEGMENTS;
            float point[3];
            for (int c = 0; c < 3; c++) point[c] = center[c] + (u[c] * std::cos(angle) + v[c] * std::sin(angle)) * radius;
            if (s > 0) line(previous, point, color);
            std::copy(point, point + 3, previous);
        }
    };
    const float axes[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    auto layerColor = [&](int32_t layer) { return pack(PALETTE[static_cast<uint32_t>(layer) % 8], 1.2f, 0.95f); };

    for (const TerrSphere& sphere : terr.spheres) {
        uint32_t color = layerColor(sphere.layer);
        for (int plane = 0; plane < 3; plane++) {
            arc(sphere.center, axes[plane], axes[(plane + 1) % 3], sphere.radius, 0, 2 * PI, color);
        }
    }
    for (const TerrCapsule& capsule : terr.capsules) {
        uint32_t color = layerColor(capsule.layer);
        float dir[3] = { capsule.p1[0] - capsule.p0[0], capsule.p1[1] - capsule.p0[1], capsule.p1[2] - capsule.p0[2] };
        float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (length > 1e-6f) {
            for (float& c : dir) c /= length;
        } else {
            dir[0] = 0;
            dir[1] = 1;
            dir[2] = 0;
        }
        const float* up = std::fabs(dir[1]) > 0.9f ? axes[0] : axes[1];
        float u[3] = { dir[1] * up[2] - dir[2] * up[1], dir[2] * up[0] - dir[0] * up[2], dir[0] * up[1] - dir[1] * up[0] };
        float uLength = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (float& c : u) c /= uLength;
        float v[3] = { dir[1] * u[2] - dir[2] * u[1], dir[2] * u[0] - dir[0] * u[2], dir[0] * u[1] - dir[1] * u[0] };
        float back[3] = { -dir[0], -dir[1], -dir[2] };
        arc(capsule.p0, u, v, capsule.radius, 0, 2 * PI, color);
        arc(capsule.p1, u, v, capsule.radius, 0, 2 * PI, color);
        arc(capsule.p1, u, dir, capsule.radius, 0, PI, color);
        arc(capsule.p1, v, dir, capsule.radius, 0, PI, color);
        arc(capsule.p0, u, back, capsule.radius, 0, PI, color);
        arc(capsule.p0, v, back, capsule.radius, 0, PI, color);
        for (const float* side : { u, v }) {
            for (float sign : { -1.0f, 1.0f }) {
                float a[3];
                float b[3];
                for (int c = 0; c < 3; c++) {
                    a[c] = capsule.p0[c] + side[c] * capsule.radius * sign;
                    b[c] = capsule.p1[c] + side[c] * capsule.radius * sign;
                }
                line(a, b, color);
            }
        }
    }
    for (const TerrBox& box : terr.boxes) {
        uint32_t color = layerColor(box.layer);
        const float* m = box.matrix;
        float corners[8][3];
        for (int k = 0; k < 8; k++) {
            float local[3] = { (k & 1 ? 1.0f : -1.0f) * box.extent[0], (k & 2 ? 1.0f : -1.0f) * box.extent[1],
                               (k & 4 ? 1.0f : -1.0f) * box.extent[2] };
            // Row-major, translation in the last row.
            for (int c = 0; c < 3; c++) {
                corners[k][c] = local[0] * m[c] + local[1] * m[4 + c] + local[2] * m[8 + c] + m[12 + c];
            }
        }
        for (int k = 0; k < 8; k++) {
            for (int bit : { 1, 2, 4 }) {
                if ((k & bit) == 0) line(corners[k], corners[k | bit], color);
            }
        }
    }
}

std::string SiblingMesh(const LoadedGame& game, const std::string& mcolPakPath) {
    std::size_t ext = mcolPakPath.find(".mcol.");
    if (ext == std::string::npos) return {};
    std::string base = mcolPakPath.substr(0, ext);
    std::string meshExt = ".mesh" + game.Game().GetMeshExt();
    // RE7 name_e / name_t / name_e02 and RE8 name_00e / name_00t go with name / name_00.
    std::string stripped = base;
    while (!stripped.empty() && std::isdigit(static_cast<unsigned char>(stripped.back()))) stripped.pop_back();
    if (!stripped.empty() && (stripped.back() == 'e' || stripped.back() == 't')) {
        stripped.pop_back();
        if (!stripped.empty() && stripped.back() == '_') stripped.pop_back();
        if (game.FindEntry(stripped + meshExt)) return stripped + meshExt;
    }
    std::string folder = base.substr(0, base.find_last_of('/') + 1);
    std::string found;
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            const std::string& path = entry.filePath;
            if (!path.starts_with(folder) || !path.ends_with(meshExt) || path.find('/', folder.size()) != std::string::npos) continue;
            if (!found.empty() && found != path) return {};
            found = path;
        }
    }
    return found;
}

// Binds the SDF's CB/SRV slots by name, not by register. slotRaw (checked against
// dxc reflection): byte 0 VS register, byte 4 PS register, byte 6 stage mask
// (0x01 VS, 0x10 PS), byte 7 resource type (0x80 structured/byte buffer).
// Unknown cbuffers and buffer SRVs get a zero buffer: wind-style shaders size
// loops and indices from them, and other data sends reads far out of bounds.
bool SamplerFromName(const std::string& name, D3D12_FILTER& filter, D3D12_TEXTURE_ADDRESS_MODE& address) {
    if (name.starts_with("Point")) filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    else if (name.starts_with("Bilinear")) filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    else if (name.starts_with("Trilinear")) filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    else if (name.starts_with("Automatic")) filter = D3D12_FILTER_ANISOTROPIC;
    else return false;
    address = name.ends_with("Clamp")  ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
            : name.ends_with("Mirror") ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
            : name.ends_with("Border") ? D3D12_TEXTURE_ADDRESS_MODE_BORDER
                                       : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    return true;
}

static void AppendSamplers(const SdfProgram& program, std::vector<GameSamplerOverride>& out) {
    for (const SdfResource& sampler : program.samplers) {
        D3D12_FILTER filter;
        D3D12_TEXTURE_ADDRESS_MODE address;
        if (!SamplerFromName(sampler.name, filter, address)) continue;
        uint8_t stages = static_cast<uint8_t>((sampler.slotRaw >> 48) & 0xFF);
        if (stages & 0x01) out.push_back({ false, static_cast<uint8_t>(sampler.slotRaw & 0xFF), filter, address });
        if (stages & 0x10) out.push_back({ true, static_cast<uint8_t>((sampler.slotRaw >> 32) & 0xFF), filter, address });
    }
}

static void AppendBindOverrides(const SdfProgram& program, std::vector<GameBindOverride>& out) {
    auto add = [&out](uint64_t slotRaw, bool constantBuffer, GameBindSlotKind kind) {
        uint8_t stages = static_cast<uint8_t>((slotRaw >> 48) & 0xFF);
        if (stages & 0x01) {
            out.push_back({ false, constantBuffer, static_cast<uint8_t>(slotRaw & 0xFF), kind });
        }
        if (stages & 0x10) {
            out.push_back({ true, constantBuffer, static_cast<uint8_t>((slotRaw >> 32) & 0xFF), kind });
        }
    };

    for (const SdfConstantBuffer& cb : program.constantBuffers) {
        GameBindSlotKind kind = cb.name == "SceneInfo" ? GameBindSlotKind::Scene
                              : cb.name == "GBufferType" ? GameBindSlotKind::GBufferType
                              : cb.name == "CheckerBoardInfo" ? GameBindSlotKind::CheckerBoard
                              : cb.name == "Tonemap" ? GameBindSlotKind::Tonemap
                              : cb.name == "EnvironmentInfo" ? GameBindSlotKind::Environment
                              : cb.name == "ShadowCastInfo" ? GameBindSlotKind::ShadowCast
                              : GameBindSlotKind::ZeroCb;
        add(cb.slotRaw, true, kind);
    }
    for (const SdfResource& srv : program.srvs) {
        if (static_cast<uint8_t>((srv.slotRaw >> 56) & 0xFF) != 0x80) continue;
        GameBindSlotKind kind = srv.name == "InstanceWorldInfo" ? GameBindSlotKind::Instance
                              : srv.name == "BindlessBuffer" ? GameBindSlotKind::BindlessData
                              : srv.name == "BindlessRedirectTbl" ? GameBindSlotKind::Redirect
                              : srv.name == "SkinningMatrices" ? GameBindSlotKind::Skinning
                              : srv.name == "WhitePtSrv" ? GameBindSlotKind::WhitePoint
                              : GameBindSlotKind::ZeroSrv;
        add(srv.slotRaw, false, kind);
    }
}

// *Skinning programs skin in the VS from SkinningMatrices; masters without them
// fall back to the Static ones (bind pose).
static uint32_t LoadMaster(const LoadedGame& game, MasterRegistry& reg, const std::string& masterPath,
                           bool alphaTest, bool skinned, bool twoSided, ScenePrefetch* prefetch) {
    std::string key = masterPath + (alphaTest ? "|A" : "") + (skinned ? "|S" : "") + (twoSided ? "|T" : "");
    auto it = reg.pipelineByPath.find(key);
    if (it != reg.pipelineByPath.end()) return it->second;

    uint32_t pipeline = UINT32_MAX;
    try {
        std::string sdfPath = ToPakPath(masterPath, game.Game().GetMasterExt().c_str());
        auto loaded = reg.mastersByPath.find(sdfPath);
        if (loaded == reg.mastersByPath.end()) {
            std::shared_ptr<SdfData> fetched = prefetch ? prefetch->masters.Take(sdfPath) : nullptr;
            reg.masters.push_back(fetched ? std::move(*fetched) : game.Readers().Get<SdfReader>()->Read(game.ExtractFile(sdfPath)));
            loaded = reg.mastersByPath.emplace(sdfPath, &reg.masters.back()).first;
        }
        const SdfData& sdf = *loaded->second;
        // Forward-only masters (glass, ForwardSolid segment) draw after the lighting instead of into the GBuffer.
        const bool forward = !sdf.FindProgram("DeferredStatic") && sdf.FindProgram("ForwardStatic");
        const std::string pass = forward ? "Forward" : "Deferred";
        const char* kind = skinned && sdf.FindProgram(pass + "Skinning") ? "Skinning" : "Static";
        // TS programs are the two-sided ones: no back-face culling, normals flipped by SV_IsFrontFace.
        std::string twoSide = twoSided && sdf.FindProgram("TS" + pass + kind) ? "TS" : "";
        auto program = [&sdf, kind](const std::string& prefix) { return sdf.FindProgram(prefix + kind); };
        const SdfProgram* shadow = alphaTest ? program("A" + twoSide + "Shadow") : nullptr;
        if (shadow == nullptr) shadow = program(twoSide + "Shadow");
        const SdfProgram* deferred = alphaTest ? program("A" + twoSide + pass) : nullptr;
        const SdfProgram* prepass = alphaTest && !forward ? program("A" + twoSide + "ZPrePass") : nullptr;
        if (deferred == nullptr || (alphaTest && !forward && prepass == nullptr)) {
            deferred = program(twoSide + pass);
            prepass = nullptr;
        }
        bool alphaTested = prepass != nullptr;
        if (deferred != nullptr) {
            GameDeferredDesc desc{ deferred->vs, deferred->ps, BuildInputLayout(*deferred),
                                   deferred->blendState, deferred->depthStencilState,
                                   deferred->rasterizerState, alphaTested };
            desc.forward = forward;
            if (forward) {
                desc.vsSlots = StageSlots(*deferred, false);
                desc.psSlots = StageSlots(*deferred, true);
            }
            AppendBindOverrides(*deferred, desc.bindOverrides);
            AppendSamplers(*deferred, desc.samplers);
            if (shadow != nullptr) {
                desc.shadowVs = shadow->vs;
                desc.shadowPs = shadow->ps;
                desc.shadowLayout = BuildInputLayout(*shadow);
                desc.shadowRasterizerState = shadow->rasterizerState;
                AppendBindOverrides(*shadow, desc.shadowBindOverrides);
                AppendSamplers(*shadow, desc.shadowSamplers);
            }
            if (prepass != nullptr) {
                desc.prepassVs = prepass->vs;
                desc.prepassPs = prepass->ps;
                desc.prepassLayout = BuildInputLayout(*prepass);
                desc.prepassRasterizerState = prepass->rasterizerState;
                AppendBindOverrides(*prepass, desc.prepassBindOverrides);
                AppendSamplers(*prepass, desc.prepassSamplers);
            }
            reg.deferredDescs.push_back(std::move(desc));
            pipeline = static_cast<uint32_t>(reg.deferredDescs.size() - 1);
            LogInfo("[pipe %u] %s %s%s%s", pipeline, masterPath.c_str(), deferred->name.c_str(), alphaTested ? " (alphatest)" : "",
                        std::string_view(kind) == "Skinning" ? " (skinning)" : skinned ? " (no skinning program)" : "");
        }
        if (reg.prepass == nullptr) reg.prepass = sdf.FindProgram("ZPrePassStatic");
    } catch (const std::exception& e) {
        LogWarning("skip master %s: %s", masterPath.c_str(), e.what());
    }
    reg.pipelineByPath.emplace(std::move(key), pipeline);
    return pipeline;
}

// PointLight::update and SpotLight::update.
static void AddSceneLight(std::vector<GameLightParam>& out, std::vector<float>& scattering, const ScenePunctualLight& source,
                          const Mat4& world, float lightScale) {
    constexpr float pi = 3.14159265f;
    float range = source.EffectiveRange();
    if (!source.common.enabled || source.common.intensity <= 0.0f || range <= 0.0f) return;
    GameLightParam light{};
    for (int i = 0; i < 3; i++) light.position[i] = world.m[12 + i];
    light.boundingRadius = range;
    float attenuation = 1.0f / std::max(source.radius, 1e-5f);
    light.attenuation[0] = attenuation;
    light.attenuation[1] = source.common.minRoughness * source.common.minRoughness;
    light.attenuation[2] = 1.0f;
    float scale = 1.0f;
    if (source.spot) {
        float dir[3] = { -world.m[8], -world.m[9], -world.m[10] };
        float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (len < 1e-6f) { dir[0] = 0; dir[1] = -1; dir[2] = 0; len = 1; }
        for (int i = 0; i < 3; i++) light.direction[i] = dir[i] / len;
        light.falloff = source.falloff;
        float cosOuter = std::cos(source.cone * 0.5f * pi / 180.0f);
        float inv = 1.0f / std::max(1.1920929e-7f, std::cos((source.cone - source.spread) * 0.5f * pi / 180.0f) - cosOuter);
        light.attenuation[2] = -cosOuter * inv;
        light.attenuation[3] = inv;
        if (source.unit == LightUnit::Lumen) {
            scale = 1.0f / (4.0f * pi) / ((1.0f - std::cos(source.cone * pi / 180.0f)) * 2.0f * pi);
        }
    } else {
        light.reserved[0] = 1;
        if (source.unit == LightUnit::Lumen) scale = 1.0f / (4.0f * pi);
    }
    // Light::setAOEfficiency stores 1 - AOEfficiency.
    float aoEfficiency = 1.0f - source.common.aoEfficiency;
    std::memcpy(&light.reserved[1], &aoEfficiency, 4);
    light.tolerance = std::min(1.0f / (attenuation * range * attenuation * range), 0.999999f);
    for (int i = 0; i < 3; i++) light.color[i] = source.common.color[i] * source.common.intensity * scale * lightScale;
    light.shadowIndex = 0xFFFFFFFF;
    light.iesId = 0xFFFFFFFF;
    out.push_back(light);
    // LightRenderer's mVolumetricScatteringParamCache entry: the light with VolumetricScatteringIntensity in place of
    // Intensity unless UsingSameIntensity.
    float factor = source.common.usingSameIntensity || source.common.intensity == 0.0f
                       ? 1.0f
                       : source.common.volumetricScatteringIntensity / source.common.intensity;
    for (int i = 0; i < 3; i++) scattering.push_back(light.color[i] * factor);
}

static void ApplyMaterialOverrides(const AssetOverrides* overrides, const std::string& mdfPakPath, MaterialData& mdf) {
    for (MaterialEntry& mat : mdf.materials) {
        for (MaterialTexture& tex : mat.textures) {
            if (const std::string* path = FindOverride(overrides, MaterialTextureKey(mdfPakPath, mat.name, tex.type))) {
                tex.path = SourceAssetPath(*path);
            }
        }
        for (MaterialProperty& prop : mat.properties) {
            const std::string* text = FindOverride(overrides, MaterialParamKey(mdfPakPath, mat.name, prop.name));
            if (text == nullptr) continue;
            const char* cursor = text->c_str();
            for (float& value : prop.values) {
                char* end = nullptr;
                float parsed = std::strtof(cursor, &end);
                if (end == cursor) break;
                value = parsed;
                cursor = *end == ',' ? end + 1 : end;
            }
        }
    }
}

// Pipeline per material slot (UINT32_MAX when its master did not load).
static std::vector<uint32_t> AppendMaterials(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                             const std::string& mdfPakPath, const MaterialData& mdf, bool skinned,
                                             SceneMeshAsset& asset, ScenePrefetch* prefetch = nullptr) {
    std::vector<uint32_t> materialPipeline;
    for (const MaterialEntry& mat : mdf.materials) {
        materialPipeline.push_back(
            LoadMaster(game, build.masters, mat.masterMaterialPath, mat.AlphaTest(), skinned, mat.TwoSided(), prefetch));
        uint32_t materialIndex = static_cast<uint32_t>(build.materials.size() + materialPipeline.size() - 1);
        asset.materialIndices.push_back(materialIndex);
        build.materialNames.push_back(mdfPakPath + '|' + mat.name + '|' + mat.masterMaterialPath);
        for (const MaterialProperty& prop : mat.properties) {
            build.materialParams.push_back({ MaterialParamKey(mdfPakPath, mat.name, prop.name), materialIndex,
                                             prop.dataOffset, static_cast<uint32_t>(prop.values.size()) });
        }
    }
    std::vector<GameMaterialDesc> materials = BuildGameMaterials(game, mdf, cache);
    build.materials.insert(build.materials.end(),
                           std::make_move_iterator(materials.begin()), std::make_move_iterator(materials.end()));
    return materialPipeline;
}

static SceneMeshAsset AppendMeshData(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                     const std::string& meshPakPath, const std::string& mdfPakPath, bool allLods,
                                     const MeshData& mesh, MaterialData mdf, ScenePrefetch* prefetch) {
    SceneMeshAsset asset;
    ApplyMaterialOverrides(build.overrides, mdfPakPath, mdf);

    std::span<const uint8_t> weights = MeshStreamRange(mesh, VertexStreamSlot::Weights);
    bool skinned = !mesh.joints.empty() && !mesh.jointRemap.empty() && !weights.empty();
    if (skinned) asset.skeleton = SkeletonData::FromMesh(mesh);
    asset.meshPath = meshPakPath;

    std::vector<uint32_t> materialPipeline = AppendMaterials(game, build, cache, mdfPakPath, mdf, skinned, asset, prefetch);

    std::span<const uint8_t> positions = MeshStreamRange(mesh, VertexStreamSlot::Position);
    if (positions.empty() || mesh.lods.empty()) {
        LogWarning("skip mesh %s: no geometry", meshPakPath.c_str());
        return asset;
    }
    std::size_t vertexCount = positions.size() / 12;

    // Every stream must cover the same vertex range for the shared baseVertex to
    // line up; missing data pads with zeros.
    auto appendStream = [&](std::vector<uint8_t>& dst, std::span<const uint8_t> src, std::size_t stride) {
        std::size_t want = vertexCount * stride;
        std::size_t copy = src.size() < want ? src.size() : want;
        dst.insert(dst.end(), src.begin(), src.begin() + copy);
        dst.insert(dst.end(), want - copy, 0);
    };

    uint32_t vertexBase = static_cast<uint32_t>(build.positions.size() / 12);
    uint32_t indexBase = static_cast<uint32_t>(build.indices.size() / 2);
    appendStream(build.positions, positions, 12);
    appendStream(build.normals, MeshStreamRange(mesh, VertexStreamSlot::NormalTangent), 8);
    appendStream(build.uv0, MeshStreamRange(mesh, VertexStreamSlot::Uv0), 4);
    appendStream(build.uv1, MeshStreamRange(mesh, VertexStreamSlot::Uv1), 4);
    appendStream(build.weights, weights, 16);
    build.indices.insert(build.indices.end(), mesh.indexBuffer.begin(), mesh.indexBuffer.end());

    asset.lodCount = allLods ? static_cast<uint32_t>(mesh.lods.size()) : 1;
    for (uint32_t lod = 0; lod < asset.lodCount; lod++) {
        uint32_t submesh = 0;
        for (const MeshPart& part : mesh.lods[lod].parts) {
            for (const MeshCluster& cluster : part.clusters) {
                uint32_t index = submesh++;
                uint32_t slot = MaterialSlotByName(mdf, ClusterMaterialName(mesh, cluster));
                uint32_t pipeline = slot < materialPipeline.size() ? materialPipeline[slot] : UINT32_MAX;
                if (pipeline == UINT32_MAX) continue;
                asset.draws.push_back({ cluster.indexCount, cluster.startIndexLocation + indexBase,
                                        cluster.baseVertexLocation + static_cast<int32_t>(vertexBase),
                                        slot, pipeline });
                asset.drawTags.push_back({ lod, index });
            }
        }
    }

    std::memcpy(asset.aabbMin, mesh.aabbMin.data(), sizeof(asset.aabbMin));
    std::memcpy(asset.aabbMax, mesh.aabbMax.data(), sizeof(asset.aabbMax));
    asset.boundingRadius = mesh.boundingRadius;
    asset.lodGroup = mesh.lodGroup;
    asset.valid = true;
    return asset;
}

SceneMeshAsset AppendSceneMesh(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                      const std::string& meshPakPath, const std::string& mdfPakPath,
                                      bool allLods) {
    MeshData mesh;
    MaterialData mdf;
    try {
        mesh = game.Readers().Get<MeshReader>()->Read(game.ExtractFile(meshPakPath));
        mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(mdfPakPath));
    } catch (const std::exception& e) {
        LogWarning("skip mesh %s: %s", meshPakPath.c_str(), e.what());
        return {};
    }
    return AppendMeshData(game, build, cache, meshPakPath, mdfPakPath, allLods, mesh, std::move(mdf), nullptr);
}

static SceneMeshAsset AppendPrefetchedMesh(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                           const std::string& meshPakPath, const std::string& mdfPakPath,
                                           ScenePrefetch& prefetch) {
    std::shared_ptr<MeshData> mesh;
    std::shared_ptr<MaterialData> mdf;
    try {
        mesh = prefetch.meshes.Get(meshPakPath);
        mdf = prefetch.materials.Get(mdfPakPath);
    } catch (const std::exception& e) {
        LogWarning("skip mesh %s: %s", meshPakPath.c_str(), e.what());
        return {};
    }
    if (!mesh || !mdf) return AppendSceneMesh(game, build, cache, meshPakPath, mdfPakPath, true);
    return AppendMeshData(game, build, cache, meshPakPath, mdfPakPath, true, *mesh, *mdf, &prefetch);
}

std::string SceneObjectKey(const std::string& scenePakPath, std::size_t nodeIndex) {
    return scenePakPath + '|' + std::to_string(nodeIndex);
}

static uint16_t ToHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    if (exponent <= 0) return static_cast<uint16_t>(sign);
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | ((bits & 0x7FFFFF) >> 13));
}

static void PackDirection(uint8_t* out, const float v[4], DXGI_FORMAT format) {
    for (int i = 0; i < 4; i++) {
        float c = v[i] < -1.0f ? -1.0f : v[i] > 1.0f ? 1.0f : v[i];
        out[i] = format == DXGI_FORMAT_R8G8B8A8_UNORM
            ? static_cast<uint8_t>(std::lround((c * 0.5f + 0.5f) * 255.0f))
            : static_cast<uint8_t>(static_cast<int8_t>(std::lround(c * 127.0f)));
    }
}

static SceneMeshAsset AppendSpheres(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                    const std::string& mdfPakPath, const std::string& materialName, bool everyMaterial) {
    constexpr int SEGMENTS = 48;
    constexpr int RINGS = 32;
    constexpr float RADIUS = 0.5f;
    constexpr float PI = 3.14159265f;

    SceneMeshAsset asset;
    MaterialData mdf;
    try {
        mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(mdfPakPath));
    } catch (const std::exception& e) {
        LogWarning("skip material %s: %s", mdfPakPath.c_str(), e.what());
        return asset;
    }
    ApplyMaterialOverrides(build.overrides, mdfPakPath, mdf);
    std::vector<uint32_t> materialPipeline = AppendMaterials(game, build, cache, mdfPakPath, mdf, false, asset);
    // Mdfs often start with a leftover material on systems/rendering/Null* placeholders.
    auto realTextures = [](const MaterialEntry& mat) {
        return std::count_if(mat.textures.begin(), mat.textures.end(), [](const MaterialTexture& tex) {
            std::string lower = tex.path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            return !lower.empty() && !lower.starts_with("systems/rendering/");
        });
    };
    std::vector<uint32_t> slots;
    for (uint32_t i = 0; i < materialPipeline.size(); i++) {
        if (materialPipeline[i] == UINT32_MAX) continue;
        if (everyMaterial) {
            slots.push_back(i);
        } else if (!materialName.empty()) {
            if (mdf.materials[i].name == materialName) slots = { i };
        } else if (slots.empty() || realTextures(mdf.materials[i]) > realTextures(mdf.materials[slots[0]])) {
            slots = { i };
        }
    }
    if (slots.empty()) {
        LogWarning("skip material %s: no master loaded", mdfPakPath.c_str());
        return asset;
    }

    // Normals and tangents share the 8-byte stream of slot 1; the layout says how.
    DXGI_FORMAT normalFormat = DXGI_FORMAT_R8G8B8A8_SNORM;
    DXGI_FORMAT tangentFormat = DXGI_FORMAT_R8G8B8A8_SNORM;
    uint32_t normalOffset = 0;
    uint32_t tangentOffset = 4;
    for (const GameInputElement& e : build.masters.deferredDescs[materialPipeline[slots[0]]].inputLayout) {
        if (e.slot != 1 || e.offset > 4) continue;
        if (std::strcmp(e.semanticName, "NORMAL") == 0) {
            normalFormat = e.format;
            normalOffset = e.offset;
        } else if (std::strcmp(e.semanticName, "TANGENT") == 0) {
            tangentFormat = e.format;
            tangentOffset = e.offset;
        }
    }

    uint32_t vertexBase = static_cast<uint32_t>(build.positions.size() / 12);
    uint32_t indexBase = static_cast<uint32_t>(build.indices.size() / 2);
    // u runs left to right across the front (-Z, where the preview camera sits); the seam is behind.
    for (int r = 0; r <= RINGS; r++) {
        float v = static_cast<float>(r) / RINGS;
        float theta = v * PI;
        for (int s = 0; s <= SEGMENTS; s++) {
            float u = static_cast<float>(s) / SEGMENTS;
            float phi = u * 2 * PI;
            float normal[4] = { std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi), 0 };
            float tangent[4] = { std::cos(phi), 0, -std::sin(phi), 1 };
            float position[3] = { normal[0] * RADIUS, normal[1] * RADIUS, normal[2] * RADIUS };
            const uint8_t* p = reinterpret_cast<const uint8_t*>(position);
            build.positions.insert(build.positions.end(), p, p + 12);
            uint8_t packed[8]{};
            PackDirection(packed + normalOffset, normal, normalFormat);
            PackDirection(packed + tangentOffset, tangent, tangentFormat);
            build.normals.insert(build.normals.end(), packed, packed + 8);
            uint16_t uv[2] = { ToHalf(u), ToHalf(v) };
            const uint8_t* uvBytes = reinterpret_cast<const uint8_t*>(uv);
            build.uv0.insert(build.uv0.end(), uvBytes, uvBytes + 4);
            build.uv1.insert(build.uv1.end(), uvBytes, uvBytes + 4);
            build.weights.insert(build.weights.end(), 16, 0);
        }
    }
    for (int r = 0; r < RINGS; r++) {
        for (int s = 0; s < SEGMENTS; s++) {
            uint16_t a = static_cast<uint16_t>(r * (SEGMENTS + 1) + s);
            uint16_t b = static_cast<uint16_t>(a + SEGMENTS + 1);
            uint16_t tri[6] = { a, b, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(a + 1), b,
                                static_cast<uint16_t>(b + 1) };
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(tri);
            build.indices.insert(build.indices.end(), bytes, bytes + sizeof(tri));
        }
    }

    for (uint32_t slot : slots) {
        asset.draws.push_back({ static_cast<uint32_t>(RINGS * SEGMENTS * 6), indexBase,
                                static_cast<int32_t>(vertexBase), slot, materialPipeline[slot] });
        asset.drawTags.push_back({ everyMaterial ? slot : 0, 0 });
    }
    asset.lodCount = everyMaterial ? static_cast<uint32_t>(mdf.materials.size()) : 1;
    for (int i = 0; i < 3; i++) {
        asset.aabbMin[i] = -RADIUS;
        asset.aabbMax[i] = RADIUS;
    }
    asset.meshPath = mdfPakPath;
    asset.valid = true;
    return asset;
}

SceneMeshAsset AppendMaterialSphere(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                    const std::string& mdfPakPath, const std::string& materialName) {
    return AppendSpheres(game, build, cache, mdfPakPath, materialName, false);
}

SceneMeshAsset AppendMaterialSpheres(const LoadedGame& game, SceneBuild& build, TextureCache& cache,
                                     const std::string& mdfPakPath) {
    return AppendSpheres(game, build, cache, mdfPakPath, {}, true);
}

static void ExpandWorldBounds(const SceneMeshAsset& asset, const Mat4& world, float lo[3], float hi[3]) {
    for (int corner = 0; corner < 8; corner++) {
        float p[3] = { (corner & 1) ? asset.aabbMax[0] : asset.aabbMin[0],
                       (corner & 2) ? asset.aabbMax[1] : asset.aabbMin[1],
                       (corner & 4) ? asset.aabbMax[2] : asset.aabbMin[2] };
        float out[3];
        TransformPoint(world, p, out);
        for (int i = 0; i < 3; i++) {
            lo[i] = std::min(lo[i], out[i]);
            hi[i] = std::max(hi[i], out[i]);
        }
    }
}

void AddSceneInstance(SceneBuild& build, const SceneMeshAsset& asset, const Mat4& world, std::string owner) {
    uint32_t instanceIndex = static_cast<uint32_t>(build.worlds.size());
    ViewerInstanceWorld w;
    ToFloat3x4(world, w.m);
    if (asset.skeleton) {
        w.jointOffset = build.skinMatrixCount;
        build.skinMatrixCount += static_cast<uint32_t>(asset.skeleton->remap.size());
        build.skinned.push_back({ asset.meshPath, w.jointOffset, world, asset.skeleton, owner });
        build.skinned.back().instance = static_cast<uint32_t>(build.worlds.size());
    }
    build.worlds.push_back(w);
    build.instanceOwners.push_back(std::move(owner));
    build.instanceMaterials.push_back(asset.materialIndices);
    build.instanceMeshes.push_back(asset.meshPath);

    float localCenter[3];
    float ext[3];
    for (int i = 0; i < 3; i++) {
        localCenter[i] = (asset.aabbMin[i] + asset.aabbMax[i]) * 0.5f;
        ext[i] = (asset.aabbMax[i] - asset.aabbMin[i]) * 0.5f;
    }
    float worldCenter[3];
    TransformPoint(world, localCenter, worldCenter);
    float maxScale = 0;
    for (int r = 0; r < 3; r++) {
        float len = std::sqrt(world.m[r * 4] * world.m[r * 4] +
                              world.m[r * 4 + 1] * world.m[r * 4 + 1] +
                              world.m[r * 4 + 2] * world.m[r * 4 + 2]);
        maxScale = len > maxScale ? len : maxScale;
    }
    float boundsRadius = std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]) * maxScale;

    for (std::size_t i = 0; i < asset.draws.size(); i++) {
        ViewerMeshDraw draw = asset.draws[i];
        draw.instanceIndex = instanceIndex;
        draw.id = static_cast<uint32_t>(build.draws.size());
        std::memcpy(draw.boundsCenter, worldCenter, sizeof(worldCenter));
        // Animation moves skinned meshes out of their bind-pose bounds.
        draw.boundsRadius = asset.skeleton ? 0.0f : boundsRadius;
        build.draws.push_back(draw);
        build.drawTags.push_back(asset.drawTags[i]);
    }

    ExpandWorldBounds(asset, world, build.aabbMin, build.aabbMax);
}

// via::render::makeLodFactor with the renderer's default lodRate of 1.
static bool MeshLodThresholds(const MeshLodSettings& settings, const SceneMeshAsset& asset, SceneMeshLod& out) {
    uint32_t count = std::min<uint32_t>(asset.lodCount, 8);
    const MeshLodParameter* p = settings.Find(asset.lodGroup, count);
    if (count < 2 || asset.boundingRadius <= 0 || p == nullptr) return false;
    float scale = std::pow(asset.boundingRadius * 2.0f, 0.8f);
    float occupancyMax = scale * p->occupancyMax;
    float occupancyMin = scale * p->occupancyMin;
    out.lodCount = count;
    out.radius = asset.boundingRadius;
    out.thresholds[0] = 1.0f / occupancyMax;
    for (uint32_t k = 1; k + 2 < count; k++) {
        float rate = k - 1 < p->occupancyRates.size() ? p->occupancyRates[k - 1] : 0.0f;
        out.thresholds[k] = 1.0f / ((occupancyMax - occupancyMin) * rate + occupancyMin);
    }
    if (count > 2) out.thresholds[count - 2] = 1.0f / occupancyMin;
    return true;
}

// RE7 app.LightConditionControl: the scenes and folders named for a light condition (lightsetDay_*, Night/...).
static std::string LightCondition(std::string_view name) {
    if (name.find("Midnight") != std::string_view::npos || name.find("midnight") != std::string_view::npos) return "Midnight";
    if (name.find("Night") != std::string_view::npos || name.find("night") != std::string_view::npos) return "Night";
    if (name.find("Day") != std::string_view::npos || name.find("day") != std::string_view::npos) return "Day";
    return {};
}

static bool OtherLightCondition(std::string_view name, const std::string& condition) {
    if (condition.empty()) return false;
    std::string named = LightCondition(name);
    return !named.empty() && named != condition;
}

static const RszInstance* FindComponent(const SceneData& scn, const SceneNode& node, std::string_view type) {
    for (int32_t index : node.componentIndices) {
        const RszInstance* component = scn.Instance(index);
        if (component && component->typeName == type) return component;
    }
    return nullptr;
}

static bool SameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// app.LightEnvironmentStateController loads, of the scene folders beside it named <area>_<state>,
// only the current state's. The game starts at app.LightSceneSetupParam.state, the name's hash.
static std::set<std::string> UnselectedLightStates(const SceneData& scn, uint32_t setupState, const std::string* requested,
                                                   SceneBuild& build) {
    std::set<std::string> unselected;
    std::vector<std::size_t> parents(scn.nodes.size(), SIZE_MAX);
    for (std::size_t i = 0; i < scn.nodes.size(); i++) {
        for (std::size_t child : scn.nodes[i].children) parents[child] = i;
    }
    for (std::size_t i = 0; i < scn.nodes.size(); i++) {
        if (!FindComponent(scn, scn.nodes[i], "app.LightEnvironmentStateController")) continue;
        const std::vector<std::size_t>& siblings = parents[i] == SIZE_MAX ? scn.roots : scn.nodes[parents[i]].children;
        std::vector<std::pair<std::string, const SceneNode*>> variants;
        for (std::size_t s : siblings) {
            const SceneNode& folder = scn.nodes[s];
            if (folder.kind != SceneNode::Kind::Folder || folder.scenePath.empty()) continue;
            variants.emplace_back(folder.name.substr(folder.name.rfind('_') + 1), &folder);
        }
        if (variants.empty()) continue;
        static const std::string DEFAULT_STATE = "morning";
        const std::string& wanted = requested ? *requested : DEFAULT_STATE;
        auto chosen = std::find_if(variants.begin(), variants.end(), [&](const auto& v) { return SameText(v.first, wanted); });
        if (chosen == variants.end()) {
            chosen = std::find_if(variants.begin(), variants.end(),
                                  [&](const auto& v) { return Murmur3::MakeHash(v.first) == setupState; });
        }
        if (chosen == variants.end()) chosen = variants.begin();
        for (auto v = variants.begin(); v != variants.end(); ++v) {
            if (std::none_of(build.lightStates.begin(), build.lightStates.end(),
                             [&](const std::string& s) { return SameText(s, v->first); })) {
                build.lightStates.push_back(v->first);
            }
            if (v != chosen) unselected.insert(v->second->scenePath);
        }
        bool asked = requested && SameText(chosen->first, *requested);
        if (build.lightState.empty() || asked) build.lightState = chosen->first;
    }
    return unselected;
}

static double Number(const RszValue* value, double fallback) {
    if (!value) return fallback;
    if (const double* d = value->As<double>()) return *d;
    if (const int64_t* i = value->As<int64_t>()) return static_cast<double>(*i);
    if (const uint64_t* u = value->As<uint64_t>()) return static_cast<double>(*u);
    if (const bool* b = value->As<bool>()) return *b ? 1.0 : 0.0;
    return fallback;
}

static std::optional<RszGuid> GuidValue(const RszValue* value) {
    if (!value) return std::nullopt;
    if (const RszGuid* guid = value->As<RszGuid>()) return *guid;
    const RszBytes* bytes = value->As<RszBytes>();
    if (!bytes || bytes->size() != 16) return std::nullopt;
    RszGuid guid;
    std::memcpy(guid.bytes.data(), bytes->data(), 16);
    return guid;
}

static std::string UserDataPath(const RszData& rsz, const RszValue* value) {
    const RszObjectRef* ref = value ? value->As<RszObjectRef>() : nullptr;
    if (!ref) return {};
    for (const RszUserDataRef& user : rsz.userData) {
        if (user.instanceId == ref->index) return ToPakPath(user.path, ".2");
    }
    return {};
}

static const RszInstance* Root(const UserFileData& user) {
    int32_t root = user.RootInstance();
    return root >= 0 && root < static_cast<int32_t>(user.rsz.instances.size()) ? &user.rsz.instances[root] : nullptr;
}

static const RszInstance* Child(const UserFileData& user, const RszInstance& owner, std::string_view field) {
    const RszValue* value = owner.Field(field);
    const RszObjectRef* ref = value ? value->As<RszObjectRef>() : nullptr;
    return ref && ref->index < user.rsz.instances.size() ? &user.rsz.instances[ref->index] : nullptr;
}

// app.PostToneMapUserDataList -> its first app.PostToneMapUserData -> _AnimationData.
static SceneToneMap ReadToneMap(const SceneFiles& files, const std::string& listPath) {
    SceneToneMap toneMap;
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return toneMap;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* dataRoot = data ? Root(*data) : nullptr;
    const RszInstance* animation = dataRoot ? Child(*data, *dataRoot, "_AnimationData") : nullptr;
    if (!animation) return toneMap;
    toneMap.found = true;
    toneMap.ev = static_cast<float>(Number(animation->Field("_EV"), toneMap.ev));
    toneMap.contrast = static_cast<float>(Number(animation->Field("_Contrast"), toneMap.contrast));
    toneMap.linearBegin = static_cast<float>(Number(animation->Field("_LinearSectionBegin"), toneMap.linearBegin));
    toneMap.linearLength = static_cast<float>(Number(animation->Field("_LinearSectionLength"), toneMap.linearLength));
    toneMap.toe = static_cast<float>(Number(animation->Field("_SDRToe"), toneMap.toe));
    toneMap.brightAdaptationRate = static_cast<float>(Number(animation->Field("_BrightAdaptationRate"), 1));
    toneMap.darkAdaptationRate = static_cast<float>(Number(animation->Field("_DarkAdaptationRate"), 1));
    toneMap.vignettingBrightness = static_cast<float>(Number(animation->Field("_VignettingBrightness"), 0));
    toneMap.animationTime = static_cast<float>(Number(animation->Field("_AnimationTime"), 0));
    if (const RszInstance* once = Child(*data, *dataRoot, "_OnetimeData")) {
        toneMap.autoExposure = Number(once->Field("_AutoExposure"), 1) == 0;
        toneMap.minWhitePoint = static_cast<float>(Number(once->Field("_MinWhitePoint"), 1));
        toneMap.maxWhitePoint = static_cast<float>(Number(once->Field("_MaxWhitePoint"), 1));
        toneMap.whiteRange = static_cast<float>(Number(once->Field("_WhiteRange"), 0.8));
    }
    return toneMap;
}

// app.PostFogUserDataList -> its first app.PostFogUserData: the fields app.FogController sets on via.render.Fog.
static SceneFog ReadFog(const SceneFiles& files, const std::string& listPath) {
    SceneFog fog;
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return fog;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* root = data ? Root(*data) : nullptr;
    if (!root) return fog;
    fog.found = true;
    fog.enabled = Number(root->Field("_Enabled"), 0) != 0;
    uint32_t packed = static_cast<uint32_t>(Number(root->Field("_InscatteringColor"), 0));
    for (int c = 0; c < 3; c++) fog.color[c] = static_cast<float>((packed >> (c * 8)) & 0xFF) / 255.0f;
    fog.intensity = static_cast<float>(Number(root->Field("_Intensity"), 0));
    fog.density = static_cast<float>(Number(root->Field("_Density"), 0));
    fog.heightFalloff = static_cast<float>(Number(root->Field("_HeightFalloff"), 0));
    fog.maxOpacity = static_cast<float>(Number(root->Field("_MaxOpacity"), 0));
    fog.startDistance = static_cast<float>(Number(root->Field("_StartDistance"), 0));
    fog.startHeight = static_cast<float>(Number(root->Field("_StartHeightDistance"), 0));
    fog.animationTime = static_cast<float>(Number(root->Field("_AnimationTime"), 0));
    return fog;
}

// app.PostVolumetricFogUserDataList -> its first app.PostVolumetricFogUserData: _AnimationParam
// (app.VolumetricFogAnimationParam::apply / applyControlParam) and _ControlParam (requestControl).
static SceneVolumetricFogZone ReadVolumetricFogZone(const SceneFiles& files, const std::string& listPath) {
    SceneVolumetricFogZone zone;
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return zone;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* root = data ? Root(*data) : nullptr;
    if (const RszInstance* animation = root ? Child(*data, *root, "_AnimationParam") : nullptr) {
        zone.found = true;
        zone.enabled = Number(animation->Field("_Enabled"), 0) != 0;
        zone.color = static_cast<uint32_t>(Number(animation->Field("_Color"), 0xFFFFFFFF));
        zone.density = static_cast<float>(Number(animation->Field("_Density"), 0));
        zone.scatteringDistribution = static_cast<float>(Number(animation->Field("_ScatteringDistribution"), 0));
        zone.attenuationByHeight = static_cast<float>(Number(animation->Field("_DensityAttenuationByHeight"), 0));
        zone.cullingDistance = static_cast<float>(Number(animation->Field("_FogCullingDistance"), 0));
        zone.depthDecodingParam = static_cast<float>(Number(animation->Field("_DepthDecodingParam"), 0));
        zone.rejection = Number(animation->Field("_Rejection"), 0) != 0;
        zone.rejectSensitivity = static_cast<float>(Number(animation->Field("_RejectSensitivity"), 0));
        zone.animationTime = static_cast<float>(Number(animation->Field("_AnimationTime"), 0));
    }
    if (const RszInstance* control = root ? Child(*data, *root, "_ControlParam") : nullptr) {
        zone.controlFound = true;
        zone.leakBias = static_cast<float>(Number(control->Field("_LeakiBias"), 0));
    }
    return zone;
}

// app.PostSSAOUserDataList -> its first app.PostSSAOUserData -> _SSAOAnimationParam.
static void ReadSsao(const SceneFiles& files, const std::string& listPath, SceneSsao& ssao) {
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* dataRoot = data ? Root(*data) : nullptr;
    const RszInstance* param = dataRoot ? Child(*data, *dataRoot, "_SSAOAnimationParam") : nullptr;
    if (!param) return;
    ssao.zoneFound = true;
    ssao.zoneEnabled = Number(param->Field("_Enabled"), 0) != 0;
    ssao.intensity = static_cast<float>(Number(param->Field("_AOIntensity"), 0));
    ssao.zoneAnimationTime = static_cast<float>(Number(param->Field("_AnimationTime"), 0));
}

// app.PostColorCorrectUserDataList -> its first app.PostColorCorrectUserData.
static SceneColorCorrectZone ReadColorCorrectZone(const SceneFiles& files, const std::string& listPath) {
    SceneColorCorrectZone zone;
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return zone;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* root = data ? Root(*data) : nullptr;
    if (!root) return zone;
    zone.found = true;
    zone.blendTargetIndex = static_cast<uint32_t>(Number(root->Field("_BlendTargetIndex"), 0));
    zone.animationTime = static_cast<float>(Number(root->Field("_AnimationTime"), 0));
    return zone;
}

// app.PostSoftBloomUserDataList -> its first app.PostSoftBloomUserData.
static SceneSoftBloomZone ReadSoftBloomZone(const SceneFiles& files, const std::string& listPath) {
    SceneSoftBloomZone zone;
    std::optional<UserFileData> list = files.user(listPath);
    const RszInstance* listRoot = list ? Root(*list) : nullptr;
    const RszValue* entries = listRoot ? listRoot->Field("_UserDataList") : nullptr;
    const RszArray* array = entries ? entries->As<RszArray>() : nullptr;
    if (!array || array->empty()) return zone;
    std::optional<UserFileData> data = files.user(UserDataPath(list->rsz, &array->front()));
    const RszInstance* root = data ? Root(*data) : nullptr;
    if (!root) return zone;
    zone.found = true;
    zone.enabled = Number(root->Field("_Enabled"), 0) != 0;
    zone.reductionLevel = static_cast<uint32_t>(Number(root->Field("_ReductionLevel"), 0));
    zone.threshold = static_cast<float>(Number(root->Field("_Threshold"), 0));
    zone.dispersion = static_cast<float>(Number(root->Field("_Dispersion"), 0));
    zone.outputRatio = static_cast<float>(Number(root->Field("_OutputRatio"), 0));
    zone.useBlurColor = Number(root->Field("_UseUserDefinedBlurColor"), 0) != 0;
    zone.blurColor = static_cast<uint32_t>(Number(root->Field("_BlurColor"), 0xFFFFFFFF));
    return zone;
}

// A via.timeline.Timeline played at scene start (v0 Enabled, v3 clip, v10 PlaySceneStart) holds its last frame;
// of what it animates, the LDRPostProcess ColorCorrect color cube elements.
static void ReadStartTimeline(const LoadedGame& game, const RszInstance& timeline, SceneBuild& build) {
    auto flag = [&](const char* field) {
        const RszValue* v = timeline.Field(field);
        const RszBytes* b = v ? v->As<RszBytes>() : nullptr;
        return b && !b->empty() && (*b)[0] != 0;
    };
    if (!flag("v0") || !flag("v10")) return;
    const RszValue* resource = timeline.Field("v3");
    std::string path = resource ? resource->AsString() : std::string();
    if (path.empty()) return;
    std::string pak = ToPakPath(path, ".40");
    if (!game.FindEntry(pak)) return;
    TimelineClip clip;
    try {
        clip = ReadTimelineClip(game.ExtractFile(pak));
    } catch (const std::exception& e) {
        LogWarning("skip timeline %s: %s", pak.c_str(), e.what());
        return;
    }
    if (std::find(clip.objectNames.begin(), clip.objectNames.end(), "LDRPostProcess") == clip.objectNames.end()) return;
    constexpr std::string_view ELEMENT = "ColorCubeElement[";
    for (const TimelineClipCurve& curve : clip.curves) {
        if (curve.keys.empty() || curve.path.empty() || curve.path.front() != "ColorCorrect") continue;
        for (const std::string& name : curve.path) {
            if (!name.starts_with(ELEMENT)) continue;
            uint32_t index = static_cast<uint32_t>(std::strtoul(name.c_str() + ELEMENT.size(), nullptr, 10));
            build.colorCubeElements[index] = curve.keys.back().value.text;
        }
    }
}

// LDRPostProcess holds its filters as instance indices; LDRColorCorrect's element list as ColorCubeElement indices.
static void ReadColorCorrect(const RszData& rsz, const RszInstance& postProcess, SceneColorCorrect& out) {
    auto instance = [&](const RszValue* value) -> const RszInstance* {
        const RszBytes* bytes = value ? value->As<RszBytes>() : nullptr;
        if (!bytes || bytes->size() != 4) return nullptr;
        uint32_t index;
        std::memcpy(&index, bytes->data(), 4);
        return index > 0 && index < rsz.instances.size() ? &rsz.instances[index] : nullptr;
    };
    auto text = [](const RszInstance& c, const char* field) {
        const RszValue* v = c.Field(field);
        return v ? v->AsString() : std::string();
    };
    auto number = [](const RszInstance& c, const char* field) {
        const RszValue* v = c.Field(field);
        const RszBytes* b = v ? v->As<RszBytes>() : nullptr;
        uint32_t bits = 0;
        if (b && b->size() >= 4) std::memcpy(&bits, b->data(), 4);
        return bits;
    };
    auto real = [&](const RszInstance& c, const char* field) {
        uint32_t bits = number(c, field);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    for (const RszFieldValue& field : postProcess.fields) {
        const RszInstance* filter = instance(&field.value);
        if (!filter || filter->typeName != "via.render.LDRColorCorrect") continue;
        out.found = true;
        const RszValue* enabled = filter->Field("v0");
        const RszBytes* enabledBytes = enabled ? enabled->As<RszBytes>() : nullptr;
        out.enabled = enabledBytes && !enabledBytes->empty() && (*enabledBytes)[0] != 0;
        out.cubeBlendRate = real(*filter, "v1");
        out.cube0 = text(*filter, "v2");
        out.cube1 = text(*filter, "v3");
        out.cube2BlendRate = real(*filter, "v4");
        out.cube2 = text(*filter, "v5");
        if (const RszValue* list = filter->Field("v6"); list && list->As<RszArray>()) {
            for (const RszValue& item : *list->As<RszArray>()) {
                const RszInstance* element = instance(&item);
                out.elements.push_back(element ? text(*element, "v0") : std::string());
            }
        }
        out.blendRate = real(*filter, "v7");
        out.blendTargetIndex = number(*filter, "v8");
        return;
    }
}

// The application scene's MainCamera carries the camera and SSAOControl every chapter renders with.
static void ReadApplicationCamera(const LoadedGame& game, const RszTypeDatabase& db, SceneBuild& build, float lightScale) {
    std::string path = "natives/stm/nobody/appresident.scn" + game.Game().GetSceneExt();
    if (!game.FindEntry(path)) return;
    try {
        SceneData scn = ReadSceneOrPrefab(game, db, path);
        std::size_t cameraNode = scn.nodes.size();
        std::optional<SceneVolumetricFog> applicationFog;
        for (std::size_t n = 0; n < scn.nodes.size(); n++) {
            for (int32_t componentIndex : scn.nodes[n].componentIndices) {
                const RszInstance* component = scn.Instance(componentIndex);
                if (!component) continue;
                if (!build.ssao.controlFound) ReadSsaoControl(*component, build.ssao);
                if (!build.mainCamera.found && ReadMainCamera(*component, build.mainCamera)) cameraNode = n;
                if (!build.fog.found) ReadFogComponent(*component, build.fog);
                if (!build.toneMapping.found) ReadToneMapping(*component, build.toneMapping);
                if (!build.softBloom.found) ReadSoftBloom(*component, build.softBloom);
                if (!build.colorCorrect.found && component->typeName == "via.render.LDRPostProcess") {
                    ReadColorCorrect(scn.rsz, *component, build.colorCorrect);
                }
                if (!build.volumetricFogControl.found) ReadVolumetricFogControl(*component, build.volumetricFogControl);
                if (std::optional<SceneVolumetricFog> fog = ReadVolumetricFog(*component); fog && !applicationFog) applicationFog = fog;
            }
        }
        std::function<void(std::size_t, const Mat4&)> walk = [&](std::size_t nodeIndex, const Mat4& parent) {
            for (std::size_t child : scn.nodes[nodeIndex].children) {
                const SceneNode& node = scn.nodes[child];
                float t[3] = { node.position.x, node.position.y, node.position.z };
                float q[4] = { node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w };
                float s[3] = { node.scale.x, node.scale.y, node.scale.z };
                Mat4 local = Mul(ComposeTRS(t, q, s), parent);
                for (int32_t componentIndex : node.componentIndices) {
                    const RszInstance* component = scn.Instance(componentIndex);
                    if (!component) continue;
                    if (std::optional<ScenePunctualLight> light = ReadPunctualLight(*component)) {
                        AddSceneLight(build.cameraLights, build.cameraLightScattering, *light, local, lightScale);
                    }
                }
                walk(child, local);
            }
        };
        if (cameraNode < scn.nodes.size()) walk(cameraNode, Identity());
        build.applicationVolumetricFog = applicationFog;
        for (const auto& [index, texture] : build.colorCubeElements) {
            if (build.colorCorrect.elements.size() <= index) build.colorCorrect.elements.resize(index + 1);
            build.colorCorrect.elements[index] = texture;
        }
    } catch (const std::exception& e) {
        LogWarning("SSAOControl: %s", e.what());
    }
}

SceneData ReadSceneOrPrefab(const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath) {
    if (pakPath.find(".pfb.") != std::string::npos) {
        return game.Readers().Get<PrefabReader>()->Read(game.ExtractFile(pakPath), db);
    }
    return game.Readers().Get<SceneReader>()->Read(game.ExtractFile(pakPath), db);
}

static void PrefetchMaterial(const LoadedGame& game, const AssetOverrides* overrides, ScenePrefetch& prefetch,
                             const std::string& mdfPakPath) {
    prefetch.materials.Request(prefetch.pool, mdfPakPath, [&game, overrides, &prefetch, mdfPakPath] {
        MaterialData mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(mdfPakPath));
        ApplyMaterialOverrides(overrides, mdfPakPath, mdf);
        for (const MaterialEntry& mat : mdf.materials) {
            std::string sdfPath = ToPakPath(mat.masterMaterialPath, game.Game().GetMasterExt().c_str());
            prefetch.masters.Request(prefetch.pool, sdfPath, [&game, sdfPath] {
                return game.Readers().Get<SdfReader>()->Read(game.ExtractFile(sdfPath));
            });
            for (const MaterialTexture& tex : mat.textures) {
                std::string file = TextureFilePath(game, tex.path);
                prefetch.textures.Request(prefetch.pool, file, [&game, file] {
                    return game.Readers().Get<TexReader>()->Read(game.ExtractFile(file));
                }, true);
            }
        }
        return mdf;
    });
}

// Follows the build's walk (external scenes, day variants, mesh overrides) without its
// visibility rules; whatever it misses the build reads itself.
static void PrefetchScene(const LoadedGame& game, const RszTypeDatabase& db, const AssetOverrides* overrides,
                          ScenePrefetch& prefetch, const std::string& pakPath, int depth, const std::string& condition) {
    if (depth > MAX_SCENE_DEPTH) return;
    prefetch.scenes.Request(prefetch.pool, pakPath, [&game, &db, overrides, &prefetch, pakPath, depth, condition] {
        SceneData scn = ReadSceneOrPrefab(game, db, pakPath);
        std::function<void(std::size_t)> visit = [&](std::size_t nodeIndex) {
            const SceneNode& node = scn.nodes[nodeIndex];
            if (OtherLightCondition(node.name, condition)) return;
            for (int32_t componentIndex : node.componentIndices) {
                const RszInstance& component = scn.rsz.instances[componentIndex];
                if (node.kind != SceneNode::Kind::GameObject || component.typeName != "via.render.Mesh") continue;
                const RszValue* meshField = component.Field("v2");
                const RszValue* mdfField = component.Field("v3");
                if (meshField == nullptr || mdfField == nullptr) continue;
                std::string meshPath = meshField->AsString();
                std::string mdfPath = mdfField->AsString();
                if (meshPath.empty() || mdfPath.empty()) continue;
                std::string objectKey = SceneObjectKey(pakPath, nodeIndex);
                std::string meshPak = ToPakPath(meshPath, game.Game().GetMeshExt().c_str());
                std::string mdfPak = ToPakPath(mdfPath, game.Game().GetMdfExt().c_str());
                if (const std::string* o = FindOverride(overrides, ComponentFieldKey(objectKey, componentIndex, "v2"))) meshPak = *o;
                if (const std::string* o = FindOverride(overrides, ComponentFieldKey(objectKey, componentIndex, "v3"))) mdfPak = *o;
                prefetch.meshes.Request(prefetch.pool, meshPak, [&game, meshPak] {
                    return game.Readers().Get<MeshReader>()->Read(game.ExtractFile(meshPak));
                });
                PrefetchMaterial(game, overrides, prefetch, mdfPak);
            }
            for (std::size_t child : node.children) visit(child);
        };
        for (std::size_t root : scn.roots) visit(root);
        for (const RszInstance& instance : scn.rsz.instances) {
            if (instance.typeName != "via.physics.MeshShape") continue;
            const RszValue* pathField = instance.Field("v1");
            std::string path = pathField ? pathField->AsString() : std::string();
            if (path.empty()) continue;
            std::string mcolPak = ToPakPath(path, game.Game().GetMcolExt().c_str());
            prefetch.collisionMeshes.Request(prefetch.pool, mcolPak, [&game, mcolPak] { return ReadMcol(game, mcolPak); });
        }
        for (const std::string& external : scn.externalScenes) {
            if (OtherLightCondition(external, condition)) continue;
            PrefetchScene(game, db, overrides, prefetch, ToPakPath(external, game.Game().GetSceneExt().c_str()), depth + 1,
                          condition);
        }
        return scn;
    });
}

namespace {

struct EffectProviders {
    EffectProviderCache prefabs;
    std::map<std::string, int32_t> assets;  // efx pak path -> SceneBuild::effectAssets index, -1 = nothing to draw
    std::map<std::string, SceneMeshAsset> meshes;  // "efx pak path#emitter"
};

const RszInstance* Referenced(const SceneData& scn, const RszValue* value) {
    const RszObjectRef* ref = value ? value->As<RszObjectRef>() : nullptr;
    return ref ? scn.Instance(static_cast<int32_t>(ref->index)) : nullptr;
}

std::span<const RszValue> Elements(const RszValue* value) {
    const RszArray* array = value ? value->As<RszArray>() : nullptr;
    return array ? std::span<const RszValue>(*array) : std::span<const RszValue>();
}

std::string PrefabFieldPath(const LoadedGame& game, const SceneData& owner, const RszValue* prefabField) {
    const RszInstance* prefab = Referenced(owner, prefabField);
    const RszValue* path = prefab ? prefab->Field("v1") : nullptr;
    if (!path || path->AsString().empty()) return {};
    return ToPakPath(path->AsString(), game.Game().GetPrefabExt().c_str());
}

const SceneData* ProviderPrefab(const LoadedGame& game, const RszTypeDatabase& db, EffectProviderCache& cache,
                                const std::string& pakPath) {
    if (pakPath.empty()) return nullptr;
    auto it = cache.prefabs.find(pakPath);
    if (it == cache.prefabs.end()) {
        std::optional<SceneData> data;
        try {
            if (game.FindEntry(pakPath)) data = ReadSceneOrPrefab(game, db, pakPath);
        } catch (const std::exception& e) {
            LogWarning("effect provider %s: %s", pakPath.c_str(), e.what());
        }
        it = cache.prefabs.emplace(pakPath, std::move(data)).first;
    }
    return it->second ? &*it->second : nullptr;
}

int32_t SceneEffectAsset(const LoadedGame& game, SceneBuild& build, TextureCache& cache, EffectProviders& providers,
                         const std::string& pakPath) {
    auto it = providers.assets.find(pakPath);
    if (it != providers.assets.end()) return it->second;
    int32_t index = -1;
    try {
        if (game.FindEntry(pakPath)) {
            LoadedEffect fx = LoadEffect(game, cache, pakPath);
            if (std::any_of(fx.data.emitters.begin(), fx.data.emitters.end(),
                            [](const EfxEmitter& e) { return IsDrawableEmitter(e) || IsStaticMeshEmitter(e); })) {
                for (EffectEmitterAssets& assets : fx.assets) {
                    if (!assets.material) continue;
                    const std::string& key = fx.programKeys[assets.material->program];
                    auto known = std::find(build.materialProgramKeys.begin(), build.materialProgramKeys.end(), key);
                    if (known == build.materialProgramKeys.end()) {
                        build.materialPrograms.push_back(fx.programs[assets.material->program]);
                        build.materialProgramKeys.push_back(key);
                        known = build.materialProgramKeys.end() - 1;
                    }
                    assets.material->program = static_cast<uint32_t>(known - build.materialProgramKeys.begin());
                }
                fx.programs.clear();
                fx.programKeys.clear();
                index = static_cast<int32_t>(build.effectAssets.size());
                build.effectAssets.push_back(std::move(fx));
            }
        }
    } catch (const std::exception& e) {
        LogWarning("effect %s: %s", pakPath.c_str(), e.what());
    }
    providers.assets.emplace(pakPath, index);
    return index;
}

// The mdf with the TypeMesh parameters on top (TypeMesh::rewriteConstantBuffer); a float is x plus a random value
// centered on z (TypeMesh::updateParameters), taken at its center.
SceneMeshAsset AppendEffectMesh(const LoadedGame& game, SceneBuild& build, TextureCache& cache, const EfxTypeMesh& m) {
    std::string meshPak = ToPakPath(m.meshPath, game.Game().GetMeshExt().c_str());
    std::string mdfPak = ToPakPath(m.mdfPath, game.Game().GetMdfExt().c_str());
    MeshData mesh;
    MaterialData mdf;
    try {
        mesh = game.Readers().Get<MeshReader>()->Read(game.ExtractFile(meshPak));
        mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(mdfPak));
    } catch (const std::exception& e) {
        LogWarning("skip effect mesh %s: %s", meshPak.c_str(), e.what());
        return {};
    }
    for (MaterialEntry& entry : mdf.materials) {
        for (MaterialProperty& prop : entry.properties) {
            uint32_t hash = Murmur3::HashAscii(prop.name);
            for (const EfxMaterialParam& param : m.params) {
                if (param.nameHash != hash || prop.values.empty()) continue;
                if (param.type == 1) {
                    for (std::size_t c = 0; c < prop.values.size() && c < 4; c++) prop.values[c] = param.values[c];
                } else if (param.type == 2) {
                    prop.values[0] = param.values[0] + param.values[2];
                }
            }
        }
    }
    return AppendMeshData(game, build, cache, meshPak, mdfPak, false, mesh, std::move(mdf), nullptr);
}

Mat4 EffectElementTransform(const RszInstance& element) {
    RszVec3 offset = element.Field("Offset") ? element.Field("Offset")->AsVec3() : RszVec3{ 0, 0, 0 };
    RszVec3 degrees = element.Field("Rotation") ? element.Field("Rotation")->AsVec3() : RszVec3{ 0, 0, 0 };
    RszVec3 scale = element.Field("Scale") ? element.Field("Scale")->AsVec3() : RszVec3{ 1, 1, 1 };
    constexpr float DEG = 3.14159265f / 180.0f;
    float cx = std::cos(degrees.x * DEG), sx = std::sin(degrees.x * DEG);
    float cy = std::cos(degrees.y * DEG), sy = std::sin(degrees.y * DEG);
    float cz = std::cos(degrees.z * DEG), sz = std::sin(degrees.z * DEG);
    Mat4 rx{{ 1, 0, 0, 0,  0, cx, sx, 0,  0, -sx, cx, 0,  0, 0, 0, 1 }};
    Mat4 ry{{ cy, 0, -sy, 0,  0, 1, 0, 0,  sy, 0, cy, 0,  0, 0, 0, 1 }};
    Mat4 rz{{ cz, sz, 0, 0,  -sz, cz, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 }};
    Mat4 s{{ scale.x, 0, 0, 0,  0, scale.y, 0, 0,  0, 0, scale.z, 0,  0, 0, 0, 1 }};
    Mat4 m = Mul(s, Mul(Mul(rx, ry), rz));
    m.m[12] = offset.x;
    m.m[13] = offset.y;
    m.m[14] = offset.z;
    return m;
}

// An element instance naming .efx files in v0 (EPV*Data elements share the layout).
std::optional<ObjectEffectElement> ReadEffectElement(const LoadedGame& game, const RszInstance& instance,
                                                     const std::string& container, const std::string& provider) {
    std::span<const RszValue> paths = Elements(instance.Field("v0"));
    if (paths.empty() || !paths[0].As<std::string>()) return std::nullopt;
    ObjectEffectElement element;
    element.container = container;
    element.provider = provider;
    element.type = instance.typeName;
    element.triggerId = static_cast<uint32_t>(Number(instance.Field("TriggerId"), 4294967295.0));
    element.local = EffectElementTransform(instance);
    element.loopFrames = static_cast<float>(std::max(0.0, Number(instance.Field("LoopFrame"), 0)));
    for (const RszValue& path : paths) {
        if (!path.AsString().empty()) element.effects.push_back(ToPakPath(path.AsString(), game.Game().GetEfxExt().c_str()));
    }
    return element;
}

void PlaceEffects(const LoadedGame& game, const std::vector<ObjectEffectElement>& elements, const Mat4& world,
                  const std::string& owner, SceneBuild& build, TextureCache& cache, EffectProviders& providers) {
    for (const ObjectEffectElement& element : elements) {
        if (!element.autoPlay) continue;
        Mat4 effectWorld = Mul(element.local, world);
        for (const std::string& efx : element.effects) {
            int32_t asset = SceneEffectAsset(game, build, cache, providers, efx);
            if (asset < 0) continue;
            const EffectData& data = build.effectAssets[static_cast<std::size_t>(asset)].data;
            if (std::any_of(data.emitters.begin(), data.emitters.end(), IsDrawableEmitter)) {
                build.effects.push_back({ static_cast<uint32_t>(asset), effectWorld, owner, element.loopFrames });
            }
            for (std::size_t i = 0; i < data.emitters.size(); i++) {
                const EfxEmitter& emitter = data.emitters[i];
                if (!IsStaticMeshEmitter(emitter)) continue;
                std::string key = efx + '#' + std::to_string(i);
                auto mesh = providers.meshes.find(key);
                if (mesh == providers.meshes.end()) {
                    mesh = providers.meshes.emplace(key, AppendEffectMesh(game, build, cache, *emitter.mesh)).first;
                }
                if (!mesh->second.valid) continue;
                Mat4 meshWorld = emitter.transform ? Mul(EmitterLocalTransform(*emitter.transform), effectWorld) : effectWorld;
                AddSceneInstance(build, mesh->second, meshWorld, owner);
            }
        }
    }
}

}

std::vector<ObjectEffectElement> ReadObjectEffects(const LoadedGame& game, const RszTypeDatabase& db, const SceneData& scn,
                                                   const RszInstance& manager, EffectProviderCache& cache) {
    std::vector<std::string> containers{ PrefabFieldPath(game, scn, manager.Field("DataContainer")) };
    for (const RszValue& external : Elements(manager.Field("ExternalDataContainers"))) {
        containers.push_back(PrefabFieldPath(game, scn, &external));
    }
    std::vector<ObjectEffectElement> out;
    for (const std::string& containerPath : containers) {
        const SceneData* epvc = ProviderPrefab(game, db, cache, containerPath);
        if (!epvc) continue;
        for (const RszInstance& container : epvc->rsz.instances) {
            if (container.typeName != "via.effect.script.EPVDataContainer") continue;
            for (const RszValue& settingRef : Elements(container.Field("ExpertData"))) {
                const RszInstance* setting = Referenced(*epvc, &settingRef);
                std::string providerPath = setting ? PrefabFieldPath(game, *epvc, setting->Field("Data")) : std::string();
                const SceneData* epve = ProviderPrefab(game, db, cache, providerPath);
                if (!epve) continue;
                for (const RszInstance& instance : epve->rsz.instances) {
                    std::optional<ObjectEffectElement> element = ReadEffectElement(game, instance, containerPath, providerPath);
                    if (!element) continue;
                    element->autoPlay = instance.typeName == "via.effect.script.EPVExpertAutoData.AutoDataElement" &&
                                        element->triggerId == 0xFFFFFFFFu;
                    out.push_back(std::move(*element));
                }
            }
        }
    }
    return out;
}

std::vector<ObjectEffectElement> ReadEnvironmentEffects(const LoadedGame& game, const SceneData& scn, const SceneNode& node) {
    std::vector<ObjectEffectElement> out;
    for (int32_t componentIndex : node.componentIndices) {
        const RszInstance* data = scn.Instance(componentIndex);
        if (!data || data->typeName.find("effect.script.EPV") == std::string::npos) continue;
        for (const RszValue& ref : Elements(data->Field("Elements"))) {
            const RszInstance* instance = Referenced(scn, &ref);
            std::optional<ObjectEffectElement> element = instance ? ReadEffectElement(game, *instance, {}, {}) : std::nullopt;
            if (!element) continue;
            element->autoPlay = element->triggerId == 0xFFFFFFFFu;
            out.push_back(std::move(*element));
        }
    }
    return out;
}

void BuildSceneFromScn(const LoadedGame& game, const RszTypeDatabase& db,
                              const std::string& rootScnPath, SceneBuild& build, TextureCache& cache,
                              float lightScale) {
    bool wasDeferred = cache.deferred;
    cache.deferred = true;
    std::map<std::pair<std::string, std::string>, SceneMeshAsset> assets;
    std::set<std::string> visited;
    std::vector<std::unique_ptr<SceneLodRule>> lodRules = MakeSceneLodRules();
    MeshLodSettings meshLodSettings;
    try {
        meshLodSettings = game.Readers().Get<MeshLodSettingsReader>()->Read(game.ExtractFile(MESH_LOD_SETTINGS));
    } catch (const std::exception& e) {
        LogWarning("mesh LOD settings: %s", e.what());
    }

    const std::string* requestedLightState = FindOverride(build, LIGHT_STATE_OVERRIDE);
    // app.LightConditionManager starts at KindEnum 0, Day (chapter 1's outside has only Day lights).
    std::string lightCondition;
    if (game.Game().GetId() == "re7") {
        lightCondition = requestedLightState ? LightCondition(*requestedLightState) : std::string();
        if (lightCondition.empty()) lightCondition = "Day";
        build.lightStates = { "Day", "Night", "Midnight" };
        build.lightState = lightCondition;
    }
    auto loads = std::make_unique<ScenePrefetch>();
    ScenePrefetch& prefetch = *loads;
    PrefetchScene(game, db, build.overrides, prefetch, rootScnPath, 0, lightCondition);
    uint32_t setupLightState = 0;
    // app.PostProcessWeatherSetting names its zone's GameObject; the zone with the largest
    // _Priority value is the area-wide one the others override.
    struct WeatherSetting {
        RszGuid zone;
        std::string toneMapList;
        std::string fogList;
        std::string ssaoList;
        std::string volumetricFogList;
        std::string colorCorrectList;
        std::string softBloomList;
    };
    std::vector<WeatherSetting> weatherSettings;
    std::map<std::array<uint8_t, 16>, double> zonePriorities;
    struct ZoneSource {
        std::array<uint8_t, 16> guid{};
        WeatherSetting lists;
    };
    std::vector<ZoneSource> zoneSources;
    EffectProviders effectProviders;
    // An EnvironmentEffectManager without IsAutoPlay plays while the player is inside an EffectEmitZoneGroup (active on
    // start) that targets it; the others (the village fires of the chapter 1 attack...) are started by events.
    struct PendingEnvironmentEffects {
        std::array<uint8_t, 16> guid;
        std::vector<ObjectEffectElement> elements;
        Mat4 world;
        std::string owner;
    };
    std::vector<PendingEnvironmentEffects> pendingEnvironmentEffects;
    std::map<std::array<uint8_t, 16>, std::vector<uint32_t>> zoneEffectTargets;

    std::function<void(const std::string&, bool, int, const std::vector<SceneLodRole>&, bool)> loadScene =
        [&](const std::string& pakPath, bool withMeshes, int depth, const std::vector<SceneLodRole>& roles, bool hiddenScene) {
        if (depth > MAX_SCENE_DEPTH || !visited.insert(pakPath).second) return;

        SceneData scn;
        try {
            std::shared_ptr<SceneData> fetched = prefetch.scenes.Take(pakPath);
            scn = fetched ? std::move(*fetched) : ReadSceneOrPrefab(game, db, pakPath);
        } catch (const std::exception& e) {
            LogWarning("skip scene %s: %s", pakPath.c_str(), e.what());
            return;
        }

        for (const auto& rule : lodRules) rule->Scene(pakPath, scn);
        for (const SceneNode& node : scn.nodes) {
            const RszInstance* setup = FindComponent(scn, node, "app.LightSceneSetupParam");
            const RszValue* state = setup ? setup->Field("state") : nullptr;
            if (!state) continue;
            if (const int64_t* i = state->As<int64_t>()) setupLightState = static_cast<uint32_t>(*i);
            else if (const uint64_t* u = state->As<uint64_t>()) setupLightState = static_cast<uint32_t>(*u);
        }
        std::set<std::string> unselectedStates = UnselectedLightStates(scn, setupLightState, requestedLightState, build);
        std::vector<uint8_t> hiddenNodes = HiddenByDefault(scn);
        std::map<std::string, bool> hiddenReferences;
        std::map<int32_t, RszGuid> guids;
        for (const SceneGameObjectInfo& info : scn.gameObjects) guids[info.id] = info.guid;
        int32_t parentObject = -1;
        std::function<void(std::size_t, const Mat4&, bool)> walk = [&](std::size_t nodeIndex, const Mat4& parentWorld, bool parentHidden) {
            const SceneNode& node = scn.nodes[nodeIndex];
            if (OtherLightCondition(node.name, lightCondition)) return;
            if (!node.scenePath.empty() && unselectedStates.count(node.scenePath)) return;
            Mat4 world = parentWorld;
            bool hidden = parentHidden || hiddenNodes[nodeIndex];
            if (!node.scenePath.empty()) hiddenReferences[node.scenePath] = hidden;
            int32_t enclosingObject = parentObject;

            if (node.kind == SceneNode::Kind::GameObject) {
                std::string objectKey = SceneObjectKey(pakPath, nodeIndex);
                if (hidden) build.hiddenObjects.push_back(objectKey);
                float t[3] = { node.position.x, node.position.y, node.position.z };
                float q[4] = { node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w };
                float s[3] = { node.scale.x, node.scale.y, node.scale.z };
                TransformOverride(build.overrides, objectKey, "position", t);
                TransformOverride(build.overrides, objectKey, "scale", s);
                if (float euler[3]; TransformOverride(build.overrides, objectKey, "rotation", euler)) EulerDegreesToQuat(euler, q);
                world = Mul(ComposeTRS(t, q, s), parentWorld);
                build.objects.push_back({ objectKey, enclosingObject, world });
                parentObject = static_cast<int32_t>(build.objects.size() - 1);

                for (int32_t componentIndex : node.componentIndices) {
                    const RszInstance& component = scn.rsz.instances[componentIndex];

                    if (std::optional<ScenePunctualLight> light = ReadPunctualLight(component)) {
                        // Hidden ones too: the viewport switches lights with their GameObject's visibility.
                        std::size_t lightsBefore = build.lights.size();
                        AddSceneLight(build.lights, build.lightScattering, *light, world, lightScale);
                        SceneLightMarker marker;
                        if (build.lights.size() > lightsBefore) marker.lightIndex = static_cast<int32_t>(lightsBefore);
                        marker.owner = SceneObjectKey(pakPath, nodeIndex);
                        marker.kind = light->spot ? SceneLightKind::Spot : SceneLightKind::Point;
                        marker.enabled = light->common.enabled;
                        float dir[3] = { -world.m[8], -world.m[9], -world.m[10] };
                        float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                        for (int c = 0; c < 3; c++) {
                            marker.position[c] = world.m[12 + c];
                            marker.direction[c] = len > 1e-6f ? dir[c] / len : c == 1 ? -1.0f : 0.0f;
                            marker.color[c] = light->common.color[c];
                        }
                        marker.range = light->EffectiveRange();
                        marker.cone = light->cone;
                        marker.spread = light->spread;
                        build.lightMarkers.push_back(std::move(marker));
                        continue;
                    }
                    if (std::optional<SceneDirectionalLight> light = ReadDirectionalLight(component)) {
                        float local[3] = { light->direction[0], light->direction[1], light->direction[2] };
                        for (int c = 0; c < 3; c++) {
                            light->direction[c] = local[0] * world.m[c] + local[1] * world.m[4 + c] + local[2] * world.m[8 + c];
                        }
                        if (!hidden && light->common.enabled) {
                            if (build.directionalLights.empty()) build.directionalOwner = SceneObjectKey(pakPath, nodeIndex);
                            build.directionalLights.push_back(*light);
                        }
                        SceneLightMarker marker;
                        marker.owner = SceneObjectKey(pakPath, nodeIndex);
                        marker.kind = SceneLightKind::Directional;
                        marker.enabled = light->common.enabled;
                        float len = std::sqrt(light->direction[0] * light->direction[0] + light->direction[1] * light->direction[1] +
                                              light->direction[2] * light->direction[2]);
                        for (int c = 0; c < 3; c++) {
                            marker.position[c] = world.m[12 + c];
                            marker.direction[c] = len > 1e-6f ? light->direction[c] / len : c == 1 ? 1.0f : 0.0f;
                            marker.color[c] = light->common.color[c];
                        }
                        build.lightMarkers.push_back(std::move(marker));
                        continue;
                    }
                    if (std::optional<SceneIbl> ibl = ReadIbl(component)) {
                        if (!hidden && ibl->enabled) build.ibls.push_back(*ibl);
                        continue;
                    }
                    if (std::optional<SceneLightProbes> probes = ReadLightProbes(component)) {
                        if (!hidden && probes->enabled && !probes->probes.empty() && !probes->network.empty()) {
                            build.lightProbes.push_back(*probes);
                        }
                        continue;
                    }
                    if (std::optional<SceneVolumetricFog> fog = ReadVolumetricFog(component)) {
                        std::copy_n(world.m, 16, fog->world);
                        if (!hidden && fog->enabled) build.volumetricFogs.push_back(*fog);
                        continue;
                    }
                    if (std::optional<SceneLocalCubemap> cubemap = ReadLocalCubemap(component)) {
                        std::copy_n(world.m + 12, 3, cubemap->position);
                        if (!hidden && cubemap->enabled) build.localCubemaps.push_back(*cubemap);
                        continue;
                    }
                    if (component.typeName == "app.PostProcessZoneControl") {
                        auto guid = guids.find(node.id);
                        if (guid != guids.end()) zonePriorities[guid->second.bytes] = Number(component.Field("_Priority"), 0);
                        if (hidden || guid == guids.end()) continue;
                        ScenePostProcessZone zone;
                        zone.name = node.name;
                        zone.priority = static_cast<int32_t>(Number(component.Field("_Priority"), 0));
                        for (int32_t other : node.componentIndices) {
                            const RszInstance& colliders = scn.rsz.instances[other];
                            if (colliders.typeName != "via.physics.Colliders") continue;
                            std::vector<CollisionVolume> volumes = ColliderVolumes(scn.rsz, colliders, world);
                            zone.volumes.insert(zone.volumes.end(), volumes.begin(), volumes.end());
                        }
                        WeatherSetting lists{ guid->second, UserDataPath(scn.rsz, component.Field("_ToneMapUserDataList")),
                                              UserDataPath(scn.rsz, component.Field("_FogUserDataList")),
                                              UserDataPath(scn.rsz, component.Field("_SSAOUserDataList")),
                                              UserDataPath(scn.rsz, component.Field("_VolumetricFogUserDataList")),
                                              UserDataPath(scn.rsz, component.Field("_ColorCorrectUserDataList")),
                                              UserDataPath(scn.rsz, component.Field("_SoftBloomUserDataList")) };
                        zoneSources.push_back({ guid->second.bytes, lists });
                        build.postProcessZones.push_back(std::move(zone));
                        continue;
                    }
                    // RE7 zones: a Colliders volume with app.ChangeToneMapParam (and the other Change*Param) on it.
                    if (component.typeName == "app.ChangeToneMapParam") {
                        if (hidden) continue;
                        auto child = [&](const RszInstance& owner, std::string_view field) -> const RszInstance* {
                            const RszValue* value = owner.Field(field);
                            const RszObjectRef* ref = value ? value->As<RszObjectRef>() : nullptr;
                            return ref && ref->index < scn.rsz.instances.size() ? &scn.rsz.instances[ref->index] : nullptr;
                        };
                        ScenePostProcessZone zone;
                        zone.name = node.name;
                        for (int32_t other : node.componentIndices) {
                            const RszInstance& colliders = scn.rsz.instances[other];
                            if (colliders.typeName != "via.physics.Colliders") continue;
                            std::vector<CollisionVolume> volumes = ColliderVolumes(scn.rsz, colliders, world);
                            zone.volumes.insert(zone.volumes.end(), volumes.begin(), volumes.end());
                        }
                        if (const RszInstance* target = child(component, "TargetData")) {
                            zone.toneMap.found = true;
                            zone.toneMap.ev = static_cast<float>(Number(target->Field("EV"), 0));
                            zone.toneMap.animationTime = static_cast<float>(Number(target->Field("AnimationTime"), 0));
                        }
                        if (const RszInstance* once = child(component, "OneTimeSetData")) {
                            zone.toneMap.autoExposure = Number(once->Field("AutoExposure"), 1) == 0;
                            zone.toneMap.minWhitePoint = static_cast<float>(Number(once->Field("MinWhitePoint"), 1));
                            zone.toneMap.maxWhitePoint = static_cast<float>(Number(once->Field("MaxWhitePoint"), 1));
                            zone.toneMap.whiteRange = static_cast<float>(Number(once->Field("WhiteRange"), 0.8));
                        }
                        if (zone.toneMap.found && !zone.volumes.empty()) {
                            if (!build.toneMap.found) build.toneMap = zone.toneMap;
                            build.postProcessZones.push_back(std::move(zone));
                        }
                        continue;
                    }
                    if (component.typeName == "app.PostProcessWeatherSetting") {
                        std::optional<RszGuid> zone = GuidValue(component.Field("_ZoneControlGameObject"));
                        std::string list = UserDataPath(scn.rsz, component.Field("_ToneMapUserDataList"));
                        std::string fog = UserDataPath(scn.rsz, component.Field("_FogUserDataList"));
                        std::string ssao = UserDataPath(scn.rsz, component.Field("_SSAOUserDataList"));
                        std::string volumetric = UserDataPath(scn.rsz, component.Field("_VolumetricFogUserDataList"));
                        std::string colorCorrect = UserDataPath(scn.rsz, component.Field("_ColorCorrectUserDataList"));
                        std::string softBloom = UserDataPath(scn.rsz, component.Field("_SoftBloomUserDataList"));
                        if (!hidden && zone && !list.empty()) {
                            weatherSettings.push_back({ *zone, list, fog, ssao, volumetric, colorCorrect, softBloom });
                        }
                        continue;
                    }
                    if (component.typeName == "via.timeline.Timeline") {
                        if (!hidden) ReadStartTimeline(game, component, build);
                        continue;
                    }
                    if (component.typeName == "via.physics.Colliders") {
                        AppendColliders(game, scn.rsz, component, world, SceneObjectKey(pakPath, nodeIndex), build.collision,
                                        &prefetch.collisionMeshes);
                        continue;
                    }
                    if (component.typeName == "via.navigation.AIMap") {
                        AppendAiMaps(game, scn.rsz, component, SceneObjectKey(pakPath, nodeIndex), build.aiMaps);
                        continue;
                    }
                    if (component.typeName == "via.effect.script.ObjectEffectManager") {
                        if (withMeshes) {
                            PlaceEffects(game, ReadObjectEffects(game, db, scn, component, effectProviders.prefabs), world,
                                         SceneObjectKey(pakPath, nodeIndex), build, cache, effectProviders);
                        }
                        continue;
                    }
                    if (component.typeName == "via.effect.script.EnvironmentEffectManager") {
                        if (!withMeshes) continue;
                        std::vector<ObjectEffectElement> elements = ReadEnvironmentEffects(game, scn, node);
                        auto guid = guids.find(node.id);
                        if (Number(component.Field("IsAutoPlay"), 0) != 0 || guid == guids.end()) {
                            PlaceEffects(game, elements, world, SceneObjectKey(pakPath, nodeIndex), build, cache, effectProviders);
                        } else {
                            pendingEnvironmentEffects.push_back({ guid->second.bytes, std::move(elements), world, SceneObjectKey(pakPath, nodeIndex) });
                        }
                        continue;
                    }
                    if (component.typeName == "via.effect.script.EffectEmitZoneGroup") {
                        if (hidden || Number(component.Field("IsActiveOnStart"), 1) == 0) continue;
                        SceneEffectZone zone;
                        zone.name = node.name;
                        for (int32_t other : node.componentIndices) {
                            const RszInstance& colliders = scn.rsz.instances[other];
                            if (colliders.typeName != "via.physics.Colliders") continue;
                            std::vector<CollisionVolume> volumes = ColliderVolumes(scn.rsz, colliders, world);
                            zone.volumes.insert(zone.volumes.end(), volumes.begin(), volumes.end());
                        }
                        if (zone.volumes.empty()) continue;
                        uint32_t zoneIndex = static_cast<uint32_t>(build.effectZones.size());
                        build.effectZones.push_back(std::move(zone));
                        for (const RszValue& target : Elements(component.Field("TargetObjects"))) {
                            std::optional<RszGuid> guid = GuidValue(&target);
                            if (guid) zoneEffectTargets[guid->bytes].push_back(zoneIndex);
                        }
                        continue;
                    }

                    if (!withMeshes || component.typeName != "via.render.Mesh") continue;
                    const RszValue* meshField = component.Field("v2");
                    const RszValue* mdfField = component.Field("v3");
                    if (meshField == nullptr || mdfField == nullptr) continue;
                    std::string meshPath = meshField->AsString();
                    std::string mdfPath = mdfField->AsString();
                    if (meshPath.empty() || mdfPath.empty()) continue;

                    std::string objectKey = SceneObjectKey(pakPath, nodeIndex);
                    std::string meshPak = ToPakPath(meshPath, game.Game().GetMeshExt().c_str());
                    std::string mdfPak = ToPakPath(mdfPath, game.Game().GetMdfExt().c_str());
                    if (const std::string* o = FindOverride(build, ComponentFieldKey(objectKey, componentIndex, "v2"))) meshPak = *o;
                    if (const std::string* o = FindOverride(build, ComponentFieldKey(objectKey, componentIndex, "v3"))) mdfPak = *o;

                    auto key = std::make_pair(meshPak, mdfPak);
                    auto it = assets.find(key);
                    if (it == assets.end()) {
                        it = assets.emplace(key, AppendPrefetchedMesh(game, build, cache, meshPak, mdfPak, prefetch)).first;
                        if (it->second.valid) {
                            LogInfo("[mesh] %s (draws=%zu)", meshPak.c_str(), it->second.draws.size());
                        }
                    }
                    if (!it->second.valid) continue;
                    uint32_t instance = static_cast<uint32_t>(build.worlds.size());
                    float lo[3] = { 1e9f, 1e9f, 1e9f };
                    float hi[3] = { -1e9f, -1e9f, -1e9f };
                    ExpandWorldBounds(it->second, world, lo, hi);
                    std::size_t firstDraw = build.draws.size();
                    AddSceneInstance(build, it->second, world, objectKey);
                    // via.render.Mesh DrawDefault (RE8 v6, RE7 v8): off for the meshes that only cast shadows.
                    const RszValue* drawDefault = component.Field(game.Game().GetId() == "re7" ? "v8" : "v6");
                    const RszBytes* drawBytes = drawDefault ? drawDefault->As<RszBytes>() : nullptr;
                    if (drawBytes && drawBytes->size() == 1 && (*drawBytes)[0] == 0) {
                        for (std::size_t d = firstDraw; d < build.draws.size(); d++) build.draws[d].shadowOnly = true;
                    }
                    SceneMeshLod meshLod{ instance, { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f } };
                    if (MeshLodThresholds(meshLodSettings, it->second, meshLod)) build.meshLods.push_back(meshLod);
                    for (const SceneLodRole& role : roles) {
                        build.lods.AddMember(static_cast<uint32_t>(role.group), instance, role.low, lo, hi);
                    }
                    for (const auto& rule : lodRules) rule->Object(scn, node, instance, lo, hi);
                }
            }

            for (std::size_t child : node.children) walk(child, world, hidden);
            parentObject = enclosingObject;
        };

        for (std::size_t root : scn.roots) walk(root, Identity(), hiddenScene);

        for (const std::string& external : scn.externalScenes) {
            if (OtherLightCondition(external, lightCondition) || unselectedStates.count(external)) continue;
            std::vector<SceneLodRole> referenceRoles = roles;
            for (const auto& rule : lodRules) {
                SceneLodRole claimed = rule->ReferencedScene(pakPath, scn, external, build.lods);
                if (claimed.group >= 0) referenceRoles.push_back(claimed);
            }
            auto hiddenReference = hiddenReferences.find(external);
            bool hidden = hiddenReference != hiddenReferences.end() ? hiddenReference->second : hiddenScene;
            loadScene(ToPakPath(external, game.Game().GetSceneExt().c_str()), withMeshes, depth + 1, referenceRoles, hidden);
        }
    };

    loadScene(rootScnPath, true, 0, {}, false);
    for (const PendingEnvironmentEffects& pending : pendingEnvironmentEffects) {
        auto zones = zoneEffectTargets.find(pending.guid);
        if (zones == zoneEffectTargets.end()) continue;
        std::size_t firstEffect = build.effects.size();
        uint32_t firstInstance = static_cast<uint32_t>(build.worlds.size());
        PlaceEffects(game, pending.elements, pending.world, pending.owner, build, cache, effectProviders);
        for (std::size_t e = firstEffect; e < build.effects.size(); e++) build.effects[e].zones = zones->second;
        for (uint32_t i = firstInstance; i < build.worlds.size(); i++) build.zoneInstances.push_back({ i, zones->second });
    }
    SceneFiles files;
    files.scene = [&](const std::string& pakPath) -> std::optional<SceneData> {
        if (!game.FindEntry(pakPath)) return std::nullopt;
        try {
            return ReadSceneOrPrefab(game, db, pakPath);
        } catch (const std::exception& e) {
            LogWarning("skip scene %s: %s", pakPath.c_str(), e.what());
            return std::nullopt;
        }
    };
    files.user = [&](const std::string& pakPath) -> std::optional<UserFileData> {
        if (!game.FindEntry(pakPath)) return std::nullopt;
        try {
            return game.Readers().Get<UserReader>()->Read(game.ExtractFile(pakPath), db);
        } catch (const std::exception& e) {
            LogWarning("skip user file %s: %s", pakPath.c_str(), e.what());
            return std::nullopt;
        }
    };
    for (const auto& rule : lodRules) rule->Finish(build.lods, files);

    const WeatherSetting* global = weatherSettings.empty() ? nullptr : &weatherSettings.front();
    double globalPriority = -1e300;
    for (const WeatherSetting& setting : weatherSettings) {
        auto priority = zonePriorities.find(setting.zone.bytes);
        if (priority != zonePriorities.end() && priority->second > globalPriority) {
            global = &setting;
            globalPriority = priority->second;
        }
    }
    if (global) build.toneMap = ReadToneMap(files, global->toneMapList);
    if (global && !global->fogList.empty()) build.fogZone = ReadFog(files, global->fogList);
    if (global && !global->volumetricFogList.empty()) build.volumetricFogZone = ReadVolumetricFogZone(files, global->volumetricFogList);
    if (global && !global->ssaoList.empty()) ReadSsao(files, global->ssaoList, build.ssao);
    if (global && !global->colorCorrectList.empty()) build.colorCorrectZone = ReadColorCorrectZone(files, global->colorCorrectList);
    if (global && !global->softBloomList.empty()) build.softBloomZone = ReadSoftBloomZone(files, global->softBloomList);
    std::map<std::string, SceneColorCorrectZone> colorCorrects;
    std::map<std::string, SceneSoftBloomZone> softBlooms;
    // PostProcessWeatherSetting::update hands the zone its lists; the one loaded last stands for the one updated last.
    std::map<std::string, SceneToneMap> toneMaps;
    std::map<std::string, SceneFog> fogs;
    std::map<std::string, SceneVolumetricFogZone> volumetricFogs;
    std::map<std::string, SceneSsao> ssaos;
    for (std::size_t z = 0; z < zoneSources.size(); z++) {
        WeatherSetting lists = zoneSources[z].lists;
        for (const WeatherSetting& setting : weatherSettings) {
            if (setting.zone.bytes == zoneSources[z].guid) lists = setting;
        }
        ScenePostProcessZone& zone = build.postProcessZones[z];
        if (!lists.toneMapList.empty()) {
            if (!toneMaps.count(lists.toneMapList)) toneMaps[lists.toneMapList] = ReadToneMap(files, lists.toneMapList);
            zone.toneMap = toneMaps[lists.toneMapList];
        }
        if (!lists.fogList.empty()) {
            if (!fogs.count(lists.fogList)) fogs[lists.fogList] = ReadFog(files, lists.fogList);
            zone.fog = fogs[lists.fogList];
        }
        if (!lists.volumetricFogList.empty()) {
            if (!volumetricFogs.count(lists.volumetricFogList)) {
                volumetricFogs[lists.volumetricFogList] = ReadVolumetricFogZone(files, lists.volumetricFogList);
            }
            zone.volumetricFog = volumetricFogs[lists.volumetricFogList];
        }
        if (!lists.ssaoList.empty()) {
            if (!ssaos.count(lists.ssaoList)) ReadSsao(files, lists.ssaoList, ssaos[lists.ssaoList]);
            zone.ssao = ssaos[lists.ssaoList];
        }
        if (!lists.colorCorrectList.empty()) {
            if (!colorCorrects.count(lists.colorCorrectList)) {
                colorCorrects[lists.colorCorrectList] = ReadColorCorrectZone(files, lists.colorCorrectList);
            }
            zone.colorCorrect = colorCorrects[lists.colorCorrectList];
        }
        if (!lists.softBloomList.empty()) {
            if (!softBlooms.count(lists.softBloomList)) softBlooms[lists.softBloomList] = ReadSoftBloomZone(files, lists.softBloomList);
            zone.softBloom = softBlooms[lists.softBloomList];
        }
    }
    ReadApplicationCamera(game, db, build, lightScale);
    ResolveTextures(game, cache, &prefetch.textures);
    for (LoadedEffect& fx : build.effectAssets) ResolveEffectTextureFlags(fx, cache);
    LogInfo("scene build: %zu instances, %zu draws, %zu meshLods, %zu lodGroups",
            build.worlds.size(), build.draws.size(), build.meshLods.size(), build.lods.groups.size());
    cache.deferred = wasDeferred;
    // Freeing the leftover loads here would take as long as everything after the walk.
    prefetch.pool.Stop();
    std::thread([leftover = std::move(loads)] {}).detach();
}

TextureCache::TextureCache() {
    storage.push_back(MakeFallbackTexture());
    descs.push_back(ToGameTexture(storage.back()));
    byPath.emplace("", 0);
}

std::string FindMeshMaterial(const LoadedGame& game, const std::string& meshPath) {
    std::string mdfSuffix = ".mdf2" + game.Game().GetMdfExt();
    std::string meshBase = meshPath.substr(0, meshPath.rfind(".mesh"));
    if (game.FindEntry(meshBase + mdfSuffix)) return meshBase + mdfSuffix;
    return FindPath(game, meshPath.substr(0, meshPath.find_last_of('/') + 1), mdfSuffix);
}

GamePrepassDesc MakePrepassDesc(const SceneBuild& build) {
    if (build.masters.prepass == nullptr) throw std::runtime_error("ZPrePassStatic not found");
    GamePrepassDesc desc{ build.masters.prepass->vs, BuildInputLayout(*build.masters.prepass) };
    AppendBindOverrides(*build.masters.prepass, desc.bindOverrides);
    return desc;
}

ViewerMesh MakeViewerMesh(const SceneBuild& build) {
    ViewerMesh vm{};
    vm.positions = build.positions;
    vm.normals = build.normals;
    vm.uv0 = build.uv0;
    vm.uv1 = build.uv1;
    if (!build.skinned.empty()) vm.weights = build.weights;
    vm.indices = build.indices;
    vm.draws = build.draws;
    std::stable_sort(vm.draws.begin(), vm.draws.end(), [](const ViewerMeshDraw& a, const ViewerMeshDraw& b) {
        return a.pipelineIndex < b.pipelineIndex;
    });
    std::memcpy(vm.aabbMin, build.aabbMin, sizeof(vm.aabbMin));
    std::memcpy(vm.aabbMax, build.aabbMax, sizeof(vm.aabbMax));
    return vm;
}
