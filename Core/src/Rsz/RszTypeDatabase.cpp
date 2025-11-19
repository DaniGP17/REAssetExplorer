#include "Core/Rsz/RszTypeDatabase.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

void RszTypeDatabase::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("RszTypeDatabase: couldn't open " + path.string());
    }

    nlohmann::json root = nlohmann::json::parse(file);
    types.clear();
    types.reserve(root.size());

    for (const auto& [key, entry] : root.items()) {
        uint32_t typeId = static_cast<uint32_t>(std::stoul(key, nullptr, 16));

        RszTypeDef def;
        def.name = entry.value("name", "");
        def.crc = static_cast<uint32_t>(std::stoul(entry.value("crc", std::string("0")), nullptr, 16));

        if (auto fields = entry.find("fields"); fields != entry.end()) {
            def.fields.reserve(fields->size());
            for (const auto& field : *fields) {
                def.fields.push_back({
                    field.value("name", ""),
                    field.value("type", ""),
                    field.value("size", 0u),
                    field.value("align", 1u),
                    field.value("array", false),
                });
            }
        }

        types.emplace(typeId, std::move(def));
    }
}
