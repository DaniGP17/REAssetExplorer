#ifndef REASSETEXPLORER_RSZTYPEDATABASE_H
#define REASSETEXPLORER_RSZTYPEDATABASE_H
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct RszFieldDef {
    std::string name;
    std::string type;
    uint32_t size;
    uint32_t align;
    bool array;
};

struct RszTypeDef {
    std::string name;
    uint32_t crc = 0;  // layout hash
    std::vector<RszFieldDef> fields;
};

class RszTypeDatabase {
public:
    void LoadFromFile(const std::filesystem::path& path);

    const RszTypeDef* Find(uint32_t typeId) const {
        auto it = types.find(typeId);
        return it != types.end() ? &it->second : nullptr;
    }


private:
    std::unordered_map<uint32_t, RszTypeDef> types;
};

#endif
