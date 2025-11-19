#ifndef REASSETEXPLORER_RSZTYPES_H
#define REASSETEXPLORER_RSZTYPES_H
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct RszVec3 {
    float x, y, z;
};

struct RszVec4 {
    float x, y, z, w;
};

struct RszGuid {
    std::array<uint8_t, 16> bytes{};
};

struct RszObjectRef {
    uint32_t index;
};

struct RszValue;
using RszArray = std::vector<RszValue>;
using RszBytes = std::vector<uint8_t>;

struct RszValue {
    std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string,
                 RszVec3, RszVec4, RszGuid, RszObjectRef, RszBytes, RszArray> data;

    template <typename T>
    const T* As() const { return std::get_if<T>(&data); }

    bool AsBool() const {
        if (auto* b = std::get_if<bool>(&data)) return *b;
        return false;
    }

    std::string AsString() const {
        if (auto* s = std::get_if<std::string>(&data)) return *s;
        return {};
    }

    RszVec3 AsVec3() const {
        if (auto* v = std::get_if<RszVec3>(&data)) return *v;
        if (auto* v = std::get_if<RszVec4>(&data)) return { v->x, v->y, v->z };
        if (auto* b = std::get_if<RszBytes>(&data); b && b->size() >= 12) {
            RszVec3 v;
            std::memcpy(&v, b->data(), 12);
            return v;
        }
        return { 0, 0, 0 };
    }

    RszVec4 AsVec4() const {
        if (auto* v = std::get_if<RszVec4>(&data)) return *v;
        if (auto* b = std::get_if<RszBytes>(&data); b && b->size() >= 16) {
            RszVec4 v;
            std::memcpy(&v, b->data(), 16);
            return v;
        }
        return { 0, 0, 0, 1 };
    }
};

struct RszFieldValue {
    std::string name;
    RszValue value;
    uint32_t offset = 0;  // of the value in the file, for in-place patches of fixed-size fields
};

struct RszInstance {
    uint32_t typeId = 0;
    uint32_t crc = 0;
    std::string typeName;
    bool parsed = false;
    bool layoutMismatch = false;  // crc differs from the type dump: values may be misread
    std::vector<RszFieldValue> fields;

    const RszValue* Field(std::string_view name) const {
        for (const RszFieldValue& field : fields) {
            if (field.name == name) return &field.value;
        }
        return nullptr;
    }
};

struct RszUserDataRef {
    uint32_t instanceId;
    uint32_t typeId;
    std::string path;
};

struct RszData {
    uint32_t version = 0;
    // -1 when all were read; instances after it are unparsed, as their data can't be located.
    int32_t stoppedAt = -1;
    std::string stopReason;
    std::vector<int32_t> objectTable;
    std::vector<RszInstance> instances;
    std::vector<RszUserDataRef> userData;
};

#endif
