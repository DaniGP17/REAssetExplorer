#include "Explorer/AssetOutline.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

#include "Core/Assets/Readers/AimpReader.h"
#include "Core/Assets/Readers/EfxReader.h"
#include "Core/Assets/Readers/FsmReader.h"
#include "Core/Assets/Readers/MotbankReader.h"
#include "Core/Assets/Readers/McolReader.h"
#include "Core/Assets/Readers/MdfReader.h"
#include "Core/Assets/Readers/MeshReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Core/Assets/Readers/SceneReader.h"
#include "Core/Assets/Readers/GuiReader.h"
#include "Core/Assets/Readers/MsgReader.h"
#include "Core/IO/MemoryReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/Readers/UserReader.h"
#include "Core/Assets/Readers/UvsReader.h"
#include "Core/Assets/Readers/WwiseReader.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Explorer/EffectParams.h"
#include "Explorer/EffectSimulator.h"
#include "Explorer/FsmMotions.h"
#include "Explorer/MessageIndex.h"
#include "Explorer/SceneBuilder.h"
#include "Explorer/SceneVisibility.h"

namespace {

// SceneBuilder's MAX_SCENE_DEPTH: every object the viewport can pick must be in the outline.
constexpr int MAX_SCENE_NESTING = 6;
constexpr std::size_t MAX_COMPONENT_FIELDS = 48;
constexpr std::size_t MAX_BONE_CLIPS = 256;

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

std::string Vec3Text(float x, float y, float z) {
    return Format("%.4g, %.4g, %.4g", x, y, z);
}

// Editable values keep enough digits to round-trip a float.
std::string PreciseVec3Text(float x, float y, float z) {
    return Format("%.7g, %.7g, %.7g", x, y, z);
}

class OutlineWriter {
public:
    // key identifies nodes the viewport can select (LODs, submeshes, scene objects).
    void Node(int depth, std::string_view kind, std::string_view name, std::string_view detail = {},
              std::string_view key = {}, bool hidden = false) {
        text += "N\t";
        text += std::to_string(depth);
        Field(kind);
        Field(name);
        Field(detail);
        if (!key.empty() || hidden) Field(key);
        if (hidden) Field("1");
        text += '\n';
    }

    // overrideKey marks an asset reference the inspector may replace (AssetOverrides)
    // or a live parameter ("param:"); components names a parameter's values.
    void Prop(std::string_view section, std::string_view key, std::string_view value,
              std::string_view overrideKey = {}, std::string_view components = {}) {
        text += 'P';
        Field(section);
        Field(key);
        Field(value);
        if (!overrideKey.empty() || !components.empty()) Field(overrideKey);
        if (!components.empty()) Field(components);
        text += '\n';
    }

    std::string text;

private:
    void Field(std::string_view value) {
        text += '\t';
        for (char c : value) text += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
    }
};

// x.mesh.2109108288 -> x.mesh, x.pck.3.x64.ja -> x.pck
std::string DisplayName(const std::string& pakPath) {
    static const std::set<std::string, std::less<>> TAGS = {
        "x64", "stm", "nsw", "ja", "en", "fr", "it", "de", "es", "ru", "pl", "nl", "pt", "ptbr", "ko",
        "zhcn", "zhtw", "ar", "tr", "latam", "hu", "cs"
    };
    std::string name = pakPath.substr(pakPath.find_last_of('/') + 1);
    for (std::size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0; dot = name.find_last_of('.')) {
        std::string tail = name.substr(dot + 1);
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
        bool numeric = !tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos;
        if (!numeric && !TAGS.contains(tail)) break;
        name.resize(dot);
    }
    return name;
}

std::string ShortType(const std::string& typeName) {
    std::size_t dot = typeName.find_last_of('.');
    return dot == std::string::npos ? typeName : typeName.substr(dot + 1);
}

// XYZ euler angles in degrees, display only.
void QuatToEuler(const RszVec4& q, float out[3]) {
    constexpr float RAD_TO_DEG = 57.2957795f;
    float sinr = 2 * (q.w * q.x + q.y * q.z);
    float cosr = 1 - 2 * (q.x * q.x + q.y * q.y);
    float sinp = 2 * (q.w * q.y - q.z * q.x);
    float siny = 2 * (q.w * q.z + q.x * q.y);
    float cosy = 1 - 2 * (q.y * q.y + q.z * q.z);
    out[0] = std::atan2(sinr, cosr) * RAD_TO_DEG;
    out[1] = (std::fabs(sinp) >= 1 ? std::copysign(1.5707963f, sinp) : std::asin(sinp)) * RAD_TO_DEG;
    out[2] = std::atan2(siny, cosy) * RAD_TO_DEG;
}

bool PlausibleFloat(float f, uint32_t bits) {
    if (bits == 0) return true;
    if ((bits & 0x7F800000u) == 0) return false;
    return std::isfinite(f) && std::fabs(f) >= 1e-6f && std::fabs(f) <= 1e7f;
}

std::string BytesText(const RszBytes& b) {
    auto floats = [&b](std::size_t count) {
        std::string text;
        for (std::size_t i = 0; i < count; i++) {
            float f;
            std::memcpy(&f, b.data() + i * 4, 4);
            text += (i ? ", " : "") + Format("%.4g", f);
        }
        return text;
    };
    switch (b.size()) {
        case 1: return b[0] <= 1 ? (b[0] ? "true" : "false") : std::to_string(b[0]);
        case 2: return std::to_string(b[0] | b[1] << 8);
        case 4: {
            float f;
            uint32_t u;
            std::memcpy(&f, b.data(), 4);
            std::memcpy(&u, b.data(), 4);
            return PlausibleFloat(f, u) ? Format("%.6g", f) : std::to_string(u);
        }
        case 8: return floats(2);
        case 12: return floats(3);
        case 16: return floats(4);
        default: return Format("bytes[%zu]", b.size());
    }
}

std::string ValueText(const RszValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "";
        else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
        else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) return std::to_string(v);
        else if constexpr (std::is_same_v<T, double>) return Format("%.6g", v);
        else if constexpr (std::is_same_v<T, std::string>) return v.empty() ? "\"\"" : v;
        else if constexpr (std::is_same_v<T, RszVec3>) return Vec3Text(v.x, v.y, v.z);
        else if constexpr (std::is_same_v<T, RszVec4>) return Format("%.4g, %.4g, %.4g, %.4g", v.x, v.y, v.z, v.w);
        else if constexpr (std::is_same_v<T, RszGuid>) {
            std::string text;
            for (std::size_t i = 0; i < v.bytes.size(); i++) {
                if (i == 4 || i == 6 || i == 8 || i == 10) text += '-';
                text += Format("%02x", v.bytes[i]);
            }
            return text;
        }
        else if constexpr (std::is_same_v<T, RszObjectRef>) return Format("object #%u", v.index);
        else if constexpr (std::is_same_v<T, RszBytes>) return BytesText(v);
        else return Format("[%zu items]", v.size());
    }, value.data);
}

// Field names in the RE7/RE8 type dumps are positional (vN).
const char* KnownFieldLabel(const std::string& typeName, const std::string& field) {
    if (typeName == "via.render.Mesh") {
        if (field == "v2") return "Mesh (v2)";
        if (field == "v3") return "Material (v3)";
    }
    if (typeName == "via.render.PointLight" || typeName == "via.render.SpotLight") {
        if (field == "v1") return "Intensity (v1)";
        if (field == "v3") return "Color (v3)";
        if (field == "v16") return "Radius (v16)";
        if (field == "v17" && typeName == "via.render.SpotLight") return "Cone (v17)";
    }
    return nullptr;
}

void OutlineScene(OutlineWriter& out, const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath,
                  int depth, int nesting, std::set<std::string>& visited) {
    if (!visited.insert(pakPath).second) return;
    SceneData scn;
    try {
        scn = ReadSceneOrPrefab(game, db, pakPath);
    } catch (const std::exception& e) {
        out.Node(depth, "missing", "(not loaded)", e.what());
        return;
    }

    std::vector<uint8_t> hiddenNodes = HiddenByDefault(scn);
    EffectProviderCache effectProviders;
    std::function<void(std::size_t, int)> emit = [&](std::size_t index, int nodeDepth) {
        const SceneNode& node = scn.nodes[index];
        if (node.kind == SceneNode::Kind::Folder) {
            bool linked = !node.scenePath.empty();
            out.Node(nodeDepth, linked ? "sceneref" : "folder", node.name, linked ? node.scenePath : "", {},
                     hiddenNodes[index]);
            out.Prop("Folder", "Name", node.name);
            if (linked) {
                std::string linkedPath = ToPakPath(node.scenePath, game.Game().GetSceneExt().c_str());
                out.Prop("Folder", "Scene", linkedPath);
                if (nesting < MAX_SCENE_NESTING) OutlineScene(out, game, db, linkedPath, nodeDepth + 1, nesting + 1, visited);
            }
        } else {
            std::string summary;
            for (int32_t componentIndex : node.componentIndices) {
                const RszInstance* component = scn.Instance(componentIndex);
                if (component == nullptr || component->typeName == "via.Transform") continue;
                if (!summary.empty()) summary += ", ";
                summary += ShortType(component->typeName);
            }
            std::string objectKey = SceneObjectKey(pakPath, index);
            out.Node(nodeDepth, "gameobject", node.name, summary, objectKey, hiddenNodes[index]);
            float euler[3];
            QuatToEuler(node.rotation, euler);
            out.Prop("Transform", "Position", PreciseVec3Text(node.position.x, node.position.y, node.position.z),
                     TransformParamKey(objectKey, "position"));
            out.Prop("Transform", "Rotation", PreciseVec3Text(euler[0], euler[1], euler[2]), TransformParamKey(objectKey, "rotation"));
            out.Prop("Transform", "Scale", PreciseVec3Text(node.scale.x, node.scale.y, node.scale.z),
                     TransformParamKey(objectKey, "scale"));
            for (int32_t componentIndex : node.componentIndices) {
                const RszInstance* component = scn.Instance(componentIndex);
                if (component == nullptr || component->typeName == "via.Transform") continue;
                std::string section = component->typeName.empty() ? "(unknown component)" : component->typeName;
                if (component->fields.empty()) out.Prop(section, "", component->parsed ? "(no fields)" : "(not parsed)");
                // Native physics types only have untyped fields; their colliders are decoded instead.
                if (component->typeName == "via.physics.Colliders") {
                    std::vector<ColliderInfo> colliders = DescribeColliders(scn.rsz, *component);
                    if (colliders.empty()) out.Prop(section, "", "(no colliders)");
                    for (std::size_t c = 0; c < colliders.size(); c++) {
                        std::string label = Format("Collider %zu", c);
                        out.Prop(section, label, colliders[c].shape);
                        if (!colliders[c].mesh.empty()) out.Prop(section, label + " mesh", colliders[c].mesh);
                        else if (colliders[c].shape == "MeshShape") out.Prop(section, label + " mesh", "(not set)");
                        out.Prop(section, label + " filter", colliders[c].filter);
                    }
                    continue;
                }
                if (component->typeName == "via.navigation.AIMap") {
                    std::vector<std::string> maps = AiMapReferences(scn.rsz, *component);
                    if (maps.empty()) out.Prop(section, "", "(no maps)");
                    for (std::size_t m = 0; m < maps.size(); m++) out.Prop(section, Format("Map %zu", m), maps[m]);
                    continue;
                }
                bool objectEffects = component->typeName == "via.effect.script.ObjectEffectManager";
                if (objectEffects || component->typeName == "via.effect.script.EnvironmentEffectManager") {
                    std::vector<ObjectEffectElement> elements = objectEffects
                        ? ReadObjectEffects(game, db, scn, *component, effectProviders)
                        : ReadEnvironmentEffects(game, scn, node);
                    std::set<std::string> containers;
                    for (const ObjectEffectElement& element : elements) {
                        if (!element.container.empty() && containers.insert(element.container).second) {
                            out.Prop(section, "Data container", element.container);
                        }
                    }
                    std::size_t shownEffects = 0;
                    for (const ObjectEffectElement& element : elements) {
                        std::string plays = element.autoPlay ? "on its own"
                                          : element.triggerId != 0xFFFFFFFFu ? Format("trigger %u", element.triggerId)
                                                                            : "game event (" + ShortType(element.type) + ")";
                        for (const std::string& efx : element.effects) {
                            std::string label = Format("Effect %zu", shownEffects++);
                            out.Prop(section, label, efx);
                            if (!element.provider.empty()) out.Prop(section, label + " provider", element.provider);
                            out.Prop(section, label + " plays", plays);
                        }
                    }
                    if (elements.empty()) out.Prop(section, "Effect", "(none)");
                }
                std::size_t shown = 0;
                for (const RszFieldValue& field : component->fields) {
                    if (shown++ >= MAX_COMPONENT_FIELDS) {
                        out.Prop(section, "...", Format("%zu more fields", component->fields.size() - MAX_COMPONENT_FIELDS));
                        break;
                    }
                    const char* label = KnownFieldLabel(component->typeName, field.name);
                    bool meshReference = component->typeName == "via.render.Mesh" &&
                                         (field.name == "v2" || field.name == "v3");
                    out.Prop(section, label ? label : field.name, ValueText(field.value),
                             meshReference ? ComponentFieldKey(SceneObjectKey(pakPath, index), componentIndex, field.name)
                                           : std::string());
                }
            }
        }
        for (std::size_t child : node.children) emit(child, nodeDepth + 1);
    };
    for (std::size_t root : scn.roots) emit(root, depth);
}

void OutlineMaterials(OutlineWriter& out, const MaterialData& mdf, const std::string& mdfPakPath, int depth) {
    for (std::size_t i = 0; i < mdf.materials.size(); i++) {
        const MaterialEntry& mat = mdf.materials[i];
        std::string master = mat.masterMaterialPath.substr(mat.masterMaterialPath.find_last_of("/\\") + 1);
        // The key is the material slot, which the material editor previews.
        out.Node(depth, "material", mat.name, master, std::to_string(i));
        out.Prop("Material", "Master", mat.masterMaterialPath);
        out.Prop("Material", "Shader type", std::to_string(mat.shaderType));
        out.Prop("Material", "Flags", Format("0x%08X", mat.flags));
        out.Prop("Material", "Alpha test", mat.AlphaTest() ? "true" : "false");
        for (const MaterialTexture& tex : mat.textures) {
            out.Prop("Textures", tex.type, tex.path, MaterialTextureKey(mdfPakPath, mat.name, tex.type));
        }
        for (const MaterialProperty& prop : mat.properties) {
            std::string values;
            for (std::size_t i = 0; i < prop.values.size() && i < 4; i++) values += (i ? ", " : "") + Format("%.7g", prop.values[i]);
            out.Prop("Parameters", prop.name, values, MaterialParamKey(mdfPakPath, mat.name, prop.name));
        }
    }
}

void OutlineMesh(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    MeshData mesh = game.Readers().Get<MeshReader>()->Read(game.ExtractFile(pakPath));
    std::string name = DisplayName(pakPath);
    out.Node(0, "mesh", name, Format("%zu LODs", mesh.lods.size()));
    out.Prop("Mesh", "Path", pakPath);
    out.Prop("Mesh", "Version", std::to_string(mesh.version));
    out.Prop("Mesh", "LODs", std::to_string(mesh.lods.size()));
    out.Prop("Mesh", "Joints", std::to_string(mesh.joints.size()));
    out.Prop("Mesh", "Materials", std::to_string(mesh.materialNames.size()));
    out.Prop("Mesh", "Skin weights", std::to_string(mesh.skinWeightCount));
    out.Prop("Bounds", "Min", Vec3Text(mesh.aabbMin[0], mesh.aabbMin[1], mesh.aabbMin[2]));
    out.Prop("Bounds", "Max", Vec3Text(mesh.aabbMax[0], mesh.aabbMax[1], mesh.aabbMax[2]));

    std::string mdfPath;
    try {
        mdfPath = FindMeshMaterial(game, pakPath);
    } catch (const std::exception&) {
    }
    out.Node(1, "group", "Materials", std::to_string(mesh.materialNames.size()));
    if (!mdfPath.empty()) out.Prop("Materials", "Material file", mdfPath, MeshMaterialKey(pakPath));
    try {
        if (mdfPath.empty()) throw std::runtime_error("no material file");
        OutlineMaterials(out, game.Readers().Get<MdfReader>()->Read(game.ExtractFile(mdfPath)), mdfPath, 2);
    } catch (const std::exception&) {
        for (const std::string& material : mesh.materialNames) out.Node(2, "material", material);
    }

    out.Node(1, "group", "LODs", std::to_string(mesh.lods.size()));
    for (std::size_t i = 0; i < mesh.lods.size(); i++) {
        std::size_t clusters = 0;
        uint64_t indices = 0;
        for (const MeshPart& part : mesh.lods[i].parts) {
            clusters += part.clusters.size();
            for (const MeshCluster& cluster : part.clusters) indices += cluster.indexCount;
        }
        out.Node(2, "lod", Format("LOD %zu", i), Format("%llu triangles", static_cast<unsigned long long>(indices / 3)),
                 std::to_string(i));
        out.Prop("LOD", "Parts", std::to_string(mesh.lods[i].parts.size()));
        out.Prop("LOD", "Clusters", std::to_string(clusters));
        out.Prop("LOD", "Triangles", std::to_string(indices / 3));
        out.Prop("LOD", "LOD factor", Format("%g", mesh.lods[i].lodFactor));

        // Same numbering as SceneDrawTag::submesh.
        uint32_t submesh = 0;
        for (std::size_t p = 0; p < mesh.lods[i].parts.size(); p++) {
            const MeshPart& part = mesh.lods[i].parts[p];
            for (std::size_t c = 0; c < part.clusters.size(); c++) {
                const MeshCluster& cluster = part.clusters[c];
                std::string material = cluster.materialId < mesh.materialNames.size()
                    ? mesh.materialNames[cluster.materialId] : std::string();
                std::string label = material.empty() ? Format("Submesh %u", submesh)
                                                     : Format("Submesh %u - %s", submesh, material.c_str());
                out.Node(3, "submesh", label, Format("%u triangles", cluster.indexCount / 3),
                         Format("%zu:%u", i, submesh));
                out.Prop("Submesh", "Index", std::to_string(submesh));
                out.Prop("Submesh", "LOD", std::to_string(i));
                out.Prop("Submesh", "Part", Format("%zu (id %u)", p, part.partId));
                out.Prop("Submesh", "Cluster", std::to_string(c));
                out.Prop("Submesh", "Material", material.empty() ? Format("#%u", cluster.materialId) : material);
                out.Prop("Geometry", "Triangles", std::to_string(cluster.indexCount / 3));
                out.Prop("Geometry", "Indices", std::to_string(cluster.indexCount));
                out.Prop("Geometry", "Start index", std::to_string(cluster.startIndexLocation));
                out.Prop("Geometry", "Base vertex", std::to_string(cluster.baseVertexLocation));
                submesh++;
            }
        }
    }

    if (mesh.joints.empty()) return;
    out.Node(1, "group", "Skeleton", Format("%zu joints", mesh.joints.size()));
    std::vector<std::vector<std::size_t>> children(mesh.joints.size());
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < mesh.joints.size(); i++) {
        uint16_t parent = mesh.joints[i].parentIndex;
        if (parent < mesh.joints.size() && parent != i) children[parent].push_back(i);
        else roots.push_back(i);
    }
    std::function<void(std::size_t, int)> emitJoint = [&](std::size_t index, int depth) {
        const MeshJoint& joint = mesh.joints[index];
        // The key is the joint's position in the skeleton, which the viewport highlights.
        out.Node(depth, "joint", joint.name, {}, Format("joint:%zu", index));
        out.Prop("Joint", "Index", std::to_string(joint.index));
        out.Prop("Joint", "Parent", joint.parentIndex < mesh.joints.size() ? mesh.joints[joint.parentIndex].name : "-");
        out.Prop("Joint", "Local position", Vec3Text(joint.localMatrix[12], joint.localMatrix[13], joint.localMatrix[14]));
        out.Prop("Joint", "World position", Vec3Text(joint.worldMatrix[12], joint.worldMatrix[13], joint.worldMatrix[14]));
        if (depth < 64) {
            for (std::size_t child : children[index]) emitJoint(child, depth + 1);
        }
    };
    for (std::size_t root : roots) emitJoint(root, 2);
}

// Wwise ids are FNV-1 hashes of lowercase names; languages are the only names
// worth recovering (objects are hashed from names the games do not ship).
uint32_t WwiseHash(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash *= 16777619u;
        hash ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c)));
    }
    return hash;
}

std::string WwiseLanguage(uint32_t id) {
    static const char* const LANGUAGES[] = {
        "SFX", "English(US)", "English(UK)", "Japanese", "French(France)", "German", "Italian", "Spanish(Spain)",
        "Spanish(Mexico)", "Russian", "Polish", "Portuguese(Brazil)", "Chinese(PRC)", "Chinese(Taiwan)",
        "Chinese(HK)", "Korean", "Arabic", "Turkish", "Dutch", "Czech", "Hungarian"
    };
    for (const char* name : LANGUAGES) {
        if (WwiseHash(name) == id) return name;
    }
    return Format("0x%08X", id);
}

std::string CodecName(uint32_t pluginId) {
    if ((pluginId & 0xF) == 2) return Format("Source plugin 0x%X", pluginId);
    switch (pluginId >> 16) {
        case 0x01: return "PCM";
        case 0x02: return "ADPCM";
        case 0x03: return "XMA";
        case 0x04: return "Vorbis";
        case 0x09: return "XWMA";
        case 0x0A: return "AAC";
        case 0x0C: return "ATRAC9";
        case 0x13: case 0x14: case 0x15: return "Opus";
        default: return Format("0x%X", pluginId);
    }
}

std::string WemFormatName(uint16_t format) {
    switch (format) {
        case 0xFFFF: return "Wwise Vorbis";
        case 0xFFFE: return "PCM";
        case 0x0001: return "PCM";
        case 0x0002: return "ADPCM";
        case 0x3040: return "Opus";
        default: return Format("0x%04X", format);
    }
}

const char* StreamTypeName(uint8_t type) {
    switch (type) {
        case 0: return "In the bank";
        case 1: return "Prefetch, then streamed";
        case 2: return "Streamed from the .pck";
        default: return "Unknown";
    }
}

std::string ActionName(uint16_t type) {
    static const std::map<uint8_t, const char*> NAMES = {
        { 0x01, "Stop" }, { 0x02, "Pause" }, { 0x03, "Resume" }, { 0x04, "Play" }, { 0x05, "Play and Continue" },
        { 0x06, "Mute" }, { 0x07, "Unmute" }, { 0x08, "Set Pitch" }, { 0x09, "Reset Pitch" }, { 0x0A, "Set Volume" },
        { 0x0B, "Reset Volume" }, { 0x0C, "Set Bus Volume" }, { 0x0D, "Reset Bus Volume" }, { 0x0E, "Set LPF" },
        { 0x0F, "Reset LPF" }, { 0x10, "Use State" }, { 0x11, "Unuse State" }, { 0x12, "Set State" },
        { 0x13, "Set Game Parameter" }, { 0x14, "Reset Game Parameter" }, { 0x19, "Set Switch" },
        { 0x1A, "Toggle Bypass" }, { 0x1B, "Reset Bypass Effect" }, { 0x1C, "Break" }, { 0x1D, "Trigger" },
        { 0x1E, "Seek" }, { 0x1F, "Release" }, { 0x20, "Set HPF" }, { 0x21, "Play Event" },
        { 0x22, "Reset Playlist" }, { 0x30, "Set Effect" }, { 0x31, "Reset Effect" }
    };
    auto it = NAMES.find(static_cast<uint8_t>(type >> 8));
    return it != NAMES.end() ? it->second : Format("Action 0x%04X", type);
}

std::string DurationText(const WemInfo& info) {
    if (!info.valid || info.sampleRate == 0 || info.sampleCount == 0) return {};
    double seconds = static_cast<double>(info.sampleCount) / info.sampleRate;
    int minutes = static_cast<int>(seconds / 60);
    return Format("%d:%06.3f", minutes, seconds - minutes * 60);
}

std::string SizeText(uint64_t bytes) {
    if (bytes < 1024) return Format("%llu B", static_cast<unsigned long long>(bytes));
    if (bytes < 1024 * 1024) return Format("%.1f KB", bytes / 1024.0);
    return Format("%.1f MB", bytes / (1024.0 * 1024.0));
}

void WemProps(OutlineWriter& out, const WemInfo& info) {
    if (!info.valid) return;
    out.Prop("Audio", "Format", WemFormatName(info.format));
    out.Prop("Audio", "Channels", std::to_string(info.channels));
    out.Prop("Audio", "Sample rate", Format("%u Hz", info.sampleRate));
    if (std::string duration = DurationText(info); !duration.empty()) out.Prop("Audio", "Duration", duration);
}

void OutlineSoundBank(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    SoundBankData bank = game.Readers().Get<BnkReader>()->Read(game.ExtractFile(pakPath));
    std::map<uint32_t, const SoundBankObject*> byId;
    for (const SoundBankObject& obj : bank.objects) byId[obj.id] = &obj;
    std::map<uint32_t, const SoundBankMedia*> mediaById;
    for (const SoundBankMedia& media : bank.media) mediaById[media.id] = &media;

    std::string name = DisplayName(pakPath);
    out.Node(0, "soundbank", name, Format("%zu objects, %zu media", bank.objects.size(), bank.media.size()));
    out.Prop("Sound Bank", "Path", pakPath);
    out.Prop("Sound Bank", "Version", std::to_string(bank.version));
    out.Prop("Sound Bank", "Bank ID", Format("0x%08X", bank.bankId));
    out.Prop("Sound Bank", "Language", WwiseLanguage(bank.languageId));
    out.Prop("Sound Bank", "Project ID", std::to_string(bank.projectId));
    std::string chunks;
    for (const std::string& chunk : bank.chunks) chunks += (chunks.empty() ? "" : ", ") + chunk;
    out.Prop("Sound Bank", "Chunks", chunks);
    for (const auto& [id, bankName] : bank.bankNames) out.Prop("Bank Names", Format("0x%08X", id), bankName);
    for (const std::string& warning : bank.warnings) out.Prop("Warnings", "", warning);

    auto label = [](const SoundBankObject& obj) { return Format("%s 0x%08X", HircTypeName(obj.type), obj.id); };
    auto kindOf = [](const SoundBankObject& obj) -> const char* {
        switch (static_cast<HircType>(obj.type)) {
            case HircType::Sound: return "sound";
            case HircType::MusicSegment:
            case HircType::MusicTrack:
            case HircType::MusicSwitch:
            case HircType::MusicRanSeq: return "music";
            case HircType::RanSeqContainer:
            case HircType::SwitchContainer:
            case HircType::ActorMixer:
            case HircType::LayerContainer: return "container";
            default: return "object";
        }
    };
    auto isNode = [](const SoundBankObject& obj) {
        switch (static_cast<HircType>(obj.type)) {
            case HircType::Sound: case HircType::RanSeqContainer: case HircType::SwitchContainer:
            case HircType::ActorMixer: case HircType::LayerContainer: case HircType::MusicSegment:
            case HircType::MusicTrack: case HircType::MusicSwitch: case HircType::MusicRanSeq: return true;
            default: return false;
        }
    };
    auto sourceProps = [&](const SoundBankObject& obj) {
        for (std::size_t i = 0; i < obj.sources.size(); i++) {
            const SoundBankSource& source = obj.sources[i];
            std::string section = obj.sources.size() > 1 ? Format("Source %zu", i) : std::string("Source");
            out.Prop(section, "Media ID", Format("0x%08X", source.sourceId));
            out.Prop(section, "Codec", CodecName(source.pluginId));
            out.Prop(section, "Storage", StreamTypeName(source.streamType));
            if (source.memorySize) out.Prop(section, "In-memory size", SizeText(source.memorySize));
            auto media = mediaById.find(source.sourceId);
            if (media != mediaById.end()) WemProps(out, media->second->info);
        }
    };

    std::vector<const SoundBankObject*> events;
    for (const SoundBankObject& obj : bank.objects) {
        if (obj.type == static_cast<uint8_t>(HircType::Event)) events.push_back(&obj);
    }
    out.Node(1, "group", "Events", std::to_string(events.size()));
    for (const SoundBankObject* event : events) {
        out.Node(2, "event", Format("Event 0x%08X", event->id), Format("%zu actions", event->actionIds.size()));
        out.Prop("Event", "ID", Format("0x%08X", event->id));
        out.Prop("Event", "Actions", std::to_string(event->actionIds.size()));
        for (uint32_t actionId : event->actionIds) {
            auto action = byId.find(actionId);
            if (action == byId.end()) {
                out.Node(3, "missing", Format("Action 0x%08X", actionId), "not in this bank");
                continue;
            }
            const SoundBankObject& act = *action->second;
            auto target = byId.find(act.targetId);
            std::string targetText = target != byId.end() ? label(*target->second)
                                                          : Format("0x%08X (other bank)", act.targetId);
            out.Node(3, "action", ActionName(act.actionType), targetText);
            out.Prop("Action", "ID", Format("0x%08X", act.id));
            out.Prop("Action", "Type", Format("%s (0x%04X)", ActionName(act.actionType).c_str(), act.actionType));
            out.Prop("Action", "Target", targetText);
        }
    }

    std::map<uint32_t, std::vector<const SoundBankObject*>> children;
    std::vector<const SoundBankObject*> roots;
    std::size_t nodeCount = 0;
    for (const SoundBankObject& obj : bank.objects) {
        if (!isNode(obj)) continue;
        nodeCount++;
        auto parent = byId.find(obj.parentId);
        if (obj.parentId != 0 && parent != byId.end() && isNode(*parent->second)) children[obj.parentId].push_back(&obj);
        else roots.push_back(&obj);
    }
    out.Node(1, "group", "Hierarchy", std::to_string(nodeCount));
    std::function<void(const SoundBankObject&, int)> emit = [&](const SoundBankObject& obj, int depth) {
        auto kids = children.find(obj.id);
        std::size_t kidCount = kids != children.end() ? kids->second.size() : 0;
        std::string detail = !obj.sources.empty() ? CodecName(obj.sources[0].pluginId) + ", " +
                                                        StreamTypeName(obj.sources[0].streamType)
                                                  : Format("%zu children", kidCount);
        // Playable nodes carry "wem:<media id>" (codec sources only, not plugins).
        bool playable = !obj.sources.empty() && (obj.sources[0].pluginId & 0xF) == 1;
        out.Node(depth, kindOf(obj), label(obj), detail, playable ? Format("wem:%u", obj.sources[0].sourceId) : std::string());
        out.Prop("Object", "ID", Format("0x%08X", obj.id));
        out.Prop("Object", "Type", HircTypeName(obj.type));
        if (obj.parentId != 0) {
            auto parent = byId.find(obj.parentId);
            out.Prop("Object", "Parent", parent != byId.end() ? label(*parent->second)
                                                              : Format("0x%08X (other bank)", obj.parentId));
        }
        if (kidCount) out.Prop("Object", "Children", std::to_string(kidCount));
        sourceProps(obj);
        if (kids != children.end() && depth < 64) {
            for (const SoundBankObject* child : kids->second) emit(*child, depth + 1);
        }
    };
    for (const SoundBankObject* root : roots) emit(*root, 2);

    out.Node(1, "group", "Media", std::to_string(bank.media.size()));
    for (const SoundBankMedia& media : bank.media) {
        std::string duration = DurationText(media.info);
        out.Node(2, "wem", Format("0x%08X.wem", media.id),
                 duration.empty() ? SizeText(media.size) : duration, Format("wem:%u", media.id));
        out.Prop("Media", "ID", Format("0x%08X", media.id));
        out.Prop("Media", "Size", SizeText(media.size));
        out.Prop("Media", "Offset", std::to_string(media.offset));
        // Prefetch copies hold the start; the rest streams from the .pck.
        if (media.info.valid && media.info.dataSize > media.size) out.Prop("Media", "Prefetch only", "true");
        WemProps(out, media.info);
    }

    std::vector<const SoundBankObject*> others;
    for (const SoundBankObject& obj : bank.objects) {
        auto type = static_cast<HircType>(obj.type);
        if (!isNode(obj) && type != HircType::Event && type != HircType::Action) others.push_back(&obj);
    }
    if (others.empty()) return;
    out.Node(1, "group", "Other Objects", std::to_string(others.size()));
    for (const SoundBankObject* obj : others) {
        out.Node(2, "object", label(*obj));
        out.Prop("Object", "ID", Format("0x%08X", obj->id));
        out.Prop("Object", "Type", HircTypeName(obj->type));
    }
}

void OutlineSoundPackage(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    std::vector<uint8_t> first = game.ReadFileRange(pakPath, 0, 8);
    std::vector<uint8_t> header = game.ReadFileRange(pakPath, 0, PckReader::HeaderBytes(first));
    SoundPackageData package = game.Readers().Get<PckReader>()->ReadHeader(header);

    // Copies outside streaming/ hold only the header.
    const PakEntry* entry = game.FindEntry(pakPath);
    uint64_t fileSize = entry ? static_cast<uint64_t>(entry->uncompressedSize) : 0;
    bool hasData = !package.streams.empty() && package.streams.back().offset + package.streams.back().size <= fileSize;
    constexpr std::size_t MAX_PROBED = 4000;
    if (hasData) {
        for (std::size_t i = 0; i < package.streams.size() && i < MAX_PROBED; i++) {
            SoundPackageEntry& stream = package.streams[i];
            stream.info = ParseWemHeader(game.ReadFileRange(pakPath, stream.offset, std::min<uint32_t>(stream.size, 256)));
        }
    }

    std::map<uint32_t, std::string> languages(package.languages.begin(), package.languages.end());
    auto language = [&languages](uint32_t id) {
        auto it = languages.find(id);
        return it != languages.end() ? it->second : std::to_string(id);
    };

    std::string name = DisplayName(pakPath);
    out.Node(0, "soundpackage", name, Format("%zu streamed files", package.streams.size()));
    out.Prop("Package", "Path", pakPath);
    out.Prop("Package", "Version", std::to_string(package.version));
    out.Prop("Package", "Header size", SizeText(package.headerSize));
    out.Prop("Package", "Contains audio", hasData ? "true" : "false");
    if (!hasData && !package.streams.empty()) {
        out.Prop("Package", "Note", "Header only; the audio is in the streaming/ copy of this package");
    }

    out.Node(1, "group", "Languages", std::to_string(package.languages.size()));
    for (const auto& [id, languageName] : package.languages) {
        out.Node(2, "language", languageName, Format("id %u", id));
        out.Prop("Language", "Name", languageName);
        out.Prop("Language", "ID", std::to_string(id));
    }

    auto emitEntries = [&](const char* group, const char* kind, const std::vector<SoundPackageEntry>& entries) {
        if (entries.empty()) return;
        out.Node(1, "group", group, std::to_string(entries.size()));
        for (const SoundPackageEntry& e : entries) {
            std::string duration = DurationText(e.info);
            bool wide = e.id > 0xFFFFFFFFull;
            std::string id = wide ? Format("0x%016llX", static_cast<unsigned long long>(e.id))
                                  : Format("0x%08X", static_cast<uint32_t>(e.id));
            bool media = std::string_view(kind) == "wem";
            out.Node(2, kind, id + (media ? ".wem" : ".bnk"), duration.empty() ? SizeText(e.size) : duration,
                     media && !wide ? Format("wem:%u", static_cast<uint32_t>(e.id)) : std::string());
            out.Prop("Entry", "ID", id);
            out.Prop("Entry", "Size", SizeText(e.size));
            out.Prop("Entry", "Offset", std::to_string(e.offset));
            out.Prop("Entry", "Language", language(e.languageId));
            WemProps(out, e.info);
        }
    };
    emitEntries("Streamed Media", "wem", package.streams);
    emitEntries("Banks", "soundbank", package.banks);
    emitEntries("External Sources", "wem", package.externals);
}

const char* BlendName(EfxBlend blend) {
    switch (blend) {
        case EfxBlend::AlphaBlend: return "Alpha Blend";
        case EfxBlend::Physical: return "Physical";
        case EfxBlend::AddContrast: return "Add Contrast";
        case EfxBlend::EdgeBlend: return "Edge Blend";
        case EfxBlend::Multiply: return "Multiply";
    }
    return "?";
}

std::string ItemNames(const EfxReader& reader, const EfxEmitter& e) {
    std::string names;
    for (uint32_t type : e.itemTypes) {
        const char* name = reader.ItemName(type);
        names += (names.empty() ? "" : ", ") + (name ? std::string(name) : Format("Item %u", type));
    }
    return names;
}

std::string PreviewText(const EfxReader& reader, const EfxEmitter& e) {
    if (IsDrawableEmitter(e)) {
        if (e.ribbonLength) return Format("RibbonLength, %s", BlendName(e.ribbonLength->Blend()));
        return Format("Billboard3D, %s", BlendName(e.billboard->Blend()));
    }
    if (e.distortion) return "Distortion (not previewed)";
    for (uint32_t type : e.itemTypes) {
        if (const char* name = reader.ItemName(type); name && std::string_view(name).starts_with("Type")) {
            return Format("%s (not previewed)", name + 4);
        }
    }
    return e.spawn ? "Nothing drawn" : "No spawner";
}

void OutlineEffect(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    const EfxReader* reader = game.Readers().Get<EfxReader>();
    if (reader == nullptr) throw std::runtime_error("effects are not supported for " + game.Game().GetId());
    EffectData fx = reader->Read(game.ExtractFile(pakPath));
    std::size_t drawn = std::count_if(fx.emitters.begin(), fx.emitters.end(), IsDrawableEmitter);
    out.Node(0, "effect", DisplayName(pakPath), Format("%zu emitters", fx.emitters.size()));
    out.Prop("Effect", "Path", pakPath);
    out.Prop("Effect", "Version", std::to_string(fx.version));
    out.Prop("Effect", "Emitters", std::to_string(fx.emitters.size()));
    out.Prop("Effect", "Previewed", Format("%zu of %zu", drawn, fx.emitters.size()));

    const char* velocityTypes[] = { "Direction", "Normal", "Radial", "Spread" };
    const char* shapeTypes[] = { "Box", "Sphere", "Cylinder" };
    const char* uvPlayTypes[] = { "Pause", "Loop", "Finish", "Clamp" };
    for (std::size_t i = 0; i < fx.emitters.size(); i++) {
        const EfxEmitter& e = fx.emitters[i];
        std::string preview = PreviewText(*reader, e);
        out.Node(1, "emitter", e.name.empty() ? Format("Emitter %zu", i) : e.name, preview, Format("emitter:%zu", i));
        out.Prop("Emitter", "Index", std::to_string(i));
        out.Prop("Emitter", "Preview", preview);
        out.Prop("Emitter", "Items", ItemNames(*reader, e));

        std::map<std::string, std::vector<std::pair<std::string, std::string>>> info;
        if (e.billboard) info["Billboard"].emplace_back("Blend", BlendName(e.billboard->Blend()));
        if (e.velocity) {
            auto type = static_cast<uint32_t>(e.velocity->type);
            info["Velocity"].emplace_back("Type", type < 4 ? velocityTypes[type] : std::to_string(type));
        }
        if (e.shape) {
            auto type = static_cast<uint32_t>(e.shape->type);
            info["Shape"].emplace_back("Type", type < 3 ? shapeTypes[type] : std::to_string(type));
        }
        if (e.uvSequence) {
            info["UV Sequence"].emplace_back("Atlas", e.uvSequence->uvsPath);
            info["UV Sequence"].emplace_back("Play", uvPlayTypes[static_cast<uint32_t>(e.uvSequence->PlayType())]);
        }
        std::string section;
        for (const EffectParam& param : EffectParams(e)) {
            if (param.section != section) {
                section = param.section;
                for (const auto& [key, value] : info[section]) out.Prop(section, key, value);
            }
            std::string values;
            for (float v : param.values) values += (values.empty() ? "" : ", ") + Format("%.6g", v);
            out.Prop(param.section, param.label, values, EffectParamKey(pakPath, i, param.id), param.components);
        }
    }
}

std::string DxgiFormatName(uint32_t format) {
    static const std::map<uint32_t, const char*> NAMES = {
        { 2, "R32G32B32A32_FLOAT" }, { 10, "R16G16B16A16_FLOAT" }, { 11, "R16G16B16A16_UNORM" },
        { 24, "R10G10B10A2_UNORM" }, { 26, "R11G11B10_FLOAT" }, { 28, "R8G8B8A8_UNORM" },
        { 29, "R8G8B8A8_UNORM_SRGB" }, { 34, "R16G16_FLOAT" }, { 35, "R16G16_UNORM" }, { 41, "R32_FLOAT" },
        { 49, "R8G8_UNORM" }, { 51, "R8G8_SNORM" }, { 54, "R16_FLOAT" }, { 56, "R16_UNORM" },
        { 61, "R8_UNORM" }, { 63, "R8_SNORM" }, { 65, "A8_UNORM" }, { 71, "BC1_UNORM" }, { 72, "BC1_UNORM_SRGB" },
        { 74, "BC2_UNORM" }, { 75, "BC2_UNORM_SRGB" }, { 77, "BC3_UNORM" }, { 78, "BC3_UNORM_SRGB" },
        { 80, "BC4_UNORM" }, { 81, "BC4_SNORM" }, { 83, "BC5_UNORM" }, { 84, "BC5_SNORM" },
        { 87, "B8G8R8A8_UNORM" }, { 91, "B8G8R8A8_UNORM_SRGB" }, { 95, "BC6H_UF16" }, { 96, "BC6H_SF16" },
        { 98, "BC7_UNORM" }, { 99, "BC7_UNORM_SRGB" }
    };
    auto it = NAMES.find(format);
    return it != NAMES.end() ? Format("%s (%u)", it->second, format) : Format("DXGI %u", format);
}

void OutlineTexture(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    std::string source = StreamingCopyPath(game, pakPath);
    TextureData tex = game.Readers().Get<TexReader>()->Read(game.ExtractFile(source));
    uint32_t images = std::max(tex.NumImages(), 1u);
    uint32_t mips = std::max(tex.MipsPerImage(), 1u);
    out.Node(0, "texture", DisplayName(pakPath), Format("%u x %u", tex.width, tex.height));
    out.Prop("Texture", "Path", pakPath);
    if (source != pakPath) out.Prop("Texture", "Streamed from", source);
    out.Prop("Texture", "Format", DxgiFormatName(tex.format));
    out.Prop("Texture", "Size", Format("%u x %u", tex.width, tex.height));
    out.Prop("Texture", "Mips", std::to_string(mips));
    out.Prop("Texture", "Images", std::to_string(images));
    out.Prop("Texture", "Version", std::to_string(tex.version));
    uint64_t bytes = 0;
    for (const TextureMip& mip : tex.mips) bytes += mip.data.size();
    out.Prop("Texture", "Data", SizeText(bytes));

    auto mipNodes = [&](uint32_t image, int depth) {
        for (uint32_t m = 0; m < mips; m++) {
            uint32_t w = std::max(tex.width >> m, 1);
            uint32_t h = std::max(tex.height >> m, 1);
            out.Node(depth, "mip", Format("Mip %u", m), Format("%u x %u", w, h), Format("mip:%u:%u", image, m));
            out.Prop("Mip", "Size", Format("%u x %u", w, h));
            std::size_t index = static_cast<std::size_t>(image) * mips + m;
            if (index < tex.mips.size()) {
                out.Prop("Mip", "Data", SizeText(tex.mips[index].data.size()));
                out.Prop("Mip", "Pitch", std::to_string(tex.mips[index].pitch));
            }
        }
    };
    if (images == 1) {
        mipNodes(0, 1);
        return;
    }
    const char* faces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    for (uint32_t i = 0; i < images; i++) {
        std::string name = images == 6 ? Format("Face %s", faces[i]) : Format("Image %u", i);
        out.Node(1, "image", name, Format("%u mips", mips), Format("image:%u", i));
        out.Prop("Image", "Index", std::to_string(i));
        mipNodes(i, 2);
    }
}

void OutlineCollision(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    std::size_t at = pakPath.find(".mcol.");
    auto version = static_cast<uint32_t>(std::strtoul(pakPath.c_str() + at + 6, nullptr, 10));
    TerrainData mcol = game.Readers().Get<McolReader>()->Read(game.ExtractFile(pakPath), version);
    std::size_t primitives = mcol.spheres.size() + mcol.capsules.size() + mcol.boxes.size();
    out.Node(0, "collision", DisplayName(pakPath), Format("%zu triangles, %zu primitives", mcol.triangles.size(), primitives));
    out.Prop("Collision Mesh", "Path", pakPath);
    out.Prop("Collision Mesh", "Version", std::to_string(version));
    out.Prop("Collision Mesh", "Vertices", std::to_string(mcol.vertices.size()));
    out.Prop("Collision Mesh", "Triangles", std::to_string(mcol.triangles.size()));
    out.Prop("Collision Mesh", "Spheres", std::to_string(mcol.spheres.size()));
    out.Prop("Collision Mesh", "Capsules", std::to_string(mcol.capsules.size()));
    out.Prop("Collision Mesh", "Boxes", std::to_string(mcol.boxes.size()));
    if (mcol.hasBvh) {
        out.Prop("Collision Mesh", "Bounds min", Vec3Text(mcol.boundsMin[0], mcol.boundsMin[1], mcol.boundsMin[2]));
        out.Prop("Collision Mesh", "Bounds max", Vec3Text(mcol.boundsMax[0], mcol.boundsMax[1], mcol.boundsMax[2]));
    } else {
        out.Prop("Collision Mesh", "Contents", "No collision (header only)");
    }

    std::string mesh = SiblingMesh(game, pakPath);
    if (!mesh.empty()) {
        out.Node(1, "rendermesh", DisplayName(mesh), "Render mesh", "rendermesh");
        out.Prop("Render Mesh", "Mesh", mesh);
    }

    std::size_t layerCount = mcol.layerNames.size();
    auto grow = [&](int32_t layer) { layerCount = std::max(layerCount, static_cast<std::size_t>(std::max(layer, 0)) + 1); };
    for (const TerrTriangle& t : mcol.triangles) grow(t.layer);
    for (const TerrSphere& s : mcol.spheres) grow(s.layer);
    for (const TerrCapsule& c : mcol.capsules) grow(c.layer);
    for (const TerrBox& b : mcol.boxes) grow(b.layer);
    for (std::size_t i = 0; i < layerCount; i++) {
        auto count = [&](const auto& items) {
            return std::count_if(items.begin(), items.end(), [&](const auto& item) { return item.layer == static_cast<int32_t>(i); });
        };
        std::size_t triangles = count(mcol.triangles);
        std::size_t shapes = count(mcol.spheres) + count(mcol.capsules) + count(mcol.boxes);
        const TerrLayerName* name = i < mcol.layerNames.size() ? &mcol.layerNames[i] : nullptr;
        std::string title = name && !name->main.empty() ? name->main : Format("Layer %zu", i);
        out.Node(1, "layer", title, Format("%zu triangles, %zu primitives", triangles, shapes), Format("layer:%zu", i));
        out.Prop("Layer", "Index", std::to_string(i));
        out.Prop("Layer", "Name", name ? name->main : "-");
        out.Prop("Layer", "Sub name", name && !name->sub.empty() ? name->sub : "-");
        out.Prop("Layer", "Triangles", std::to_string(triangles));
        out.Prop("Layer", "Primitives", std::to_string(shapes));
    }
}

const char* AiMapSectionName(uint8_t section) {
    static const char* NAMES[] = { "NoSection", "Owner", "Section", "ConnectManager", "IndividualSection" };
    return section < std::size(NAMES) ? NAMES[section] : "?";
}

std::string AiMapColorText(uint32_t rgba) {
    return Format("#%02X%02X%02X", rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF);
}

void OutlineAiMap(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    AiMapData map = game.Readers().Get<AimpReader>()->Read(game.ExtractFile(pakPath));
    std::size_t nodes = map.main.nodes.size() + map.secondary.nodes.size();
    out.Node(0, "aimap", DisplayName(pakPath), Format("%s, %zu nodes", AiMapTypeName(map.type), nodes));
    out.Prop("AI Map", "Path", pakPath);
    out.Prop("AI Map", "Name", map.name.empty() ? "-" : map.name);
    out.Prop("AI Map", "Type", AiMapTypeName(map.type));
    out.Prop("AI Map", "Section", AiMapSectionName(map.section));
    out.Prop("AI Map", "Structure", std::to_string(map.structure));

    std::map<int, std::size_t> layerNodes;
    for (bool secondary : { false, true }) {
        const AiMapContainer& container = secondary ? map.secondary : map.main;
        if (!container.present) continue;
        std::string title = secondary ? "Secondary" : "Main";
        std::string section = title + " Content";
        out.Prop(section, "Groups", std::to_string(container.groups.size()));
        out.Prop(section, "Vertices", std::to_string(container.vertices.size()));
        out.Prop(section, "Nodes", std::to_string(container.nodes.size()));
        out.Prop(section, "Links", std::to_string(container.links.size()));
        out.Prop(section, "Bounds min", Vec3Text(container.boundsMin[0], container.boundsMin[1], container.boundsMin[2]));
        out.Prop(section, "Bounds max", Vec3Text(container.boundsMax[0], container.boundsMax[1], container.boundsMax[2]));
        for (const AiMapNode& node : container.nodes) layerNodes[AiMapNodeLayer(node.attributes)]++;
        for (std::size_t g = 0; g < container.groups.size(); g++) {
            const AiMapGroup& group = container.groups[g];
            out.Node(1, "aigroup", title + " " + AiMapGroupName(group), Format("%zu nodes", group.NodeCount()),
                     AiMapGroupKey(secondary, g));
            out.Prop("Content Group", "Class", group.className);
            out.Prop("Content Group", "Container", title);
            out.Prop("Content Group", "Nodes", std::to_string(group.NodeCount()));
        }
        if (!container.links.empty()) {
            out.Node(1, "ailinks", title + " links", Format("%zu links", container.links.size()), AiMapLinksKey(secondary));
            out.Prop("Links", "Container", title);
            out.Prop("Links", "Count", std::to_string(container.links.size()));
        }
    }
    for (const auto& [layer, count] : layerNodes) {
        const AiMapLayer* info = layer >= 0 && static_cast<std::size_t>(layer) < map.layers.size() ? &map.layers[layer] : nullptr;
        std::string name = layer < 0 ? "No attribute" : info && !info->name.empty() ? info->name : Format("Layer %d", layer);
        out.Node(1, "ailayer", name, Format("%zu nodes", count), AiMapLayerKey(layer));
        out.Prop("Layer", "Index", layer < 0 ? "-" : std::to_string(layer));
        out.Prop("Layer", "Name", info ? info->name : "-");
        out.Prop("Layer", "Color", info ? AiMapColorText(info->color) : "Map color");
        out.Prop("Layer", "Nodes", std::to_string(count));
    }
    for (std::size_t i = 0; i < map.layers.size(); i++) {
        const AiMapLayer& layer = map.layers[i];
        if (layer.name.empty() || layer.name == std::to_string(i)) continue;
        out.Prop("Layers", std::to_string(i), layer.name + "  " + AiMapColorText(layer.color));
    }
}

void OutlineMotbank(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    MotbankData bank = game.Readers().Get<MotbankReader>()->Read(game.ExtractFile(pakPath));
    out.Node(0, "motbank", DisplayName(pakPath), Format("%zu motion lists", bank.entries.size()));
    out.Prop("Motion Bank", "Path", pakPath);
    out.Prop("Motion Bank", "Version", std::to_string(bank.version));
    out.Prop("Motion Bank", "User variables", bank.uvarPath.empty() ? "-" : bank.uvarPath);
    out.Prop("Motion Bank", "Joint map", bank.jmapPath.empty() ? "-" : bank.jmapPath);
    for (const MotbankEntry& entry : bank.entries) {
        std::string motlist = ToPakPath(entry.path, game.Game().GetMotlistExt().c_str());
        out.Node(1, "motlist", DisplayName(motlist), Format("Bank %d", entry.bankId));
        out.Prop("Motion List", "Bank", std::to_string(entry.bankId));
        out.Prop("Motion List", "Bank type", std::to_string(entry.bankType));
        out.Prop("Motion List", "Type mask", Format("0x%llx", static_cast<unsigned long long>(entry.bankTypeMask)));
        out.Prop("Motion List", "Path", entry.path);
    }
}

std::string FsmTypeLabel(const std::string& typeName) {
    std::string label = ShortType(typeName);
    for (std::string_view prefix : { "Fsm2Condition", "Fsm2Action", "Condition" }) {
        if (label.size() > prefix.size() && label.starts_with(prefix)) return label.substr(prefix.size());
    }
    return label;
}

std::string FsmConditionText(const FsmData& fsm, const FsmObjectRef& ref) {
    const RszInstance* condition = fsm.Instance(ref);
    return condition ? FsmTypeLabel(condition->typeName) : "Always";
}

std::string FsmBlendText(const FsmData& fsm, int32_t transitionData) {
    if (transitionData < 0 || static_cast<std::size_t>(transitionData) >= fsm.transitions.size()) return {};
    static const char* MODES[] = { "Cut", "FrontFade", "CrossFade", "SyncCrossFade", "SyncPointCrossFade",
                                   "FrontOffsetFade", "InertiaFade", "FrontSpeedFade" };
    const FsmTransitionData& t = fsm.transitions[static_cast<std::size_t>(transitionData)];
    uint32_t mode = t.InterpolationMode();
    std::string text = mode < std::size(MODES) ? MODES[mode] : Format("Mode %u", mode);
    if (mode != 0) text += Format(" %gf", t.interpolationFrame);
    return text;
}

void FsmTransitionProps(OutlineWriter& out, const FsmData& fsm, const std::string& section, int32_t transitionData) {
    if (transitionData < 0 || static_cast<std::size_t>(transitionData) >= fsm.transitions.size()) return;
    static const char* ENDS[] = { "None", "EndOfMotion", "ExitFrame", "ExitFrameFromEnd", "SyncPoint", "SyncPointFromEnd" };
    static const char* CURVES[] = { "Linear", "Smooth", "EaseIn", "EaseOut" };
    static const char* STARTS[] = { "None", "Frame", "NormalizedTime", "SyncTime", "AutoSyncTime", "AutoSyncTimeSamePointCount" };
    const FsmTransitionData& t = fsm.transitions[static_cast<std::size_t>(transitionData)];
    auto name = [](const auto& names, uint32_t value) {
        return value < std::size(names) ? std::string(names[value]) : std::to_string(value);
    };
    out.Prop(section, "Blend", FsmBlendText(fsm, transitionData));
    out.Prop(section, "Curve", name(CURVES, t.InterpolationCurve()));
    out.Prop(section, "Leaves at", name(ENDS, t.EndType()));
    if (t.EndType() == 2 || t.EndType() == 3) out.Prop(section, "Exit frame", Format("%g", t.exitFrame));
    out.Prop(section, "Next starts at", name(STARTS, t.StartType()));
    if (t.StartType() != 0) out.Prop(section, "Start frame", Format("%g", t.startFrame));
    if (t.ContOnLayer()) out.Prop(section, "Continue on layer", Format("%u (speed %g)", t.contOnLayerNo, t.contOnLayerSpeed));
}

void FsmInstanceProps(OutlineWriter& out, const std::string& section, const RszInstance& instance) {
    out.Prop(section, "Type", instance.typeName);
    for (std::size_t f = 0; f < instance.fields.size() && f < MAX_COMPONENT_FIELDS; f++) {
        out.Prop(section, instance.fields[f].name, ValueText(instance.fields[f].value));
    }
}

std::string FsmMotionText(FsmMotions* motions, const FsmPlayMotion& play) {
    if (motions) {
        FsmMotions::Motion motion = motions->Find(play.bank, play.motionId);
        if (!motion.name.empty() && motion.name[0] != '(') return motion.name;
        if (!motion.name.empty()) return Format("Motion %d %s", play.motionId, motion.name.c_str());
    }
    return Format("Bank %d, motion %d", play.bank, play.motionId);
}

std::string FsmNodePath(const FsmData& fsm, int32_t index) {
    std::string path;
    for (int32_t at = index; at >= 0; at = fsm.nodes[static_cast<std::size_t>(at)].parent) {
        path = fsm.nodes[static_cast<std::size_t>(at)].name + (path.empty() ? "" : "/" + path);
    }
    return path;
}

std::string FsmNodeSummary(const FsmData& fsm, const FsmNode& node, FsmMotions* motions) {
    if (!node.children.empty()) return Format("%zu states", node.children.size());
    for (const FsmObjectRef& ref : node.actions) {
        const RszInstance* action = fsm.Instance(ref);
        if (action == nullptr) continue;
        if (std::optional<FsmPlayMotion> play = ReadPlayMotion(*action)) return FsmMotionText(motions, *play);
    }
    const RszInstance* first = node.actions.empty() ? nullptr : fsm.Instance(node.actions[0]);
    return first ? FsmTypeLabel(first->typeName) : std::string();
}

void OutlineFsm(OutlineWriter& out, const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath) {
    FsmData fsm = game.Readers().Get<FsmReader>()->Read(game.ExtractFile(pakPath), db);
    std::optional<FsmMotions> motionIndex;
    if (fsm.motion) motionIndex.emplace(game, pakPath);
    FsmMotions* motions = motionIndex ? &*motionIndex : nullptr;

    auto nodeProps = [&](int32_t index) {
        const FsmNode& node = fsm.nodes[static_cast<std::size_t>(index)];
        out.Prop("State", "Name", node.name);
        out.Prop("State", "Path", FsmNodePath(fsm, index));
        out.Prop("State", "ID", Format("%u (%u)", node.id, node.exId));
        out.Prop("State", "Priority", std::to_string(node.priority));
        std::string flags;
        static const std::pair<uint16_t, const char*> FLAGS[] = {
            { FSM_NODE_ENABLED, "Enabled" }, { FSM_NODE_RESTARTABLE, "Restartable" }, { FSM_NODE_REFERENCE_TREE, "ReferenceTree" },
            { FSM_NODE_BUBBLES_CHILD_END, "BubblesChildEnd" }, { FSM_NODE_SELECT_ONCE, "SelectOnce" },
            { FSM_NODE_FSM, "FSM" }, { FSM_NODE_TRAVERSE_TO_LEAF, "TraverseToLeaf" },
        };
        for (const auto& [bit, label] : FLAGS) {
            if (node.attributes & bit) flags += (flags.empty() ? "" : ", ") + std::string(label);
        }
        out.Prop("State", "Attributes", flags.empty() ? "-" : flags);
        if (node.isEnd) out.Prop("State", "End state", "true");
        if (node.isBranch) out.Prop("State", "Branch", "true");
        if (!node.tags.empty()) out.Prop("State", "Tags", std::to_string(node.tags.size()));
        if (!node.referenceTree.empty()) out.Prop("State", "Reference tree", node.referenceTree);
        if (const RszInstance* selector = fsm.Instance(node.selector)) out.Prop("State", "Selector", FsmTypeLabel(selector->typeName));

        for (std::size_t a = 0; a < node.actions.size(); a++) {
            const RszInstance* action = fsm.Instance(node.actions[a]);
            if (action == nullptr) continue;
            std::string section = Format("Action %zu: %s", a, FsmTypeLabel(action->typeName).c_str());
            if (std::optional<FsmPlayMotion> play = ReadPlayMotion(*action)) {
                out.Prop(section, "Motion", FsmMotionText(motions, *play));
                if (motions) {
                    FsmMotions::Motion motion = motions->Find(play->bank, play->motionId);
                    if (!motion.motlist.empty()) out.Prop(section, "Motion list", motion.motlist);
                }
                out.Prop(section, "Bank", std::to_string(play->bank));
                out.Prop(section, "Motion ID", std::to_string(play->motionId));
                out.Prop(section, "Speed", Format("%g", play->speed));
            }
            FsmInstanceProps(out, section, *action);
        }

        if (!node.states.empty()) {
            for (const FsmState& state : node.states) {
                std::string target = state.target >= 0 ? fsm.nodes[static_cast<std::size_t>(state.target)].name : "?";
                std::string blend = FsmBlendText(fsm, state.transitionData);
                out.Prop("Transitions", "-> " + target, FsmConditionText(fsm, state.condition) + (blend.empty() ? "" : ", " + blend));
            }
            for (const FsmState& state : node.states) {
                std::string target = state.target >= 0 ? fsm.nodes[static_cast<std::size_t>(state.target)].name : "?";
                std::string section = "To " + target;
                out.Prop(section, "Condition", FsmConditionText(fsm, state.condition));
                FsmTransitionProps(out, fsm, section, state.transitionData);
                if (const RszInstance* condition = fsm.Instance(state.condition)) {
                    for (const RszFieldValue& field : condition->fields) out.Prop(section, "Condition " + field.name, ValueText(field.value));
                }
                for (const FsmObjectRef& event : state.events) {
                    if (const RszInstance* instance = fsm.Instance(event)) out.Prop(section, "Event", FsmTypeLabel(instance->typeName));
                }
            }
        }
        for (const FsmStartTransition& start : node.transitions) {
            std::string child = start.start >= 0 ? fsm.nodes[static_cast<std::size_t>(start.start)].name : "(none)";
            out.Prop("Starts in", child, FsmConditionText(fsm, start.condition));
        }
        for (const FsmAllState& any : node.allStates) {
            std::string target = any.target >= 0 ? fsm.nodes[static_cast<std::size_t>(any.target)].name : "?";
            std::string blend = FsmBlendText(fsm, any.transitionData);
            out.Prop("From any state", "-> " + target, FsmConditionText(fsm, any.condition) + (blend.empty() ? "" : ", " + blend));
        }
    };

    std::size_t states = 0;
    for (const FsmNode& node : fsm.nodes) states += node.children.empty();
    std::string rootKey = fsm.root >= 0 ? Format("fsm:%d", fsm.root) : std::string();
    out.Node(0, "fsm", DisplayName(pakPath), Format("%s, %zu states", fsm.motion ? "Motion FSM" : "FSM", states), rootKey);
    out.Prop("FSM", "Path", pakPath);
    out.Prop("FSM", "Type", fsm.motion ? "Motion state machine (motfsm2)" : "State machine (fsmv2)");
    out.Prop("FSM", "Version", Format("%u (tree %u)", fsm.fileVersion, fsm.treeVersion));
    out.Prop("FSM", "Nodes", std::to_string(fsm.nodes.size()));
    if (fsm.motion) {
        out.Prop("FSM", "Blend settings", std::to_string(fsm.transitions.size()));
        out.Prop("FSM", "Motion bank", motions && !motions->Motbank().empty() ? motions->Motbank() : "(not found)");
    }
    for (const std::string& resource : fsm.resources) out.Prop("Resources", "", resource);
    if (fsm.root < 0) return;
    nodeProps(fsm.root);

    std::function<void(int32_t, int)> emit = [&](int32_t index, int depth) {
        const FsmNode& node = fsm.nodes[static_cast<std::size_t>(index)];
        for (const FsmChild& child : node.children) {
            if (child.node < 0 || depth > 32) continue;
            const FsmNode& state = fsm.nodes[static_cast<std::size_t>(child.node)];
            out.Node(depth, state.children.empty() ? "fsmstate" : "fsmgroup", state.name, FsmNodeSummary(fsm, state, motions),
                     Format("fsm:%d", child.node));
            nodeProps(child.node);
            emit(child.node, depth + 1);
        }
    };
    emit(fsm.root, 1);
}

void OutlineRenderTexture(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    std::vector<uint8_t> data = game.ExtractFile(pakPath);
    auto u32 = [&](std::size_t offset) {
        uint32_t value = 0;
        if (offset + 4 <= data.size()) std::memcpy(&value, data.data() + offset, 4);
        return value;
    };
    if (data.size() < 0x18 || u32(0) != 0x58455452) throw std::runtime_error("rtex: bad magic");
    uint32_t width = u32(0x10);
    uint32_t height = u32(0x14);
    out.Node(0, "rendertarget", DisplayName(pakPath), Format("%u x %u", width, height));
    out.Prop("Render Target", "Path", pakPath);
    out.Prop("Render Target", "Format", DxgiFormatName(u32(0x0C)));
    out.Prop("Render Target", "Size", Format("%u x %u", width, height));
    out.Prop("Render Target", "Version", std::to_string(u32(0x04)));
    out.Prop("Render Target", "Contents", "Drawn at runtime; the file only describes the target");
    // The remaining words are unknown.
    for (std::size_t offset = 0x18; offset + 4 <= data.size(); offset += 4) {
        out.Prop("Header", Format("0x%02zX", offset), Format("0x%08X", u32(offset)));
    }
}

// In "pat:<s>:<p>" keys, p is the global pattern index.
void OutlineUvs(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    const UvsReader* reader = game.Readers().Get<UvsReader>();
    if (reader == nullptr) throw std::runtime_error("uv sequences are not supported for " + game.Game().GetId());
    UvsData uvs = reader->Read(game.ExtractFile(pakPath));
    out.Node(0, "uvs", DisplayName(pakPath), Format("%zu sequences", uvs.sequences.size()));
    out.Prop("UV Sequence", "Path", pakPath);
    out.Prop("UV Sequence", "Sequences", std::to_string(uvs.sequences.size()));
    out.Prop("UV Sequence", "Patterns", std::to_string(uvs.patterns.size()));
    for (std::size_t i = 0; i < uvs.textures.size(); i++) out.Prop("Textures", Format("Texture %zu", i), uvs.textures[i]);

    for (std::size_t s = 0; s < uvs.sequences.size(); s++) {
        const UvsSequence& seq = uvs.sequences[s];
        out.Node(1, "sequence", Format("Sequence %zu", s), Format("%u patterns", seq.patternCount), Format("seq:%zu", s));
        out.Prop("Sequence", "Index", std::to_string(s));
        out.Prop("Sequence", "Patterns", std::to_string(seq.patternCount));
        out.Prop("Sequence", "First pattern", std::to_string(seq.firstPattern));
        for (uint32_t p = seq.firstPattern; p < seq.firstPattern + seq.patternCount && p < uvs.patterns.size(); p++) {
            const UvsPattern& pattern = uvs.patterns[p];
            const float* r = pattern.rect;
            out.Node(2, "pattern", Format("Pattern %u", p - seq.firstPattern),
                     Format("%.4g, %.4g  %.4g x %.4g", r[0], r[1], r[2] - r[0], r[3] - r[1]), Format("pat:%zu:%u", s, p));
            out.Prop("Pattern", "Index", std::to_string(p));
            out.Prop("Pattern", "UV rect", Format("%.6g, %.6g, %.6g, %.6g", r[0], r[1], r[2], r[3]));
            out.Prop("Pattern", "Texture", pattern.textureIndex < uvs.textures.size()
                         ? uvs.textures[pattern.textureIndex] : Format("#%u", pattern.textureIndex));
            out.Prop("Pattern", "Flags", Format("0x%08X", pattern.flags));
        }
    }
}

std::string FieldLabel(const RszTypeDatabase& db, const RszInstance& instance, std::size_t fieldIndex) {
    const std::string& name = instance.fields[fieldIndex].name;
    bool positional = name.size() >= 2 && name[0] == 'v' &&
                      std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    const RszTypeDef* type = db.Find(instance.typeId);
    if (!positional || type == nullptr || fieldIndex >= type->fields.size()) return name;
    const RszFieldDef& def = type->fields[fieldIndex];
    return name + " (" + def.type + (def.array ? "[]" : "") + ")";
}

void OutlineUserData(OutlineWriter& out, const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath) {
    constexpr int MAX_DEPTH = 24;
    constexpr std::size_t MAX_NODES = 20000;
    constexpr std::size_t MAX_ARRAY_TEXT = 12;

    UserFileData user = game.Readers().Get<UserReader>()->Read(game.ExtractFile(pakPath), db);
    const RszData& rsz = user.rsz;
    std::unordered_map<uint32_t, std::string> userDataPaths;
    for (const RszUserDataRef& ref : rsz.userData) userDataPaths[ref.instanceId] = ref.path;
    auto validInstance = [&](uint32_t index) { return index > 0 && index < rsz.instances.size(); };

    int32_t root = user.RootInstance();
    std::string rootType = root >= 0 ? rsz.instances[static_cast<std::size_t>(root)].typeName : std::string();
    out.Node(0, "userdata", DisplayName(pakPath), ShortType(rootType));
    out.Prop("User Data", "Path", pakPath);
    out.Prop("User Data", "Type", rootType.empty() ? "-" : rootType);
    out.Prop("User Data", "Instances", std::to_string(rsz.instances.empty() ? 0 : rsz.instances.size() - 1));
    for (std::size_t i = 0; i < user.resources.size(); i++) out.Prop("Resources", Format("Resource %zu", i), user.resources[i]);
    for (const RszUserDataRef& ref : user.userData) {
        const RszTypeDef* type = db.Find(ref.typeId);
        out.Prop("Referenced User Data", type ? ShortType(type->name) : Format("#%08X", ref.typeId), ref.path);
    }
    if (rsz.stoppedAt >= 0) {
        const RszInstance& stopped = rsz.instances[static_cast<std::size_t>(rsz.stoppedAt)];
        out.Prop("User Data", "Read up to", Format("instance %d (%s): %s", rsz.stoppedAt, ShortType(stopped.typeName).c_str(),
                                                   rsz.stopReason.c_str()));
    }
    if (root < 0) return;

    std::set<uint32_t> expanded;
    std::size_t nodes = 0;
    std::function<void(uint32_t, int)> emitFields;
    auto emitObject = [&](uint32_t index, int depth, const std::string& name) {
        const RszInstance& instance = rsz.instances[index];
        bool repeat = !expanded.insert(index).second;
        out.Node(depth, "userobject", name, ShortType(instance.typeName) + (repeat ? " (shown above)" : ""));
        out.Prop("Object", "Type", instance.typeName.empty() ? "-" : instance.typeName);
        out.Prop("Object", "Instance", std::to_string(index));
        if (!repeat && depth < MAX_DEPTH && ++nodes < MAX_NODES) emitFields(index, depth);
    };
    emitFields = [&](uint32_t index, int depth) {
        const RszInstance& instance = rsz.instances[index];
        std::string section = instance.typeName.empty() ? "Fields" : instance.typeName;
        if (instance.layoutMismatch) out.Prop(section, "Layout", "differs from the type dump; values may be misread");
        if (instance.fields.empty()) out.Prop(section, "", instance.parsed ? "(no fields)" : "(not parsed)");
        std::vector<std::pair<std::string, uint32_t>> children;
        std::vector<std::pair<std::string, std::vector<uint32_t>>> groups;
        auto objectText = [&](uint32_t target) {
            if (auto path = userDataPaths.find(target); path != userDataPaths.end()) return path->second;
            if (!validInstance(target)) return std::string("null");
            return "-> " + ShortType(rsz.instances[target].typeName);
        };
        for (std::size_t f = 0; f < instance.fields.size(); f++) {
            const RszValue& value = instance.fields[f].value;
            std::string label = FieldLabel(db, instance, f);
            if (const RszObjectRef* ref = value.As<RszObjectRef>()) {
                out.Prop(section, label, objectText(ref->index));
                if (validInstance(ref->index) && !userDataPaths.contains(ref->index)) children.emplace_back(label, ref->index);
            } else if (const RszArray* array = value.As<RszArray>()) {
                bool objects = !array->empty() && std::all_of(array->begin(), array->end(),
                    [](const RszValue& item) { return item.As<RszObjectRef>() != nullptr; });
                if (objects) {
                    std::vector<uint32_t> members;
                    for (std::size_t i = 0; i < array->size(); i++) {
                        uint32_t target = (*array)[i].As<RszObjectRef>()->index;
                        // .user references stay paths: the inspector opens them.
                        if (userDataPaths.contains(target)) out.Prop(section, Format("%s [%zu]", label.c_str(), i), objectText(target));
                        else if (validInstance(target)) members.push_back(target);
                    }
                    out.Prop(section, label, Format("[%zu objects]", array->size()));
                    if (!members.empty()) groups.emplace_back(label, std::move(members));
                } else {
                    std::string text;
                    for (std::size_t i = 0; i < array->size() && i < MAX_ARRAY_TEXT; i++) {
                        text += (i ? "; " : "") + ValueText((*array)[i]);
                    }
                    if (array->size() > MAX_ARRAY_TEXT) text += "; ...";
                    out.Prop(section, label, Format("[%zu] ", array->size()) + text);
                }
            } else {
                out.Prop(section, label, ValueText(value));
            }
        }
        for (const auto& [label, target] : children) emitObject(target, depth + 1, label);
        for (const auto& [label, members] : groups) {
            out.Node(depth + 1, "group", label, Format("%zu items", members.size()));
            for (std::size_t i = 0; i < members.size() && nodes < MAX_NODES; i++) {
                emitObject(members[i], depth + 2, Format("[%zu] ", i) + ShortType(rsz.instances[members[i]].typeName));
            }
        }
    };
    expanded.insert(static_cast<uint32_t>(root));
    emitFields(static_cast<uint32_t>(root), 0);
    // The root unread: what was read is only reachable this way.
    if (rsz.instances[static_cast<std::size_t>(root)].fields.empty()) {
        for (uint32_t i = 1; i < rsz.instances.size() && nodes < MAX_NODES; i++) {
            const RszInstance& instance = rsz.instances[i];
            if (!instance.fields.empty() && !expanded.contains(i)) emitObject(i, 1, Format("Instance %u", i));
        }
    }
}

void OutlineMotlist(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    MotlistData list = game.Readers().Get<MotlistReader>()->Read(game.ExtractFile(pakPath));
    std::string name = DisplayName(pakPath);
    out.Node(0, "motlist", name, Format("%zu motions", list.motions.size()));
    out.Prop("Motion List", "Name", list.name);
    out.Prop("Motion List", "Version", std::to_string(list.version));
    out.Prop("Motion List", "Motions", std::to_string(list.motions.size()));
    out.Prop("Motion List", "Base list", list.baseMotListPath.empty() ? "-" : list.baseMotListPath);
    for (const std::string& warning : list.warnings) out.Prop("Warnings", "", warning);

    for (std::size_t i = 0; i < list.motions.size(); i++) {
        const MotData& mot = list.motions[i];
        out.Node(1, "motion", mot.name, Format("%g frames", mot.frameCount));
        out.Prop("Motion", "Index", std::to_string(i));
        out.Prop("Motion", "Version", std::to_string(mot.version));
        out.Prop("Motion", "Frames", Format("%g", mot.frameCount));
        out.Prop("Motion", "Frame rate", std::to_string(mot.frameRate));
        out.Prop("Motion", "Range", Format("%g .. %g", mot.startFrame, mot.endFrame));
        out.Prop("Motion", "Bone clips", std::to_string(mot.boneClips.size()));
        if (!mot.jointMapPath.empty()) out.Prop("Motion", "Joint map", mot.jointMapPath);
        std::size_t shown = 0;
        for (const MotBoneClip& clip : mot.boneClips) {
            if (shown++ >= MAX_BONE_CLIPS) break;
            std::string tracks;
            if (clip.translation) tracks += Format("T:%zu ", clip.translation->KeyCount());
            if (clip.rotation) tracks += Format("R:%zu ", clip.rotation->KeyCount());
            if (clip.scale) tracks += Format("S:%zu", clip.scale->KeyCount());
            out.Prop("Bone clips", clip.boneName.empty() ? Format("#%08X", clip.boneHash) : clip.boneName, tracks);
        }
    }
}

std::string GuidText(const std::array<uint8_t, 16>& g) {
    // .NET Guid layout: the first three groups are little-endian.
    return Format("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2], g[1], g[0], g[5], g[4],
                  g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

void OutlineMessages(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath) {
    MessageData msg = game.Readers().Get<MsgReader>()->Read(game.ExtractFile(pakPath));
    out.Node(0, "messages", DisplayName(pakPath), Format("%zu entries", msg.entries.size()));
    out.Prop("Message File", "Path", pakPath);
    out.Prop("Message File", "Version", std::to_string(msg.version));
    out.Prop("Message File", "Entries", std::to_string(msg.entries.size()));
    out.Prop("Message File", "Languages", std::to_string(msg.languages.size()));
    static const char* TYPES[] = { "int", "float", "string" };
    for (const MessageAttribute& attribute : msg.attributes) {
        out.Prop("Attributes", attribute.name.empty() ? "(unnamed)" : attribute.name,
                 attribute.type >= 0 && attribute.type <= 2 ? TYPES[attribute.type] : "empty");
    }
}

// Depth first through each element's container; index is the N of "gui:N" keys.
void WalkGui(const GuiData& gui, const std::function<void(const GuiElement&, int, std::size_t)>& visit) {
    constexpr int MAX_GUI_DEPTH = 64;
    std::size_t next = 0;
    std::function<void(const GuiElement&, int)> walk = [&](const GuiElement& e, int depth) {
        visit(e, depth, next++);
        const GuiContainer* container = gui.FindContainer(e.containerId);
        if (container == nullptr || depth >= MAX_GUI_DEPTH) return;
        for (const GuiElement& child : container->elements) walk(child, depth + 1);
    };
    walk(gui.view, 1);
}

// Booleans read as true/false so the inspector shows them as checkboxes.
std::string GuiValueText(const GuiValue& value) {
    if (value.type == 0x01) return value.Bool() ? "true" : "false";
    return value.ToString();
}

void OutlineGui(OutlineWriter& out, const LoadedGame& game, const std::string& pakPath, const MessageIndex* messages) {
    constexpr int ENGLISH = 1;
    GuiData gui = game.Readers().Get<GuiReader>()->Read(game.ExtractFile(pakPath));
    std::size_t elements = 0;
    for (const GuiContainer& container : gui.containers) elements += container.elements.size();
    out.Node(0, "gui", DisplayName(pakPath), Format("%zu elements", elements));
    out.Prop("GUI", "Path", pakPath);
    out.Prop("GUI", "Version", std::to_string(gui.version));
    if (const GuiAttribute* size = gui.view.Find("ScreenSize")) out.Prop("GUI", "Screen size", size->value.ToString());
    for (std::size_t i = 0; i < gui.resources.size(); i++) out.Prop("Resources", Format("Resource %zu", i), gui.resources[i]);
    for (std::size_t i = 0; i < gui.linkedGuis.size(); i++) out.Prop("Linked GUIs", Format("GUI %zu", i), gui.linkedGuis[i]);
    for (const GuiAttributeOverride& o : gui.overrides) {
        out.Prop("Attribute Overrides", o.targetPath + " " + o.attribute.name, GuiValueText(o.attribute.value));
    }
    for (const GuiParameter& p : gui.parameters) out.Prop("Parameters", p.name, GuiValueText(p.value));

    WalkGui(gui, [&](const GuiElement& e, int depth, std::size_t index) {
        std::string type = e.className.starts_with("via.gui.") ? e.className.substr(8) : e.className;
        out.Node(depth, "guielement", e.name.empty() ? type : e.name, type, Format("gui:%zu", index));
        out.Prop("Element", "Class", e.className);
        out.Prop("Element", "ID", GuiIdText(e.id));
        for (const GuiAttribute& a : e.attributes) out.Prop(type, a.name, GuiValueText(a.value));
        if (const GuiAttribute* id = e.Find("MessageId"); id && messages != nullptr) {
            std::string text = messages->Text(id->value.text, ENGLISH);
            if (!text.empty()) out.Prop(type, "Message text", text);
        }
        for (const GuiAttribute& a : e.extraAttributes) out.Prop("Keyed Values", a.name, GuiValueText(a.value));
        auto dataInt = [&](std::size_t at) {
            int32_t v = 0;
            if (at + 4 <= e.data.size()) std::memcpy(&v, e.data.data() + at, 4);
            return v;
        };
        auto dataFloat = [&](std::size_t at) {
            float v = 0;
            if (at + 4 <= e.data.size()) std::memcpy(&v, e.data.data() + at, 4);
            return v;
        };
        // Sequence numbers carry a high flag bit; the rest is the index.
        auto sequence = [&](std::size_t at) { return dataInt(at) & 0x7FFFFFFF; };
        if (e.className == "via.gui.TextureSet") {
            // 24-byte regions: sequence, pattern, then left, top, right, bottom in local units.
            for (std::size_t at = 0, i = 0; at + 24 <= e.data.size(); at += 24, i++) {
                out.Prop("Regions", Format("Region %zu", i),
                         Format("%d, %d, %g, %g, %g, %g", sequence(at), dataInt(at + 4), dataFloat(at + 8), dataFloat(at + 12),
                                dataFloat(at + 16), dataFloat(at + 20)));
            }
        } else if (e.className == "via.gui.Scale9Grid" && e.data.size() >= 88) {
            // Border widths (left, top, right, bottom), then the nine cells' sequence and pattern, row by row.
            out.Prop("Grid", "Border", Format("%g, %g, %g, %g", dataFloat(0), dataFloat(4), dataFloat(8), dataFloat(12)));
            for (std::size_t cell = 0; cell < 9; cell++) {
                out.Prop("Grid", Format("Cell %zu", cell), Format("%d, %d", sequence(16 + cell * 8), dataInt(20 + cell * 8)));
            }
        }
        if (const GuiContainer* container = gui.FindContainer(e.containerId)) {
            for (const GuiClipInfo& clip : container->clips) {
                out.Prop("Clips", clip.name,
                         clip.frames > 0 ? Format("%g frames%s", clip.frames, clip.loop ? ", loops" : "") : "static");
            }
        }
    });
}

}

std::string OutlineAsset(const LoadedGame& game, const RszTypeDatabase* rsz, const std::string& pakPath,
                         const MessageIndex* messages) {
    OutlineWriter out;
    std::string name = DisplayName(pakPath);
    if (pakPath.find(".scn.") != std::string::npos) {
        if (rsz == nullptr) throw std::runtime_error("scene outline needs the RSZ type database");
        out.Node(0, "scene", name, pakPath);
        out.Prop("Scene", "Path", pakPath);
        std::set<std::string> visited;
        OutlineScene(out, game, *rsz, pakPath, 1, 0, visited);
    } else if (pakPath.find(".pfb.") != std::string::npos) {
        if (rsz == nullptr) throw std::runtime_error("prefab outline needs the RSZ type database");
        SceneData pfb = ReadSceneOrPrefab(game, *rsz, pakPath);
        out.Node(0, "prefab", name, Format("%zu game objects", pfb.gameObjects.size()));
        out.Prop("Prefab", "Path", pakPath);
        out.Prop("Prefab", "Game objects", std::to_string(pfb.gameObjects.size()));
        for (std::size_t i = 0; i < pfb.resources.size(); i++) out.Prop("Resources", Format("Resource %zu", i), pfb.resources[i]);
        for (const RszUserDataRef& ref : pfb.sceneUserData) {
            const RszTypeDef* type = rsz->Find(ref.typeId);
            out.Prop("Referenced User Data", type ? ShortType(type->name) : Format("#%08X", ref.typeId), ref.path);
        }
        if (pfb.rsz.stoppedAt >= 0) {
            const RszInstance& stopped = pfb.rsz.instances[static_cast<std::size_t>(pfb.rsz.stoppedAt)];
            out.Prop("Prefab", "Read up to", Format("instance %d (%s): %s", pfb.rsz.stoppedAt,
                                                    ShortType(stopped.typeName).c_str(), pfb.rsz.stopReason.c_str()));
        }
        std::set<std::string> visited;
        OutlineScene(out, game, *rsz, pakPath, 1, 0, visited);
    } else if (pakPath.find(".mesh.") != std::string::npos) {
        OutlineMesh(out, game, pakPath);
    } else if (pakPath.find(".mdf2.") != std::string::npos) {
        MaterialData mdf = game.Readers().Get<MdfReader>()->Read(game.ExtractFile(pakPath));
        out.Node(0, "mdf", name, Format("%zu materials", mdf.materials.size()));
        out.Prop("Material File", "Path", pakPath);
        out.Prop("Material File", "Version", std::to_string(mdf.version));
        out.Prop("Material File", "Materials", std::to_string(mdf.materials.size()));
        OutlineMaterials(out, mdf, pakPath, 1);
    } else if (pakPath.find(".motlist.") != std::string::npos) {
        OutlineMotlist(out, game, pakPath);
    } else if (pakPath.find(".bnk.") != std::string::npos) {
        OutlineSoundBank(out, game, pakPath);
    } else if (pakPath.find(".pck.") != std::string::npos) {
        OutlineSoundPackage(out, game, pakPath);
    } else if (pakPath.find(".efx.") != std::string::npos) {
        OutlineEffect(out, game, pakPath);
    } else if (pakPath.find(".tex.") != std::string::npos) {
        OutlineTexture(out, game, pakPath);
    } else if (pakPath.find(".uvs.") != std::string::npos) {
        OutlineUvs(out, game, pakPath);
    } else if (pakPath.find(".rtex.") != std::string::npos) {
        OutlineRenderTexture(out, game, pakPath);
    } else if (pakPath.find(".mcol.") != std::string::npos) {
        OutlineCollision(out, game, pakPath);
    } else if (game.Readers().Get<AimpReader>() && game.Readers().Get<AimpReader>()->SupportsPath(pakPath)) {
        OutlineAiMap(out, game, pakPath);
    } else if (game.Readers().Get<FsmReader>() && game.Readers().Get<FsmReader>()->SupportsPath(pakPath)) {
        if (rsz == nullptr) throw std::runtime_error("FSM outline needs the RSZ type database");
        OutlineFsm(out, game, *rsz, pakPath);
    } else if (pakPath.find(".motbank.") != std::string::npos) {
        OutlineMotbank(out, game, pakPath);
    } else if (pakPath.find(".user.") != std::string::npos) {
        if (rsz == nullptr) throw std::runtime_error("user data outline needs the RSZ type database");
        OutlineUserData(out, game, *rsz, pakPath);
    } else if (pakPath.find(".msg.") != std::string::npos) {
        OutlineMessages(out, game, pakPath);
    } else if (pakPath.find(".gui.") != std::string::npos) {
        OutlineGui(out, game, pakPath, messages);
    } else {
        throw std::runtime_error("no outline for " + name);
    }
    return std::move(out.text);
}

std::string FsmGraph(const LoadedGame& game, const RszTypeDatabase& db, const std::string& pakPath) {
    FsmData fsm = game.Readers().Get<FsmReader>()->Read(game.ExtractFile(pakPath), db);
    std::optional<FsmMotions> motionIndex;
    if (fsm.motion) motionIndex.emplace(game, pakPath);
    FsmMotions* motions = motionIndex ? &*motionIndex : nullptr;
    std::string text;
    auto record = [&](std::initializer_list<std::string> fields) {
        bool first = true;
        for (const std::string& field : fields) {
            if (!first) text += '\t';
            first = false;
            for (char c : field) text += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
        }
        text += '\n';
    };
    for (std::size_t i = 0; i < fsm.nodes.size(); i++) {
        const FsmNode& node = fsm.nodes[i];
        record({ "N", std::to_string(i), std::to_string(node.parent), node.name, node.children.empty() ? "0" : "1",
                 FsmNodeSummary(fsm, node, motions), node.isEnd ? "1" : "0" });
    }
    for (std::size_t i = 0; i < fsm.nodes.size(); i++) {
        const FsmNode& node = fsm.nodes[i];
        auto label = [&](const FsmObjectRef& condition, int32_t transitionData) {
            std::string blend = FsmBlendText(fsm, transitionData);
            return FsmConditionText(fsm, condition) + (blend.empty() ? "" : ", " + blend);
        };
        for (const FsmState& state : node.states) {
            if (state.target >= 0) record({ "S", std::to_string(i), std::to_string(state.target), label(state.condition, state.transitionData) });
        }
        for (const FsmStartTransition& start : node.transitions) {
            if (start.start >= 0) record({ "T", std::to_string(i), std::to_string(start.start), FsmConditionText(fsm, start.condition) });
        }
        for (const FsmAllState& any : node.allStates) {
            if (any.target >= 0) record({ "A", std::to_string(i), std::to_string(any.target), label(any.condition, any.transitionData) });
        }
    }
    return text;
}

std::string MessageTable(const LoadedGame& game, const std::string& pakPath) {
    MessageData msg = game.Readers().Get<MsgReader>()->Read(game.ExtractFile(pakPath));
    std::string text;
    auto field = [&](std::string_view value) {
        text += '\t';
        for (char c : value) {
            switch (c) {
            case '\t': text += "\\t"; break;
            case '\n': text += "\\n"; break;
            case '\r': text += "\\r"; break;
            case '\\': text += "\\\\"; break;
            default: text += c;
            }
        }
    };
    for (int32_t language : msg.languages) {
        text += 'L';
        field(std::to_string(language));
        field(MessageLanguageName(language));
        text += '\n';
    }
    for (const MessageAttribute& attribute : msg.attributes) {
        text += 'A';
        field(attribute.name);
        field(std::to_string(attribute.type));
        text += '\n';
    }
    for (const MessageEntry& entry : msg.entries) {
        text += 'E';
        field(entry.name);
        field(GuidText(entry.guid));
        field(Format("%08X", entry.hash));
        for (const std::string& value : entry.attributes) field(value);
        for (const std::string& value : entry.texts) field(value);
        text += '\n';
    }
    return text;
}

std::string IdentifyUnknown(const LoadedGame& game, const std::string& unknownPath) {
    std::vector<uint8_t> data = game.ExtractFile(unknownPath);
    if (MsgReader::IsMessage(data)) return unknownPath + ".msg." + std::to_string(MemoryReader(data).ReadAt<uint32_t>(0));
    return unknownPath;
}

std::string GuiClipTable(const LoadedGame& game, const std::string& pakPath) {
    GuiData gui = game.Readers().Get<GuiReader>()->Read(game.ExtractFile(pakPath));
    std::string text;
    auto field = [&](std::string_view value) {
        text += '\t';
        for (char c : value) text += c == '\t' || c == '\n' || c == '\r' || c == '\x1e' || c == '\x1f' ? ' ' : c;
    };
    WalkGui(gui, [&](const GuiElement& e, int, std::size_t index) {
        const GuiContainer* container = gui.FindContainer(e.containerId);
        if (container == nullptr || container->clips.empty()) return;
        text += 'E';
        field(Format("gui:%zu", index));
        text += '\n';
        for (const GuiClipInfo& clip : container->clips) {
            text += 'C';
            field(clip.name);
            field(Format("%g", clip.frames));
            field(clip.loop ? "1" : "0");
            field(clip.next);
            field(std::to_string(clip.pattern));
            text += '\n';
            for (const GuiClipTrack& track : clip.tracks) {
                text += 'T';
                field(track.name);
                field(track.root ? "1" : "0");
                text += '\n';
                for (const GuiClipCurve& curve : track.curves) {
                    text += 'V';
                    field(curve.attribute);
                    field(curve.component);
                    field(std::to_string(curve.attributeType));
                    std::string keys;
                    for (const GuiClipKey& key : curve.keys) {
                        if (!keys.empty()) keys += '\x1e';
                        keys += Format("%g\x1f%u\x1f%g\x1f%g\x1f%g\x1f%g\x1f", key.frame, key.interpolation, key.handles[0],
                                       key.handles[1], key.handles[2], key.handles[3]);
                        std::string value = key.value.text.empty() && key.value.count > 0 ? Format("%.9g", key.value.numbers[0]) : key.value.text;
                        for (char c : value) keys += c == '\t' || c == '\n' || c == '\r' || c == '\x1e' || c == '\x1f' ? ' ' : c;
                    }
                    text += '\t';
                    text += keys;
                    text += '\n';
                }
            }
        }
    });
    return text;
}
