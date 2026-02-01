#include "Explorer/SceneSave.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"
#include "Renderer/RenderMath.h"

namespace {

struct ObjectEdit {
    bool has[3] = {};
    float values[3][3] = {};  // position, rotation (euler degrees), scale
};

void Patch(std::vector<uint8_t>& bytes, uint32_t offset, const float* values, std::size_t count) {
    if (offset == 0 || offset + count * 4 > bytes.size()) return;
    std::memcpy(bytes.data() + offset, values, count * 4);
}

}

std::size_t SaveEditedScenes(const LoadedGame& game, const RszTypeDatabase& db,
                             const std::vector<std::pair<std::string, std::string>>& edits, const std::string& outDir,
                             std::string& error) {
    std::map<std::string, std::map<std::size_t, ObjectEdit>> scenes;
    for (const auto& [key, text] : edits) {
        std::string objectKey;
        std::string field;
        if (!ParseTransformParamKey(key, objectKey, field)) continue;
        std::size_t bar = objectKey.rfind('|');
        if (bar == std::string::npos) continue;
        int slot = field == "position" ? 0 : field == "rotation" ? 1 : field == "scale" ? 2 : -1;
        float v[3];
        if (slot < 0 || std::sscanf(text.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]) != 3) continue;
        ObjectEdit& edit = scenes[objectKey.substr(0, bar)][std::stoul(objectKey.substr(bar + 1))];
        edit.has[slot] = true;
        std::memcpy(edit.values[slot], v, sizeof(v));
    }

    std::size_t written = 0;
    for (const auto& [pakPath, objects] : scenes) {
        try {
            std::vector<uint8_t> bytes = game.ExtractFile(pakPath);
            SceneData scn = ReadSceneOrPrefab(game, db, pakPath);
            for (const auto& [index, edit] : objects) {
                if (index >= scn.nodes.size()) continue;
                const SceneNode& node = scn.nodes[index];
                if (edit.has[0]) Patch(bytes, node.transformOffsets[0], edit.values[0], 3);
                if (edit.has[1]) {
                    float q[4];
                    EulerDegreesToQuat(edit.values[1], q);
                    Patch(bytes, node.transformOffsets[1], q, 4);
                }
                if (edit.has[2]) Patch(bytes, node.transformOffsets[2], edit.values[2], 3);
            }
            std::filesystem::path out = std::filesystem::path(outDir) / pakPath;
            std::filesystem::create_directories(out.parent_path());
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file) throw std::runtime_error("cannot write " + out.string());
            LogInfo("saved %s (%zu objects)", out.string().c_str(), objects.size());
            written++;
        } catch (const std::exception& e) {
            if (error.empty()) error = pakPath + ": " + e.what();
            LogWarning("save %s: %s", pakPath.c_str(), e.what());
        }
    }
    return written;
}
