#include "Core/Rsz/RszReader.h"

#include <unordered_set>

#include "Core/IO/MemoryReader.h"

namespace {

void Align(MemoryReader& r, std::size_t alignment) {
    if (alignment < 2) return;
    std::size_t pos = r.Tell();
    r.Seek((pos + alignment - 1) & ~(alignment - 1));
}

std::string ReadCountedUtf16(MemoryReader& r) {
    uint32_t length = r.Read<uint32_t>();
    std::string result;
    result.reserve(length);
    uint16_t pendingHigh = 0;
    for (uint32_t i = 0; i < length; i++) {
        uint16_t ch = r.Read<uint16_t>();
        if (ch != 0) MemoryReader::AppendUtf16(result, ch, pendingHigh);
    }
    return result;
}

std::string ReadCountedAscii(MemoryReader& r) {
    uint32_t length = r.Read<uint32_t>();
    std::string result;
    result.reserve(length);
    for (uint32_t i = 0; i < length; i++) {
        char ch = static_cast<char>(r.Read<uint8_t>());
        if (ch != 0) result += ch;
    }
    return result;
}

RszBytes ReadRaw(MemoryReader& r, std::size_t size) {
    auto bytes = r.ReadBytes(size);
    return { bytes.begin(), bytes.end() };
}

}

RszData RszReader::Read(MemoryReader& r, std::size_t rszStart) const {
    RszData rsz;
    r.Seek(rszStart);

    if ((r.Read<uint32_t>() & 0x00FFFFFF) != 0x005A5352) {
        throw std::runtime_error("rsz: bad magic");
    }
    rsz.version = r.Read<uint32_t>();
    int32_t objectCount = r.Read<int32_t>();
    int32_t instanceCount = r.Read<int32_t>();
    int64_t userDataCount = r.Read<int64_t>();

    uint64_t instanceOffset = r.Read<uint64_t>();
    uint64_t dataOffset = r.Read<uint64_t>();
    uint64_t userDataOffset = r.Read<uint64_t>();

    rsz.objectTable.reserve(objectCount);
    for (int32_t i = 0; i < objectCount; i++) {
        rsz.objectTable.push_back(r.Read<int32_t>());
    }

    r.Seek(rszStart + instanceOffset);
    rsz.instances.resize(instanceCount);
    for (int32_t i = 0; i < instanceCount; i++) {
        RszInstance& instance = rsz.instances[i];
        instance.typeId = r.Read<uint32_t>();
        instance.crc = r.Read<uint32_t>();
        if (const RszTypeDef* type = db.Find(instance.typeId)) {
            instance.typeName = type->name;
        }
    }

    r.Seek(rszStart + userDataOffset);
    std::unordered_set<uint32_t> userDataInstances;
    rsz.userData.reserve(userDataCount);
    for (int64_t i = 0; i < userDataCount; i++) {
        RszUserDataRef ref;
        ref.instanceId = r.Read<uint32_t>();
        ref.typeId = r.Read<uint32_t>();
        uint64_t pathOffset = r.Read<uint64_t>();
        ref.path = r.ReadWStringAt(rszStart + pathOffset);
        userDataInstances.insert(ref.instanceId);
        rsz.userData.push_back(std::move(ref));
    }

    r.Seek(rszStart + dataOffset);
    for (int32_t i = 0; i < instanceCount; i++) {
        RszInstance& instance = rsz.instances[i];
        if (instance.typeName.empty()) continue;
        if (userDataInstances.contains(static_cast<uint32_t>(i))) {
            instance.parsed = true;
            continue;
        }

        const RszTypeDef* type = db.Find(instance.typeId);
        // A crc mismatch means other fields, but many still match the dump's bytes (RE8 scenes
        // rely on it), so read it the dump's way and flag it.
        instance.layoutMismatch = type->crc != 0 && instance.crc != type->crc;
        try {
            instance.fields.reserve(type->fields.size());
            for (const RszFieldDef& field : type->fields) {
                Align(r, field.array ? 4 : field.align);
                uint32_t offset = static_cast<uint32_t>(r.Tell());
                instance.fields.push_back({ field.name, ReadValue(r, field), offset });
            }
            instance.parsed = true;
        } catch (const std::exception& e) {
            rsz.stoppedAt = i;
            rsz.stopReason = instance.layoutMismatch ? "the type's layout differs from the type dump" : e.what();
            break;
        }
    }

    return rsz;
}

RszValue RszReader::ReadValue(MemoryReader& r, const RszFieldDef& field) const {
    Align(r, field.array ? 4 : field.align);

    if (field.array) {
        uint32_t count = r.Read<uint32_t>();
        RszArray items;
        items.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            Align(r, field.align);
            items.push_back(ReadScalar(r, field));
        }
        return { items };
    }
    return ReadScalar(r, field);
}

RszValue RszReader::ReadScalar(MemoryReader& r, const RszFieldDef& field) const {
    const std::string& type = field.type;

    if (type == "String" || type == "Resource") return { ReadCountedUtf16(r) };
    if (type == "RuntimeType" || type == "MBString") return { ReadCountedAscii(r) };

    if (type == "S8") return { static_cast<int64_t>(r.Read<int8_t>()) };
    if (type == "U8") return { static_cast<uint64_t>(r.Read<uint8_t>()) };
    if (type == "S16") return { static_cast<int64_t>(r.Read<int16_t>()) };
    if (type == "U16") return { static_cast<uint64_t>(r.Read<uint16_t>()) };
    if (type == "S32") return { static_cast<int64_t>(r.Read<int32_t>()) };
    if (type == "U32") return { static_cast<uint64_t>(r.Read<uint32_t>()) };
    if (type == "S64") return { r.Read<int64_t>() };
    if (type == "U64") return { r.Read<uint64_t>() };
    if (type == "F32") return { static_cast<double>(r.Read<float>()) };
    if (type == "F64") return { r.Read<double>() };
    if (type == "Bool") return { r.Read<uint8_t>() != 0 };

    if (type == "Vec2") {
        RszVec4 v{ r.Read<float>(), r.Read<float>(), 0, 0 };
        r.Seek(r.Tell() + 8);
        return { RszVec3{ v.x, v.y, 0 } };
    }
    if (type == "Vec3") {
        RszVec3 v{ r.Read<float>(), r.Read<float>(), r.Read<float>() };
        r.Seek(r.Tell() + 4);
        return { v };
    }
    if (type == "Float3") return { RszVec3{ r.Read<float>(), r.Read<float>(), r.Read<float>() } };
    if (type == "Float4" || type == "Vec4" || type == "Quaternion") {
        return { RszVec4{ r.Read<float>(), r.Read<float>(), r.Read<float>(), r.Read<float>() } };
    }

    if (type == "Color") return { static_cast<uint64_t>(r.Read<uint32_t>()) };
    if (type == "Guid") {
        RszGuid guid;
        auto bytes = r.ReadBytes(16);
        std::copy(bytes.begin(), bytes.end(), guid.bytes.begin());
        return { guid };
    }
    if (type == "Object" || type == "UserData") return { RszObjectRef{ r.Read<uint32_t>() } };

    return { ReadRaw(r, field.size) };
}
