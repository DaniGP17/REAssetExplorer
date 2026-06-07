#include "REAssetNative.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include "Core/Assets/Readers/AimpReader.h"
#include "Core/Assets/Readers/McolReader.h"
#include "Core/Assets/Readers/MotbankReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/Readers/UvsReader.h"
#include "Core/Assets/FontDecode.h"
#include "Core/Assets/TextureDecode.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Explorer/AssetOutline.h"
#include "Explorer/CharacterIndex.h"
#include "Explorer/EffectParams.h"
#include "Explorer/GameFactory.h"
#include "Explorer/GuiFonts.h"
#include "Explorer/Log.h"
#include "Explorer/MessageIndex.h"
#include "Explorer/SceneBuilder.h"
#include "Explorer/SceneSave.h"
#include "Explorer/WemLocator.h"
#include "NativeAudio.h"
#include "NativeMovie.h"
#include "NativeThumbnails.h"
#include "NativeViewport.h"

struct RaeGame {
    RaeGame(LoadedGame loaded, std::filesystem::path assets)
        : game(std::move(loaded)), assetsDir(std::move(assets)), name(game.Game().GetName()) {}

    const RszTypeDatabase& Rsz() {
        std::lock_guard lock(rszMutex);
        if (!rsz) {
            auto db = std::make_unique<RszTypeDatabase>();
            db->LoadFromFile((assetsDir / "Rsz" / game.Game().GetRszFile()).string());
            rsz = std::move(db);
        }
        return *rsz;
    }

    LoadedGame game;
    std::filesystem::path assetsDir;
    std::string name;
    std::once_flag fileListOnce;
    std::string fileList;
    std::mutex rszMutex;
    std::unique_ptr<RszTypeDatabase> rsz;

    ThumbnailRenderer& Thumbnails() {
        std::lock_guard lock(thumbnailsMutex);
        if (!thumbnails) thumbnails = std::make_unique<ThumbnailRenderer>();
        return *thumbnails;
    }

    const WwiseCodebooks& Codebooks() {
        std::lock_guard lock(audioMutex);
        if (!codebooks) {
            std::ifstream file(assetsDir / "Wwise" / "packed_codebooks_aoTuV_603.bin", std::ios::binary);
            if (!file) throw std::runtime_error("Assets/Wwise/packed_codebooks_aoTuV_603.bin is missing");
            codebooks = std::make_unique<WwiseCodebooks>(
                std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
        }
        return *codebooks;
    }

    const CharacterIndex& Characters() {
        std::lock_guard lock(charactersMutex);
        if (!characters) {
            std::filesystem::path cache;
            if (const char* local = std::getenv("LOCALAPPDATA")) {
                cache = std::filesystem::path(local) / "REAssetExplorer" / "Index" / game.Game().GetId() / "characters.txt";
            }
            characters = std::make_unique<CharacterIndex>(CharacterIndex::Load(game, Rsz(), cache));
        }
        return *characters;
    }

    std::mutex charactersMutex;
    std::unique_ptr<CharacterIndex> characters;

    const MessageIndex& Messages() {
        std::lock_guard lock(messagesMutex);
        if (!messages) messages = std::make_unique<MessageIndex>(game);
        return *messages;
    }

    std::mutex messagesMutex;
    std::unique_ptr<MessageIndex> messages;

    WemLocator& Wems() {
        std::lock_guard lock(audioMutex);
        if (!wems) wems = std::make_unique<WemLocator>(game);
        return *wems;
    }

    DecodedAudio DecodeMedia(const char* container, uint32_t mediaId) {
        std::vector<uint8_t> wem = Wems().Read(container, mediaId);
        return DecodeWem(wem, Codebooks());
    }

    std::mutex audioMutex;
    std::unique_ptr<WwiseCodebooks> codebooks;
    std::unique_ptr<WemLocator> wems;
    std::shared_ptr<const TextureData> Sky(std::string path) {
        if (path.empty()) path = game.Game().GetDefaultSkyPath();
        std::lock_guard lock(skyMutex);
        std::shared_ptr<const TextureData>& sky = skies[path];
        if (!sky) sky = std::make_shared<const TextureData>(game.Readers().Get<TexReader>()->Read(game.ExtractFile(path)));
        return sky;
    }

    std::mutex skyMutex;
    std::unordered_map<std::string, std::shared_ptr<const TextureData>> skies;
    std::once_flag skyListOnce;
    std::string skyList;
    std::mutex thumbnailsMutex;
    // Last member: destroyed (and its thread joined) before the game it reads.
    std::unique_ptr<ThumbnailRenderer> thumbnails;
};

struct RaeViewport {
    explicit RaeViewport(HWND parent) : viewport(parent) {}
    NativeViewport viewport;
};

namespace {

thread_local std::string t_lastError;

// Released by rae_audio_shutdown: a static destructor would join miniaudio's thread under the loader lock.
std::mutex g_playerMutex;
std::unique_ptr<AudioPlayer> g_player;

AudioPlayer& Player() {
    std::lock_guard lock(g_playerMutex);
    if (!g_player) g_player = std::make_unique<AudioPlayer>();
    return *g_player;
}
thread_local std::string t_outline;
std::atomic<RaeLogCallback> g_logCallback{ nullptr };

void ForwardLog(LogLevel level, const char* message) {
    if (RaeLogCallback callback = g_logCallback.load()) callback(static_cast<int32_t>(level), message);
}

// logErrors false where failures are routine (missing previews).
template <typename F>
int32_t Guard(F&& body, bool logErrors = true) {
    try {
        body();
        t_lastError.clear();
        return 1;
    } catch (const std::exception& e) {
        t_lastError = e.what();
    } catch (...) {
        t_lastError = "unknown error";
    }
    if (logErrors) LogError("%s", t_lastError.c_str());
    return 0;
}

std::filesystem::path Utf8Path(const char* text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

SceneLook DefaultLook(const LoadedGame& game) {
    SceneLook look;
    look.skyPath = game.Game().GetDefaultSkyPath();
    return look;
}

AssetOverrides ParseOverrides(const char* text) {
    AssetOverrides overrides;
    std::string_view rest = text ? text : "";
    while (!rest.empty()) {
        std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        std::size_t tab = line.find('\t');
        if (tab != std::string_view::npos) overrides[std::string(line.substr(0, tab))] = std::string(line.substr(tab + 1));
        if (end == std::string_view::npos) break;
        rest.remove_prefix(end + 1);
    }
    return overrides;
}

std::vector<float> ParseFloatList(const std::string& text) {
    std::vector<float> values;
    const char* cursor = text.c_str();
    while (*cursor) {
        char* end = nullptr;
        float value = std::strtof(cursor, &end);
        if (end == cursor) break;
        values.push_back(value);
        cursor = *end == ',' ? end + 1 : end;
    }
    return values;
}

// Render targets have no pixels in their files.
TextureData RenderTargetPlaceholder(const LoadedGame& game, const std::string& rtexPath) {
    std::vector<uint8_t> header = game.ExtractFile(rtexPath);
    uint32_t width = 0;
    uint32_t height = 0;
    if (header.size() >= 0x18) {
        std::memcpy(&width, header.data() + 0x10, 4);
        std::memcpy(&height, header.data() + 0x14, 4);
    }
    width = std::clamp<uint32_t>(width, 1, 4096);
    height = std::clamp<uint32_t>(height, 1, 4096);
    TextureData tex{};
    tex.width = static_cast<uint16_t>(width);
    tex.height = static_cast<uint16_t>(height);
    tex.format = 28;
    tex.mipInfo = (1 << 12) | 1;
    TextureMip mip{};
    mip.pitch = width * 4;
    mip.data.resize(static_cast<std::size_t>(width) * height * 4);
    uint32_t border = std::max<uint32_t>(2, std::min(width, height) / 128);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t* p = mip.data.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            bool edge = x < border || y < border || x >= width - border || y >= height - border;
            uint8_t shade = ((x / 32) + (y / 32)) % 2 ? 0x30 : 0x26;
            p[0] = edge ? 0x3A : shade;
            p[1] = edge ? 0x78 : shade;
            p[2] = edge ? 0xD6 : shade;
            p[3] = 0xFF;
        }
    }
    mip.size = static_cast<uint32_t>(mip.data.size());
    tex.mips.push_back(std::move(mip));
    return tex;
}

// Not in OutlineAsset: Explorer does not use Media Foundation.
std::string MovieOutline(const LoadedGame& game, const std::string& pakPath) {
    MovieInfo info = ReadMovieInfo(game, pakPath);
    std::string name = pakPath.substr(pakPath.find_last_of('/') + 1);
    if (std::size_t ext = name.find(".mov."); ext != std::string::npos) name.resize(ext + 4);
    auto format = [](const char* text, auto... values) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), text, values...);
        return std::string(buffer);
    };
    std::string text;
    auto prop = [&](const char* section, const char* key, const std::string& value) {
        text += std::string("P\t") + section + '\t' + key + '\t' + value + '\n';
    };
    auto clock = [&](double seconds) {
        int total = static_cast<int>(seconds);
        return format("%d:%02d.%d", total / 60, total % 60, static_cast<int>((seconds - total) * 10));
    };
    text += "N\t0\tmovie\t" + name + '\t' + format("%u x %u, %s", info.width, info.height, clock(info.duration).c_str()) + '\n';
    prop("Movie", "Path", pakPath);
    if (info.source != pakPath) prop("Movie", "Streamed from", info.source);
    prop("Movie", "Container", "ASF (Windows Media)");
    prop("Movie", "Duration", clock(info.duration));
    prop("Movie", "File size", format("%.1f MB", static_cast<double>(info.bytes) / (1024.0 * 1024.0)));
    prop("Video", "Codec", info.videoCodec);
    prop("Video", "Size", format("%u x %u", info.width, info.height));
    prop("Video", "Frame rate", format("%.3g fps", info.frameRate));
    if (info.audioCodec.empty()) {
        prop("Audio", "Stream", "none in the file");
    } else {
        prop("Audio", "Codec", info.audioCodec);
        prop("Audio", "Sample rate", format("%u Hz", info.sampleRate));
        prop("Audio", "Channels", std::to_string(info.channels));
    }
    return text;
}

void CheckDrawable(const ViewportScene& scene) {
    if (scene.build.draws.empty()) throw std::runtime_error("nothing to draw");
    if (scene.build.masters.prepass == nullptr) throw std::runtime_error("ZPrePassStatic not found");
}

}

RAE_API const char* rae_last_error(void) {
    return t_lastError.c_str();
}

RAE_API void rae_set_log_callback(RaeLogCallback callback) {
    g_logCallback = callback;
    SetLogSink(callback ? &ForwardLog : nullptr);
}

RAE_API RaeGame* rae_game_open(const char* gameId, const char* gameDir, const char* assetsDir) {
    RaeGame* result = nullptr;
    Guard([&] {
        GameId id = ParseGameId(gameId);
        if (gameDir == nullptr || *gameDir == 0) throw std::runtime_error("no game folder given");
        std::filesystem::path dir = Utf8Path(gameDir);
        std::filesystem::path assets = Utf8Path(assetsDir);
        LoadedGame loaded = LoadedGame::Open(CreateGame(id), dir, assets / "FileLists");
        if (loaded.Paks().empty()) throw std::runtime_error("no pak files found in " + dir.string());
        auto game = std::make_unique<RaeGame>(std::move(loaded), assets);
        LogInfo("%s: %zu paks mounted from %s", game->name.c_str(), game->game.Paks().size(), dir.string().c_str());
        result = game.release();
    });
    return result;
}

RAE_API void rae_game_close(RaeGame* game) {
    delete game;
}

RAE_API const char* rae_game_file_list(RaeGame* game, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        std::call_once(game->fileListOnce, [game] {
            std::unordered_map<std::string_view, std::size_t> index;
            std::vector<std::pair<std::string_view, int64_t>> entries;
            for (const PakFile& pak : game->game.Paks()) {
                for (const PakEntry& entry : pak.Entries()) {
                    auto [it, inserted] = index.emplace(entry.filePath, entries.size());
                    if (inserted) entries.emplace_back(entry.filePath, entry.uncompressedSize);
                    else entries[it->second].second = entry.uncompressedSize;
                }
            }
            std::string text;
            text.reserve(entries.size() * 72);
            for (const auto& [path, size] : entries) {
                if (path.starts_with("Unknown_")) {
                    try {
                        text.append(IdentifyUnknown(game->game, std::string(path)));
                    } catch (const std::exception&) {
                        text.append(path);
                    }
                } else {
                    text.append(path);
                }
                text.push_back('\t');
                text.append(std::to_string(size));
                text.push_back('\n');
            }
            game->fileList = std::move(text);
        });
        if (length != nullptr) *length = static_cast<int64_t>(game->fileList.size());
        result = game->fileList.c_str();
    });
    return result;
}

RAE_API const char* rae_game_outline(RaeGame* game, const char* pakPath, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        std::string path = pakPath;
        if (path.find(".mov.") != std::string::npos) {
            t_outline = MovieOutline(game->game, path);
        } else {
            bool needsRsz = path.find(".scn.") != std::string::npos || path.find(".user.") != std::string::npos ||
                            path.find(".pfb.") != std::string::npos || path.find(".motfsm2.") != std::string::npos ||
                            path.find(".fsmv2.") != std::string::npos;
            const RszTypeDatabase* rsz = needsRsz ? &game->Rsz() : nullptr;
            const MessageIndex* messages = path.find(".gui.") != std::string::npos ? &game->Messages() : nullptr;
            t_outline = OutlineAsset(game->game, rsz, path, messages);
        }
        if (length != nullptr) *length = static_cast<int64_t>(t_outline.size());
        result = t_outline.c_str();
    });
    return result;
}

// Source paths ("UI/x/y.tex") get natives/stm/ and the game's version suffix. A leading @
// marks a platform resource, whose file also carries ".x64" (or a language suffix).
static std::string ResolveAssetPath(const RaeGame& game, std::string path, const std::string& extension) {
    if (path.starts_with("natives/") || path.starts_with("Unknown_")) return path;
    if (path.starts_with("@")) path.erase(0, 1);
    std::string base = ToPakPath(path, extension.c_str());
    for (const char* suffix : { "", ".x64", ".x64.En", ".En" }) {
        if (game.game.FindEntry(base + suffix)) return base + suffix;
    }
    return base;
}

RAE_API const uint8_t* rae_texture_rgba(RaeGame* game, const char* texPath, int32_t maxSize, int32_t* width,
                                        int32_t* height) {
    static thread_local std::vector<uint8_t> pixels;
    const uint8_t* result = nullptr;
    Guard([&] {
        std::string path = StreamingCopyPath(game->game, ResolveAssetPath(*game, texPath, game->game.Game().GetTexExt()));
        TextureData tex = game->game.Readers().Get<TexReader>()->Read(game->game.ExtractFile(path));
        uint32_t mip = 0;
        uint32_t mips = std::max<uint32_t>(tex.MipsPerImage(), 1);
        while (maxSize > 0 && mip + 1 < mips && std::max(tex.width >> mip, tex.height >> mip) > static_cast<uint32_t>(maxSize)) mip++;
        uint32_t w = 0;
        uint32_t h = 0;
        if (!DecodeTextureRgba(tex, mip, pixels, w, h)) throw std::runtime_error("texture format " + std::to_string(tex.format) + " is not decoded");
        *width = static_cast<int32_t>(w);
        *height = static_cast<int32_t>(h);
        result = pixels.data();
    }, false);
    return result;
}

RAE_API const char* rae_gui_clips(RaeGame* game, const char* guiPath, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        t_outline = GuiClipTable(game->game, guiPath);
        if (length != nullptr) *length = static_cast<int64_t>(t_outline.size());
        result = t_outline.c_str();
    });
    return result;
}

RAE_API const char* rae_fsm_graph(RaeGame* game, const char* fsmPath, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        t_outline = FsmGraph(game->game, game->Rsz(), fsmPath);
        if (length != nullptr) *length = static_cast<int64_t>(t_outline.size());
        result = t_outline.c_str();
    });
    return result;
}

RAE_API int32_t rae_game_save_edited_scenes(RaeGame* game, const char* edits, const char* outDir) {
    int32_t count = -1;
    Guard([&] {
        std::vector<std::pair<std::string, std::string>> list;
        std::string text = edits ? edits : "";
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            if (std::size_t tab = line.find('\t'); tab != std::string::npos) list.emplace_back(line.substr(0, tab), line.substr(tab + 1));
            start = end + 1;
        }
        std::string error;
        std::size_t written = SaveEditedScenes(game->game, game->Rsz(), list, Utf8Path(outDir).string(), error);
        if (written == 0 && !error.empty()) throw std::runtime_error(error);
        count = static_cast<int32_t>(written);
    });
    return count;
}

RAE_API const char* rae_gui_fonts(RaeGame* game, int32_t language) {
    static thread_local std::string text;
    const char* result = nullptr;
    Guard([&] {
        std::vector<std::vector<GuiFontFace>> slots = GuiFontSlots(game->game, language);
        text.clear();
        char line[512];
        for (std::size_t s = 0; s < slots.size(); s++) {
            for (const GuiFontFace& face : slots[s]) {
                std::snprintf(line, sizeof(line), "%zu\t%s\t%g\t%g\t%g\t%g\n", s, face.path.c_str(), face.offset[0], face.offset[1],
                              face.scale[0], face.scale[1]);
                text += line;
            }
        }
        result = text.c_str();
    }, false);
    return result;
}

RAE_API const uint8_t* rae_font_data(RaeGame* game, const char* fontPath, int64_t* length) {
    static thread_local std::vector<uint8_t> font;
    const uint8_t* result = nullptr;
    Guard([&] {
        std::string path = fontPath;
        if (!path.starts_with("natives/")) {
            path = FindVersionedPath(game->game, path.starts_with("@") ? path.substr(1) : path);
            if (path.empty()) throw std::runtime_error(std::string("font not found: ") + fontPath);
        }
        font = DecryptFont(game->game.ExtractFile(path));
        *length = static_cast<int64_t>(font.size());
        result = font.data();
    }, false);
    return result;
}

RAE_API const char* rae_game_uvs(RaeGame* game, const char* uvsPath) {
    static thread_local std::string text;
    const char* result = nullptr;
    Guard([&] {
        std::string path = ResolveAssetPath(*game, uvsPath, game->game.Game().GetUvsExt());
        UvsData uvs = game->game.Readers().Get<UvsReader>()->Read(game->game.ExtractFile(path));
        text.clear();
        for (const std::string& texture : uvs.textures) text += "T\t" + texture + "\n";
        for (const UvsSequence& sequence : uvs.sequences) {
            text += "S\t" + std::to_string(sequence.firstPattern) + "\t" + std::to_string(sequence.patternCount) + "\n";
        }
        char line[160];
        for (const UvsPattern& pattern : uvs.patterns) {
            std::snprintf(line, sizeof(line), "P\t%u\t%g\t%g\t%g\t%g\n", pattern.textureIndex, pattern.rect[0], pattern.rect[1],
                          pattern.rect[2], pattern.rect[3]);
            text += line;
        }
        result = text.c_str();
    }, false);
    return result;
}

RAE_API const char* rae_game_messages(RaeGame* game, const char* pakPath, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        t_outline = MessageTable(game->game, pakPath);
        if (length != nullptr) *length = static_cast<int64_t>(t_outline.size());
        result = t_outline.c_str();
    });
    return result;
}

RAE_API int32_t rae_thumbnail(RaeGame* game, const char* pakPath, int32_t size, uint8_t* rgba) {
    return Guard([&] {
        if (size <= 0 || size > 1024) throw std::runtime_error("bad thumbnail size");
        std::vector<uint8_t> pixels = game->Thumbnails().Render(game->game, pakPath, static_cast<uint32_t>(size));
        std::memcpy(rgba, pixels.data(), pixels.size());
    }, false);
}

RAE_API int32_t rae_audio_play(RaeGame* game, const char* container, uint32_t mediaId) {
    return Guard([&] { Player().Play(game->DecodeMedia(container, mediaId)); });
}

RAE_API void rae_audio_stop(void) {
    std::lock_guard lock(g_playerMutex);
    if (g_player) g_player->Stop();
}

RAE_API int32_t rae_audio_status(float* position, float* duration) {
    std::lock_guard lock(g_playerMutex);
    double pos = 0;
    double length = 0;
    AudioPlayer::State state = g_player ? g_player->Status(pos, length) : AudioPlayer::State::Stopped;
    *position = static_cast<float>(pos);
    *duration = static_cast<float>(length);
    return static_cast<int32_t>(state);
}

RAE_API void rae_audio_seek(float seconds) {
    try {
        Player().Seek(seconds);
    } catch (const std::exception& e) {
        LogError("audio: %s", e.what());
    }
}

RAE_API void rae_audio_set_paused(int32_t paused) {
    Player().SetPaused(paused != 0);
}

RAE_API int32_t rae_audio_waveform(float* minMax, int32_t columns, int32_t maxChannels) {
    if (columns <= 0 || maxChannels <= 0) return 0;
    return static_cast<int32_t>(Player().Waveform(minMax, static_cast<uint32_t>(columns),
                                                  static_cast<uint32_t>(maxChannels)));
}

RAE_API int32_t rae_audio_export_wav(RaeGame* game, const char* container, uint32_t mediaId, const char* outputFile) {
    return Guard([&] {
        std::vector<uint8_t> wav = EncodeWav(game->DecodeMedia(container, mediaId));
        std::ofstream out(Utf8Path(outputFile), std::ios::binary);
        if (!out) throw std::runtime_error(std::string("cannot write ") + outputFile);
        out.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
    });
}

RAE_API void rae_audio_shutdown(void) {
    std::lock_guard lock(g_playerMutex);
    g_player.reset();
}

RAE_API int32_t rae_game_extract(RaeGame* game, const char* pakPath, const char* outputFile) {
    return Guard([&] {
        std::vector<uint8_t> data = game->game.ExtractFile(pakPath);
        std::ofstream out(Utf8Path(outputFile), std::ios::binary);
        if (!out) throw std::runtime_error(std::string("cannot write ") + outputFile);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        LogInfo("extracted %s (%zu bytes) to %s", pakPath, data.size(), outputFile);
    });
}

RAE_API RaeViewport* rae_viewport_create(void* parentHwnd) {
    RaeViewport* result = nullptr;
    Guard([&] { result = new RaeViewport(static_cast<HWND>(parentHwnd)); });
    return result;
}

RAE_API void* rae_viewport_hwnd(RaeViewport* viewport) {
    return viewport->viewport.Hwnd();
}

RAE_API void rae_viewport_set_parent(RaeViewport* viewport, void* parentHwnd) {
    viewport->viewport.SetParentWindow(static_cast<HWND>(parentHwnd));
}

RAE_API void rae_viewport_destroy(RaeViewport* viewport) {
    delete viewport;
}

RAE_API int32_t rae_viewport_load_mesh(RaeViewport* viewport, RaeGame* game, const char* meshPath,
                                       const char* overrides, int32_t keepCamera) {
    return Guard([&] {
        AssetOverrides replaced = ParseOverrides(overrides);
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepCamera != 0;
        scene->build.overrides = &replaced;
        std::string mesh = meshPath;
        auto mdfOverride = replaced.find(MeshMaterialKey(mesh));
        std::string mdf = mdfOverride != replaced.end() ? mdfOverride->second : FindMeshMaterial(game->game, mesh);
        LogInfo("mesh: %s (material %s)", mesh.c_str(), mdf.c_str());
        SceneMeshAsset asset = AppendSceneMesh(game->game, scene->build, scene->textures, mesh, mdf, true);
        if (!asset.valid) throw std::runtime_error("could not load " + mesh);
        AddSceneInstance(scene->build, asset, Identity());
        scene->meshAsset = true;
        scene->lodCount = asset.lodCount;
        CheckDrawable(*scene);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_character(RaeViewport* viewport, RaeGame* game, const char* parts, int32_t editedPart,
                                            const char* overrides, int32_t keepCamera) {
    return Guard([&] {
        std::vector<CharacterPart> list = ParseCharacterParts(parts ? parts : "");
        if (list.empty()) throw std::runtime_error("no character parts");
        AssetOverrides replaced = ParseOverrides(overrides);
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepCamera != 0;
        scene->build.overrides = &replaced;
        scene->frameWhole = true;
        SceneBuild& build = scene->build;
        std::vector<Mat4> worlds(list.size());
        std::vector<std::shared_ptr<const SkeletonData>> skeletons(list.size());
        std::vector<int32_t> skinned(list.size(), -1);
        std::size_t edited = editedPart >= 0 && static_cast<std::size_t>(editedPart) < list.size() ? static_cast<std::size_t>(editedPart) : 0;
        for (std::size_t i = 0; i < list.size(); i++) {
            const CharacterPart& part = list[i];
            auto mdfOverride = replaced.find(MeshMaterialKey(part.mesh));
            std::string mdf = mdfOverride != replaced.end() ? mdfOverride->second
                              : !part.material.empty() && game->game.FindEntry(part.material) ? part.material
                              : FindMeshMaterial(game->game, part.mesh);
            SceneMeshAsset asset;
            try {
                asset = AppendSceneMesh(game->game, build, scene->textures, part.mesh, mdf, i == edited);
            } catch (const std::exception& e) {
                if (i == edited) throw;
                LogWarning("character: part %s not loaded: %s", part.mesh.c_str(), e.what());
            }
            if (!asset.valid) {
                if (i == edited) throw std::runtime_error("could not load " + part.mesh);
                continue;
            }
            if (i == edited) {
                scene->lodCount = asset.lodCount;
                scene->mainInstance = static_cast<uint32_t>(build.worlds.size());
            }
            std::size_t parent = part.parent >= 0 && static_cast<std::size_t>(part.parent) < i ? static_cast<std::size_t>(part.parent) : SIZE_MAX;
            int32_t joint = -1;
            if (parent != SIZE_MAX && !part.parentJoint.empty() && skeletons[parent]) {
                const std::vector<std::string>& names = skeletons[parent]->names;
                for (std::size_t j = 0; j < names.size() && joint < 0; j++) {
                    if (_stricmp(names[j].c_str(), part.parentJoint.c_str()) == 0) joint = static_cast<int32_t>(j);
                }
            }
            if (joint >= 0) {
                worlds[i] = Mul(part.local, BindPoseWorld(*skeletons[parent], worlds[parent])[static_cast<std::size_t>(joint)]);
            } else {
                worlds[i] = Mul(part.local, parent != SIZE_MAX ? worlds[parent] : Identity());
            }
            skeletons[i] = asset.skeleton;
            auto instance = static_cast<uint32_t>(build.worlds.size());
            AddSceneInstance(build, asset, worlds[i], "part:" + std::to_string(i));
            if (asset.skeleton) {
                skinned[i] = static_cast<int32_t>(build.skinned.size() - 1);
                if (parent != SIZE_MAX && skinned[parent] >= 0) build.skinned.back().follows = skinned[parent];
            } else if (joint >= 0 && skinned[parent] >= 0) {
                build.attachments.push_back({ instance, skinned[parent], joint, part.local });
            }
        }
        scene->meshAsset = true;
        CheckDrawable(*scene);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API const char* rae_character_assemblies(RaeGame* game, const char* meshPath, int64_t* length) {
    const char* result = nullptr;
    Guard([&] {
        t_outline.clear();
        const std::vector<const CharacterAssembly*> found = game->Characters().WithMesh(meshPath);
        for (std::size_t i = 0; i < found.size(); i++) {
            const CharacterAssembly& assembly = *found[i];
            t_outline += "C\t" + std::to_string(i) + '\t' + assembly.name + '\t' + assembly.source + '\t' + assembly.motbank + '\t' +
                         assembly.jointMap + '\t' + std::to_string(assembly.uses) + '\n';
            t_outline += CharacterPartsText(assembly.parts);
        }
        if (length != nullptr) *length = static_cast<int64_t>(t_outline.size());
        result = t_outline.c_str();
    });
    return result;
}

RAE_API const char* rae_motbank_motlists(RaeGame* game, const char* motbankPath) {
    static thread_local std::string text;
    const char* result = nullptr;
    Guard([&] {
        text.clear();
        std::string path = motbankPath;
        // Motion components store the bank without its version suffix.
        if (!game->game.FindEntry(path)) path = ToPakPath(path, ".3");
        MotbankData bank = game->game.Readers().Get<MotbankReader>()->Read(game->game.ExtractFile(path));
        for (const MotbankEntry& entry : bank.entries) {
            text += std::to_string(entry.bankId) + '\t' + ToPakPath(entry.path, game->game.Game().GetMotlistExt().c_str()) + '\n';
        }
        result = text.c_str();
    });
    return result;
}

RAE_API int32_t rae_viewport_load_scene(RaeViewport* viewport, RaeGame* game, const char* scenePath,
                                        const char* overrides, int32_t keepCamera) {
    return Guard([&] {
        AssetOverrides replaced = ParseOverrides(overrides);
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepCamera != 0;
        scene->build.overrides = &replaced;
        scene->frameWhole = std::string_view(scenePath).find(".pfb.") != std::string_view::npos;
        LogInfo("scene: %s", scenePath);
        BuildSceneFromScn(game->game, game->Rsz(), scenePath, scene->build, scene->textures, 1.0f);
        LogInfo("scene built: %zu instances, %zu lights", scene->build.worlds.size(), scene->build.lights.size());
        if (scene->build.draws.empty() && scene->build.aiMaps.empty()) {
            viewport->viewport.Clear();
            return;
        }
        if (!scene->build.draws.empty()) CheckDrawable(*scene);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_material(RaeViewport* viewport, RaeGame* game, const char* mdfPath,
                                           const char* overrides, int32_t keepCamera) {
    return Guard([&] {
        AssetOverrides replaced = ParseOverrides(overrides);
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepCamera != 0;
        scene->build.overrides = &replaced;
        SceneMeshAsset asset = AppendMaterialSpheres(game->game, scene->build, scene->textures, mdfPath);
        if (!asset.valid) throw std::runtime_error(std::string("could not load ") + mdfPath);
        AddSceneInstance(scene->build, asset, Identity());
        scene->meshAsset = true;
        scene->materialPreview = true;
        scene->lodCount = asset.lodCount;
        CheckDrawable(*scene);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_effect(RaeViewport* viewport, RaeGame* game, const char* efxPath,
                                         const char* overrides, int32_t keepCamera) {
    return Guard([&] {
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->keepCamera = keepCamera != 0;
        LoadedEffect fx = LoadEffect(game->game, scene->textures, efxPath);
        for (const auto& [key, text] : ParseOverrides(overrides)) {
            std::size_t emitter = 0;
            std::string field;
            if (!ParseEffectParamKey(key, fx.path, emitter, field) || emitter >= fx.data.emitters.size()) continue;
            SetEffectParam(fx.data.emitters[emitter], field, ParseFloatList(text));
        }
        if (std::none_of(fx.data.emitters.begin(), fx.data.emitters.end(), IsDrawableEmitter)) {
            throw std::runtime_error("nothing to preview: only Billboard3D and RibbonLength emitters are simulated");
        }
        scene->effect = std::move(fx);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_texture(RaeViewport* viewport, RaeGame* game, const char* texPath,
                                          int32_t keepView) {
    return Guard([&] {
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->keepCamera = keepView != 0;
        TextureCache& cache = scene->textures;
        std::string source = StreamingCopyPath(game->game, texPath);
        if (source.find(".rtex.") != std::string::npos) cache.storage.push_back(RenderTargetPlaceholder(game->game, source));
        else cache.storage.push_back(game->game.Readers().Get<TexReader>()->Read(game->game.ExtractFile(source)));
        ViewportImage image;
        image.texture = static_cast<uint32_t>(cache.descs.size());
        cache.descs.push_back(ToGameTexture(cache.storage.back()));
        scene->image = std::move(image);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_uvs(RaeViewport* viewport, RaeGame* game, const char* uvsPath, int32_t keepView) {
    return Guard([&] {
        const UvsReader* reader = game->game.Readers().Get<UvsReader>();
        if (reader == nullptr) throw std::runtime_error("uv sequences are not supported for " + game->game.Game().GetId());
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->keepCamera = keepView != 0;
        ViewportImage image;
        image.atlas = true;
        image.uvs = reader->Read(game->game.ExtractFile(uvsPath));
        for (const std::string& texture : image.uvs.textures) {
            image.pages.push_back(LoadTexture(game->game, scene->textures, texture));
        }
        image.texture = image.pages.empty() ? 0 : image.pages[0];
        scene->image = std::move(image);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_collision(RaeViewport* viewport, RaeGame* game, const char* mcolPath, int32_t keepView) {
    return Guard([&] {
        std::string path = mcolPath;
        std::size_t at = path.find(".mcol.");
        if (at == std::string::npos) throw std::runtime_error("not a .mcol path: " + path);
        auto version = static_cast<uint32_t>(std::strtoul(path.c_str() + at + 6, nullptr, 10));
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepView != 0;
        scene->frameWhole = true;
        scene->collision = game->game.Readers().Get<McolReader>()->Read(game->game.ExtractFile(path), version);
        std::string mesh = SiblingMesh(game->game, path);
        if (!mesh.empty()) {
            try {
                SceneMeshAsset asset = AppendSceneMesh(game->game, scene->build, scene->textures, mesh,
                                                       FindMeshMaterial(game->game, mesh));
                if (asset.valid) AddSceneInstance(scene->build, asset, Identity(), "rendermesh");
            } catch (const std::exception& e) {
                LogWarning("collision: render mesh %s not loaded: %s", mesh.c_str(), e.what());
            }
        }
        if (!scene->build.draws.empty()) CheckDrawable(*scene);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_aimap(RaeViewport* viewport, RaeGame* game, const char* path, int32_t keepView) {
    return Guard([&] {
        const AimpReader* reader = game->game.Readers().Get<AimpReader>();
        if (reader == nullptr) throw std::runtime_error("AI maps are not supported for " + game->game.Game().GetId());
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->look = DefaultLook(game->game);
        scene->keepCamera = keepView != 0;
        scene->frameWhole = true;
        scene->aiMap = reader->Read(game->game.ExtractFile(path));
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API int32_t rae_viewport_load_movie(RaeViewport* viewport, RaeGame* game, const char* movPath, int32_t keepView) {
    return Guard([&] {
        auto scene = std::make_unique<ViewportScene>();
        scene->game = &game->game;
        scene->keepCamera = keepView != 0;
        ViewportImage image;
        image.movie = movPath;
        scene->image = std::move(image);
        viewport->viewport.Load(std::move(scene));
    });
}

RAE_API void rae_viewport_movie_set_paused(RaeViewport* viewport, int32_t paused) {
    viewport->viewport.SetMoviePaused(paused != 0);
}

RAE_API void rae_viewport_movie_seek(RaeViewport* viewport, float seconds) {
    viewport->viewport.SeekMovie(seconds);
}

RAE_API int32_t rae_viewport_movie_status(RaeViewport* viewport, float* position, float* duration, int32_t* paused) {
    bool isPaused = false;
    bool shown = viewport->viewport.MovieStatus(*position, *duration, isPaused);
    *paused = isPaused ? 1 : 0;
    return shown ? 1 : 0;
}

RAE_API int32_t rae_viewport_play_motion(RaeViewport* viewport, RaeGame* game, const char* motlistPath, int32_t motion) {
    return Guard([&] {
        std::string path = motlistPath ? motlistPath : "";
        if (path.empty() || motion < 0) {
            viewport->viewport.PlayMotion(nullptr, 0);
            return;
        }
        std::shared_ptr<const MotlistData> list = viewport->viewport.CachedMotlist(path);
        if (!list) {
            list = std::make_shared<const MotlistData>(
                game->game.Readers().Get<MotlistReader>()->Read(game->game.ExtractFile(path)));
            viewport->viewport.CacheMotlist(path, list);
        }
        if (static_cast<std::size_t>(motion) >= list->motions.size()) {
            throw std::runtime_error("motion " + std::to_string(motion) + " out of range");
        }
        viewport->viewport.PlayMotion(list, static_cast<std::size_t>(motion));
    });
}

RAE_API void rae_viewport_set_follow(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetFollow(on != 0);
}

RAE_API int32_t rae_viewport_get_follow(RaeViewport* viewport) {
    return viewport->viewport.Following() ? 1 : 0;
}

RAE_API void rae_viewport_motion_set_paused(RaeViewport* viewport, int32_t paused) {
    viewport->viewport.SetMotionPaused(paused != 0);
}

RAE_API void rae_viewport_motion_set_speed(RaeViewport* viewport, float speed) {
    viewport->viewport.SetMotionSpeed(speed);
}

RAE_API void rae_viewport_motion_set_loop(RaeViewport* viewport, int32_t loop) {
    viewport->viewport.SetMotionLoop(loop != 0);
}

RAE_API void rae_viewport_motion_seek(RaeViewport* viewport, float frame) {
    viewport->viewport.SeekMotion(frame);
}

RAE_API int32_t rae_viewport_motion_status(RaeViewport* viewport, float* frame, float* frameCount, float* frameRate,
                                           int32_t* paused, int32_t* drivenJoints) {
    bool isPaused = false;
    uint32_t driven = 0;
    bool shown = viewport->viewport.MotionStatus(*frame, *frameCount, *frameRate, isPaused, driven);
    *paused = isPaused ? 1 : 0;
    *drivenJoints = static_cast<int32_t>(driven);
    return shown ? 1 : 0;
}

RAE_API void rae_viewport_set_skeletons(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetSkeletons(on != 0);
}

RAE_API void rae_viewport_set_skeleton_style(RaeViewport* viewport, const RaeSkeletonStyle* style) {
    viewport->viewport.SetSkeletonStyle(*style);
}

RAE_API void rae_viewport_select_joint(RaeViewport* viewport, int32_t joint) {
    viewport->viewport.SelectJoint(joint);
}

RAE_API void rae_viewport_set_channels(RaeViewport* viewport, int32_t mask) {
    viewport->viewport.SetChannels(static_cast<uint32_t>(mask) & 0xF);
}

RAE_API void rae_viewport_set_flipbook(RaeViewport* viewport, int32_t playing, float fps) {
    viewport->viewport.SetFlipbook(playing != 0, fps);
}

RAE_API int32_t rae_viewport_image_status(RaeViewport* viewport, float* zoom, int32_t* pattern) {
    return viewport->viewport.ImageStatus(*zoom, *pattern) ? 1 : 0;
}

RAE_API void rae_viewport_effect_set_paused(RaeViewport* viewport, int32_t paused) {
    viewport->viewport.SetEffectPaused(paused != 0);
}

RAE_API void rae_viewport_effect_restart(RaeViewport* viewport) {
    viewport->viewport.RestartEffect();
}

RAE_API void rae_viewport_effect_set_speed(RaeViewport* viewport, float speed) {
    viewport->viewport.SetEffectSpeed(speed);
}

RAE_API int32_t rae_viewport_effect_status(RaeViewport* viewport, float* time, int32_t* particles) {
    uint32_t count = 0;
    bool loaded = viewport->viewport.EffectStatus(*time, count);
    *particles = static_cast<int32_t>(count);
    return loaded ? 1 : 0;
}

RAE_API const char* rae_game_skies(RaeGame* game) {
    std::call_once(game->skyListOnce, [game] {
        std::string defaultPath = game->game.Game().GetDefaultSkyPath();
        std::string suffix = ".tex" + game->game.Game().GetTexExt();
        std::set<std::string> found;
        for (const PakFile& pak : game->game.Paks()) {
            for (const PakEntry& entry : pak.Entries()) {
                std::string_view path = entry.filePath;
                if (path.find("/light/sky/") != std::string_view::npos && path.ends_with(suffix) &&
                    path.find("/streaming/") == std::string_view::npos && path != defaultPath) {
                    found.emplace(path);
                }
            }
        }
        game->skyList = defaultPath + '\n';
        for (const std::string& path : found) game->skyList += path + '\n';
    });
    return game->skyList.c_str();
}

RAE_API int32_t rae_viewport_set_sky(RaeViewport* viewport, RaeGame* game, const char* skyPath) {
    return Guard([&] { viewport->viewport.SetSky(game->Sky(skyPath ? skyPath : ""), SceneLook{}.skyIntensity); });
}

RAE_API void rae_viewport_set_background(RaeViewport* viewport, const RaeViewportBackground* background) {
    viewport->viewport.SetBackground(*background);
}

RAE_API void rae_viewport_clear(RaeViewport* viewport) {
    viewport->viewport.Clear();
}

RAE_API void rae_viewport_set_shading(RaeViewport* viewport, int32_t shading) {
    viewport->viewport.SetShading(static_cast<ViewportShading>(shading));
}

RAE_API void rae_viewport_set_grid(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetGrid(on != 0);
}

RAE_API void rae_viewport_set_gizmos(RaeViewport* viewport, uint32_t mask) {
    viewport->viewport.SetGizmos(mask);
}

RAE_API void rae_viewport_set_show_flags(RaeViewport* viewport, uint32_t mask) {
    viewport->viewport.SetShowFlags(mask);
}

RAE_API void rae_viewport_set_lod(RaeViewport* viewport, int32_t forcedLod, int32_t streamAll) {
    viewport->viewport.SetLodOverride(forcedLod, streamAll != 0);
}

RAE_API void rae_viewport_set_view(RaeViewport* viewport, int32_t view) {
    viewport->viewport.SetView(static_cast<ViewportView>(std::clamp(view, 0, 6)));
}

RAE_API int32_t rae_viewport_get_view(RaeViewport* viewport) {
    return static_cast<int32_t>(viewport->viewport.View());
}

RAE_API void rae_viewport_set_camera_speed(RaeViewport* viewport, float speed) {
    viewport->viewport.SetCameraSpeed(speed);
}

RAE_API float rae_viewport_get_camera_speed(RaeViewport* viewport) {
    return viewport->viewport.CameraSpeed();
}

RAE_API void rae_viewport_set_fov(RaeViewport* viewport, float degrees) {
    viewport->viewport.SetCameraFov(std::clamp(degrees, 5.0f, 170.0f) * 3.14159265f / 180.0f);
}

RAE_API void rae_viewport_bookmark(RaeViewport* viewport, int32_t slot, int32_t save) {
    viewport->viewport.Bookmark(slot, save != 0);
}

RAE_API uint32_t rae_viewport_get_bookmarks(RaeViewport* viewport) {
    return viewport->viewport.BookmarkMask();
}

RAE_API void rae_viewport_set_realtime(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetRealtime(on != 0);
}

RAE_API void rae_viewport_set_render_scale(RaeViewport* viewport, float scale) {
    viewport->viewport.SetRenderScale(std::clamp(scale, 0.25f, 2.0f));
}

RAE_API void rae_viewport_set_temporal_aa(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetTemporalAA(on != 0);
}

RAE_API void rae_viewport_set_exposure(RaeViewport* viewport, int32_t fixed, float ev) {
    viewport->viewport.SetExposure(fixed != 0, ev);
}

RAE_API void rae_viewport_undo(RaeViewport* viewport) {
    viewport->viewport.Undo();
}

RAE_API void rae_viewport_redo(RaeViewport* viewport) {
    viewport->viewport.Redo();
}

RAE_API uint32_t rae_viewport_get_undo_state(RaeViewport* viewport) {
    return viewport->viewport.UndoState();
}

RAE_API void rae_viewport_snap_to_floor(RaeViewport* viewport) {
    viewport->viewport.SnapSelectionToFloor();
}

RAE_API void rae_viewport_set_measuring(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetMeasuring(on != 0);
}

RAE_API void rae_viewport_set_transform_tool(RaeViewport* viewport, int32_t mode, int32_t local) {
    viewport->viewport.SetTransformTool(static_cast<GizmoMode>(std::clamp(mode, 0, 2)), local != 0);
}

RAE_API void rae_viewport_get_transform_tool(RaeViewport* viewport, int32_t* mode, int32_t* local) {
    if (mode) *mode = static_cast<int32_t>(viewport->viewport.TransformMode());
    if (local) *local = viewport->viewport.TransformLocal() ? 1 : 0;
}

RAE_API void rae_viewport_set_snap(RaeViewport* viewport, int32_t flags, float move, float rotate, float scale) {
    GizmoSnap snap;
    snap.translate = (flags & 1) != 0;
    snap.rotate = (flags & 2) != 0;
    snap.scale = (flags & 4) != 0;
    snap.translateStep = move;
    snap.rotateStep = rotate;
    snap.scaleStep = scale;
    viewport->viewport.SetSnap(snap);
}

RAE_API const char* rae_viewport_take_transform_changes(RaeViewport* viewport) {
    static thread_local std::string text;
    text = viewport->viewport.TakeTransformChanges();
    return text.c_str();
}

RAE_API void rae_viewport_set_stats_overlay(RaeViewport* viewport, int32_t on) {
    viewport->viewport.SetStatsOverlay(on != 0);
}

RAE_API void rae_viewport_set_projection_jitter(RaeViewport* viewport, float x, float y) {
    viewport->viewport.SetProjectionJitter(x, y);
}

RAE_API void rae_viewport_set_environment(RaeViewport* viewport, int64_t timeMs, int64_t frame, const float* userGlobalParams,
                                          int32_t floatCount) {
    ViewerEnvironment environment;
    environment.timeMs = timeMs;
    environment.frame = frame;
    float* params = &environment.userGlobalParams[0][0];
    for (int32_t i = 0; i < floatCount && i < 128; i++) params[i] = userGlobalParams[i];
    viewport->viewport.SetEnvironment(environment);
}

RAE_API void rae_viewport_reset_camera(RaeViewport* viewport) {
    viewport->viewport.ResetCamera();
}

RAE_API const char* rae_viewport_get_camera(RaeViewport* viewport) {
    static thread_local std::string text;
    constexpr float degrees = 180.0f / 3.14159265f;
    CameraPose pose = viewport->viewport.GetCameraPose();
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%.3f %.3f %.3f %.2f %.2f %.2f", pose.position[0], pose.position[1],
                  pose.position[2], pose.yaw * degrees, pose.pitch * degrees, pose.fov * degrees);
    text = buffer;
    return text.c_str();
}

RAE_API int32_t rae_viewport_set_camera(RaeViewport* viewport, const char* pose) {
    constexpr float radians = 3.14159265f / 180.0f;
    CameraPose parsed{};
    float fov = 0;
    float roll = 0;
    if (pose == nullptr || std::sscanf(pose, "%f %f %f %f %f %f %f", &parsed.position[0], &parsed.position[1], &parsed.position[2],
                                       &parsed.yaw, &parsed.pitch, &fov, &roll) < 5) {
        return 0;
    }
    parsed.yaw *= radians;
    parsed.pitch *= radians;
    parsed.fov = fov * radians;
    parsed.roll = roll * radians;
    viewport->viewport.SetCameraPose(parsed);
    return 1;
}

RAE_API const uint8_t* rae_viewport_capture(RaeViewport* viewport, uint32_t timeoutMs, int32_t* width, int32_t* height) {
    static thread_local std::vector<uint8_t> pixels;
    uint32_t w = 0;
    uint32_t h = 0;
    if (!viewport->viewport.CaptureFrame(pixels, w, h, timeoutMs)) return nullptr;
    *width = static_cast<int32_t>(w);
    *height = static_cast<int32_t>(h);
    return pixels.data();
}

RAE_API int32_t rae_viewport_capture_targets(RaeViewport* viewport, uint32_t timeoutMs, const RaeTargetImage** images,
                                             float* exposure) {
    static thread_local ViewerTargetCapture capture;
    static thread_local std::vector<RaeTargetImage> list;
    list.clear();
    if (!viewport->viewport.CaptureTargets(capture, timeoutMs)) return 0;
    for (const ViewerTargetImage& image : capture.images) {
        list.push_back({ image.name.c_str(), static_cast<int32_t>(image.width), static_cast<int32_t>(image.height),
                         static_cast<int32_t>(image.channels), image.pixels.data() });
    }
    *images = list.data();
    *exposure = capture.exposure;
    return static_cast<int32_t>(list.size());
}

RAE_API const char* rae_viewport_get_picked_draw(RaeViewport* viewport) {
    static thread_local std::string text;
    text = viewport->viewport.PickedDraw();
    return text.c_str();
}

RAE_API void rae_viewport_get_stats(RaeViewport* viewport, RaeViewportStats* stats) {
    *stats = viewport->viewport.Stats();
}

RAE_API void rae_viewport_select(RaeViewport* viewport, int32_t lod, int32_t submesh) {
    viewport->viewport.Select(lod, submesh);
}

static std::vector<std::string> SplitLines(const char* keys) {
    std::vector<std::string> list;
    std::string_view text = keys ? keys : "";
    while (!text.empty()) {
        std::size_t end = text.find('\n');
        std::string_view key = text.substr(0, end);
        if (!key.empty()) list.emplace_back(key);
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return list;
}

RAE_API void rae_viewport_select_objects(RaeViewport* viewport, const char* keys) {
    viewport->viewport.SelectObjects(SplitLines(keys));
}

RAE_API void rae_viewport_set_hidden_objects(RaeViewport* viewport, const char* keys) {
    viewport->viewport.SetHiddenObjects(SplitLines(keys));
}

RAE_API const char* rae_viewport_get_collision_groups(RaeViewport* viewport) {
    static thread_local std::string text;
    text.clear();
    for (const ViewportCollisionGroup& group : viewport->viewport.CollisionGroups()) {
        char color[8];
        std::snprintf(color, sizeof(color), "%02X%02X%02X", group.color[0], group.color[1], group.color[2]);
        text += group.name + "\t" + std::to_string(group.colliders) + "\t" + color + "\t" + (group.volume ? "1" : "0") + "\n";
    }
    return text.c_str();
}

RAE_API void rae_viewport_set_collision_groups(RaeViewport* viewport, const char* names) {
    viewport->viewport.SetCollisionGroups(SplitLines(names));
}

RAE_API const char* rae_viewport_get_aimap_groups(RaeViewport* viewport) {
    static thread_local std::string text;
    text.clear();
    for (const ViewportAiMapGroup& group : viewport->viewport.AiMapGroups()) {
        char color[8];
        std::snprintf(color, sizeof(color), "%02X%02X%02X", group.color[0], group.color[1], group.color[2]);
        text += group.name + "\t" + group.type + "\t" + std::to_string(group.nodes) + "\t" + color + "\n";
    }
    return text.c_str();
}

RAE_API void rae_viewport_set_aimap_groups(RaeViewport* viewport, const char* names) {
    viewport->viewport.SetAiMapGroups(SplitLines(names));
}

RAE_API const char* rae_viewport_get_light_states(RaeViewport* viewport) {
    static thread_local std::string text;
    text.clear();
    for (const std::string& state : viewport->viewport.LightStates()) text += state + "\n";
    return text.c_str();
}

RAE_API int32_t rae_viewport_take_hide_request(RaeViewport* viewport) {
    return viewport->viewport.TakeHideRequest() ? 1 : 0;
}

RAE_API int32_t rae_viewport_take_gizmo_toggles(RaeViewport* viewport) {
    return static_cast<int32_t>(viewport->viewport.TakeGizmoToggles());
}

RAE_API void rae_viewport_set_material_param(RaeViewport* viewport, const char* key, const float* values, int32_t count) {
    viewport->viewport.SetMaterialParam(key, std::vector<float>(values, values + std::max(count, 0)));
}

RAE_API uint32_t rae_viewport_get_selection(RaeViewport* viewport, int32_t* lod, int32_t* submesh) {
    return viewport->viewport.Selection(*lod, *submesh);
}

RAE_API const char* rae_viewport_get_selected_object(RaeViewport* viewport) {
    static thread_local std::string key;
    key = viewport->viewport.SelectedObject();
    return key.c_str();
}
