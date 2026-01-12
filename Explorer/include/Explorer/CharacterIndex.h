#ifndef REASSETEXPLORER_CHARACTERINDEX_H
#define REASSETEXPLORER_CHARACTERINDEX_H
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Core/LoadedGame.h"
#include "Core/Rsz/RszTypeDatabase.h"
#include "Renderer/RenderMath.h"

struct CharacterPart {
    std::string name;
    std::string mesh;         // pak path
    std::string material;     // pak path, empty when the component names none
    int32_t parent = -1;      // index of the part it hangs from; -1 for the root part
    Mat4 local;               // relative to the parent part (or to its joint)
    std::string parentJoint;  // via.Transform ParentJointName: rides that joint of the parent
    bool sameJoints = false;  // via.Transform SameJointsConstraint: joints follow the parent's by name
    std::string motbank;      // its own motion bank (RE7 heads: facial motions)
};

// parts[0] is the root.
struct CharacterAssembly {
    std::string source;  // first scene or prefab it was found in
    std::string name;
    std::string motbank;
    std::string jointMap;
    uint32_t uses = 1;   // scenes/prefabs putting it together the same way
    std::vector<CharacterPart> parts;
};

// One per line:
//   P  name  mesh  material  parent  sameJoints  parentJoint  motbank  local (16 floats)
std::string CharacterPartsText(const std::vector<CharacterPart>& parts);
std::vector<CharacterPart> ParseCharacterParts(std::string_view text);

// Cached on disk: the scan reads a few thousand scenes and prefabs.
class CharacterIndex {
public:
    // Loads the cache when it matches the game's paks, else scans and writes it.
    static CharacterIndex Load(const LoadedGame& game, const RszTypeDatabase& db, const std::filesystem::path& cacheFile);

    // meshPakPath: any case.
    std::vector<const CharacterAssembly*> WithMesh(const std::string& meshPakPath) const;

private:
    std::vector<CharacterAssembly> assemblies;
    std::multimap<std::string, std::size_t> byMesh;  // lowercase mesh pak path -> assembly

    void IndexMeshes();
    bool ReadCache(const std::filesystem::path& file, const std::string& stamp);
    void WriteCache(const std::filesystem::path& file, const std::string& stamp) const;
};

#endif
