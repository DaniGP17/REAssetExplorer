#ifndef REASSETEXPLORER_MATERIALDATA_H
#define REASSETEXPLORER_MATERIALDATA_H
#include <cstdint>
#include <string>
#include <vector>

struct MaterialTexture {
    std::string type;
    std::string path;
};

struct MaterialProperty {
    std::string name;
    uint32_t dataOffset;
    std::vector<float> values;
};

struct MaterialEntry {
    std::string name;
    std::string masterMaterialPath;
    uint32_t shaderType;
    uint32_t flags;
    uint32_t propertyBlockSize;
    std::vector<MaterialTexture> textures;
    std::vector<MaterialProperty> properties;

    bool AlphaTest() const { return (flags & 0x2) != 0; }
    // BaseTwoSideEnable: the engine draws these without back-face culling.
    bool TwoSided() const { return (flags & 0x1) != 0; }
};

struct MaterialData {
    uint16_t version;
    std::vector<MaterialEntry> materials;
};

#endif
