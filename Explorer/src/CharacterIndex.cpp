#include "Explorer/CharacterIndex.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

#include "Explorer/Log.h"
#include "Explorer/SceneBuilder.h"

namespace {

constexpr const char* CACHE_VERSION = "characters v1";

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

const RszInstance* Component(const SceneData& scn, const SceneNode& node, std::string_view type) {
    for (int32_t index : node.componentIndices) {
        if (index >= 0 && static_cast<std::size_t>(index) < scn.rsz.instances.size() &&
            scn.rsz.instances[index].typeName == type) return &scn.rsz.instances[index];
    }
    return nullptr;
}

std::string MeshPath(const SceneData& scn, const SceneNode& node) {
    const RszInstance* mesh = Component(scn, node, "via.render.Mesh");
    const RszValue* path = mesh ? mesh->Field("v2") : nullptr;
    return path ? path->AsString() : std::string();
}

// via.motion.Motion field order differs between games: find the paths by extension.
std::string MotionPath(const RszInstance* motion, std::string_view extension) {
    if (motion == nullptr) return {};
    for (const RszFieldValue& field : motion->fields) {
        std::string text = field.value.AsString();
        if (text.size() > extension.size() && Lower(text).ends_with(extension)) return text;
    }
    return {};
}

Mat4 LocalMatrix(const SceneNode& node) {
    float t[3] = { node.position.x, node.position.y, node.position.z };
    float q[4] = { node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w };
    float s[3] = { node.scale.x, node.scale.y, node.scale.z };
    return ComposeTRS(t, q, s);
}

std::string Signature(const CharacterAssembly& assembly) {
    std::string text = Lower(assembly.motbank);
    for (const CharacterPart& part : assembly.parts) {
        text += '|' + Lower(part.mesh) + '>' + Lower(part.material) + '>' + std::to_string(part.parent) + '>' + part.parentJoint +
                (part.sameJoints ? ">s" : ">");
    }
    return text;
}

// Game objects with a mesh and a Motion whose descendants hold more meshes;
// the outermost one wins, so a head with its eyes stays a part of the body.
void ScanScene(const LoadedGame& game, const SceneData& scn, const std::string& source, std::vector<CharacterAssembly>& out,
               std::map<std::string, std::size_t>& bySignature) {
    std::string meshExt = game.Game().GetMeshExt();
    std::string mdfExt = game.Game().GetMdfExt();
    std::function<bool(std::size_t)> hasMeshBelow = [&](std::size_t index) {
        for (std::size_t child : scn.nodes[index].children) {
            if (!MeshPath(scn, scn.nodes[child]).empty() || hasMeshBelow(child)) return true;
        }
        return false;
    };
    std::function<void(std::size_t)> walk = [&](std::size_t index) {
        const SceneNode& node = scn.nodes[index];
        const RszInstance* motion = Component(scn, node, "via.motion.Motion");
        if (node.kind != SceneNode::Kind::GameObject || motion == nullptr || MeshPath(scn, node).empty() || !hasMeshBelow(index)) {
            for (std::size_t child : node.children) walk(child);
            return;
        }
        CharacterAssembly assembly;
        assembly.source = source;
        assembly.name = node.name;
        assembly.motbank = MotionPath(motion, ".motbank");
        assembly.jointMap = MotionPath(motion, ".jmap");
        std::function<void(std::size_t, int32_t, const Mat4&)> add = [&](std::size_t at, int32_t parentPart, const Mat4& local) {
            const SceneNode& object = scn.nodes[at];
            std::string mesh = MeshPath(scn, object);
            int32_t childParent = parentPart;
            if (!mesh.empty()) {
                CharacterPart part;
                part.name = object.name;
                part.mesh = ToPakPath(mesh, meshExt.c_str());
                const RszInstance* meshComponent = Component(scn, object, "via.render.Mesh");
                const RszValue* mdf = meshComponent ? meshComponent->Field("v3") : nullptr;
                if (mdf && !mdf->AsString().empty()) part.material = ToPakPath(mdf->AsString(), mdfExt.c_str());
                part.parent = parentPart;
                part.local = local;
                part.parentJoint = object.parentJoint;
                part.sameJoints = object.sameJoints;
                if (parentPart >= 0) part.motbank = MotionPath(Component(scn, object, "via.motion.Motion"), ".motbank");
                childParent = static_cast<int32_t>(assembly.parts.size());
                assembly.parts.push_back(std::move(part));
            }
            for (std::size_t child : object.children) {
                Mat4 childLocal = Mul(LocalMatrix(scn.nodes[child]), mesh.empty() ? local : Identity());
                add(child, childParent, childLocal);
            }
        };
        add(index, -1, Identity());
        std::string signature = Signature(assembly);
        auto [it, inserted] = bySignature.emplace(signature, out.size());
        if (inserted) out.push_back(std::move(assembly));
        else out[it->second].uses++;
    };
    for (std::size_t root : scn.roots) walk(root);
}

std::string MatrixText(const Mat4& m) {
    std::string text;
    char buffer[32];
    for (int i = 0; i < 16; i++) {
        std::snprintf(buffer, sizeof(buffer), i ? " %.9g" : "%.9g", m.m[i]);
        text += buffer;
    }
    return text;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

}

std::string CharacterPartsText(const std::vector<CharacterPart>& parts) {
    std::string text;
    for (const CharacterPart& part : parts) {
        text += "P\t" + part.name + '\t' + part.mesh + '\t' + part.material + '\t' + std::to_string(part.parent) + '\t' +
                (part.sameJoints ? "1" : "0") + '\t' + part.parentJoint + '\t' + part.motbank + '\t' + MatrixText(part.local) + '\n';
    }
    return text;
}

std::vector<CharacterPart> ParseCharacterParts(std::string_view text) {
    std::vector<CharacterPart> parts;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        std::string line(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        start = end == std::string_view::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f = SplitTabs(line);
        if (f[0] != "P" || f.size() < 9) continue;
        CharacterPart part;
        part.name = f[1];
        part.mesh = f[2];
        part.material = f[3];
        part.parent = std::atoi(f[4].c_str());
        part.sameJoints = f[5] == "1";
        part.parentJoint = f[6];
        part.motbank = f[7];
        std::istringstream values(f[8]);
        for (float& v : part.local.m) values >> v;
        parts.push_back(std::move(part));
    }
    return parts;
}

CharacterIndex CharacterIndex::Load(const LoadedGame& game, const RszTypeDatabase& db, const std::filesystem::path& cacheFile) {
    CharacterIndex index;
    std::size_t entries = 0;
    for (const PakFile& pak : game.Paks()) entries += pak.Entries().size();
    std::string stamp = std::string(CACHE_VERSION) + " " + game.Game().GetId() + " " + std::to_string(entries);
    if (!cacheFile.empty() && index.ReadCache(cacheFile, stamp)) {
        index.IndexMeshes();
        return index;
    }

    std::map<std::string, std::size_t> bySignature;
    std::size_t files = 0;
    for (const PakFile& pak : game.Paks()) {
        for (const PakEntry& entry : pak.Entries()) {
            const std::string& path = entry.filePath;
            if (path.find(".scn.") == std::string::npos && path.find(".pfb.") == std::string::npos) continue;
            SceneData scn;
            try {
                scn = ReadSceneOrPrefab(game, db, path);
            } catch (const std::exception&) {
                continue;
            }
            files++;
            ScanScene(game, scn, path, index.assemblies, bySignature);
        }
    }
    std::stable_sort(index.assemblies.begin(), index.assemblies.end(),
                     [](const CharacterAssembly& a, const CharacterAssembly& b) { return a.uses > b.uses; });
    LogInfo("characters: %zu assemblies in %zu scenes and prefabs", index.assemblies.size(), files);
    index.IndexMeshes();
    if (!cacheFile.empty()) index.WriteCache(cacheFile, stamp);
    return index;
}

std::vector<const CharacterAssembly*> CharacterIndex::WithMesh(const std::string& meshPakPath) const {
    std::vector<const CharacterAssembly*> found;
    auto [first, last] = byMesh.equal_range(Lower(meshPakPath));
    for (auto it = first; it != last; ++it) {
        const CharacterAssembly* assembly = &assemblies[it->second];
        if (std::find(found.begin(), found.end(), assembly) == found.end()) found.push_back(assembly);
    }
    std::stable_sort(found.begin(), found.end(), [](const CharacterAssembly* a, const CharacterAssembly* b) { return a->uses > b->uses; });
    return found;
}

void CharacterIndex::IndexMeshes() {
    byMesh.clear();
    for (std::size_t i = 0; i < assemblies.size(); i++) {
        for (const CharacterPart& part : assemblies[i].parts) byMesh.emplace(Lower(part.mesh), i);
    }
}

// One record per line (P lines as in CharacterPartsText):
//   C  source  name  motbank  jointMap  uses
bool CharacterIndex::ReadCache(const std::filesystem::path& file, const std::string& stamp) {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line) || line != stamp) return false;
    while (std::getline(in, line)) {
        std::vector<std::string> f = SplitTabs(line);
        if (f[0] == "C" && f.size() >= 6) {
            CharacterAssembly assembly;
            assembly.source = f[1];
            assembly.name = f[2];
            assembly.motbank = f[3];
            assembly.jointMap = f[4];
            assembly.uses = static_cast<uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10));
            assemblies.push_back(std::move(assembly));
        } else if (f[0] == "P" && !assemblies.empty()) {
            for (CharacterPart& part : ParseCharacterParts(line)) assemblies.back().parts.push_back(std::move(part));
        }
    }
    return true;
}

void CharacterIndex::WriteCache(const std::filesystem::path& file, const std::string& stamp) const {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        LogWarning("characters: cannot write %s", file.string().c_str());
        return;
    }
    out << stamp << '\n';
    for (const CharacterAssembly& assembly : assemblies) {
        out << "C\t" << assembly.source << '\t' << assembly.name << '\t' << assembly.motbank << '\t' << assembly.jointMap << '\t'
            << assembly.uses << '\n';
        out << CharacterPartsText(assembly.parts);
    }
}
