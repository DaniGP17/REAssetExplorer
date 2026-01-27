#include "Core/Assets/Readers/FolReader.h"

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t FOL_MAGIC = 0x004C4F46;
constexpr std::size_t UNIT_SIZE = 32;
constexpr std::size_t INSTANCE_SIZE = 48;
constexpr uint32_t MAX_UNITS = 65536;

}

FoliageData FolReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0) != FOL_MAGIC) throw std::runtime_error("fol: bad magic");

    FoliageData fol;
    fol.version = r.ReadAt<uint32_t>(4);
    uint32_t unitCount = r.ReadAt<uint32_t>(8);
    uint64_t unitTable = r.ReadAt<uint64_t>(16);
    if (unitCount > MAX_UNITS) throw std::runtime_error("fol: unit count " + std::to_string(unitCount));

    for (uint32_t u = 0; u < unitCount && unitTable != 0; u++) {
        std::size_t pos = unitTable + u * UNIT_SIZE;
        uint32_t instanceCount = r.ReadAt<uint32_t>(pos);
        uint64_t instanceOffset = r.ReadAt<uint64_t>(pos + 8);
        uint64_t meshOffset = r.ReadAt<uint64_t>(pos + 16);
        uint64_t materialOffset = r.ReadAt<uint64_t>(pos + 24);

        FoliageUnit unit;
        if (meshOffset != 0) unit.meshPath = r.ReadWStringAt(meshOffset);
        if (materialOffset != 0) unit.materialPath = r.ReadWStringAt(materialOffset);
        r.BytesAt(instanceOffset, static_cast<std::size_t>(instanceCount) * INSTANCE_SIZE);
        unit.instances.reserve(instanceCount);
        for (uint32_t i = 0; i < instanceCount; i++) {
            std::size_t at = instanceOffset + i * INSTANCE_SIZE;
            FoliageInstance instance;
            for (int k = 0; k < 3; k++) {
                instance.position[k] = r.ReadAt<float>(at + k * 4);
                instance.scale[k] = r.ReadAt<float>(at + 32 + k * 4);
            }
            for (int k = 0; k < 4; k++) instance.rotation[k] = r.ReadAt<float>(at + 16 + k * 4);
            unit.instances.push_back(instance);
        }
        fol.units.push_back(std::move(unit));
    }
    return fol;
}
