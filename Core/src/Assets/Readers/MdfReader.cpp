#include "Core/Assets/Readers/MdfReader.h"

#include "Core/IO/MemoryReader.h"

MaterialData MdfReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);

    if (r.ReadAt<uint32_t>(0x00) != 0x0046444D) {
        throw std::runtime_error("mdf2: bad magic");
    }

    MaterialData mat;
    mat.version = r.ReadAt<uint16_t>(0x04);
    uint16_t materialCount = r.ReadAt<uint16_t>(0x06);

    std::size_t pos = 0x10;
    for (uint16_t i = 0; i < materialCount; i++) {
        uint64_t nameOffset = r.ReadAt<uint64_t>(pos);
        uint32_t propertyBlockSize = r.ReadAt<uint32_t>(pos + 12);
        uint32_t propertyCount = r.ReadAt<uint32_t>(pos + 16);
        uint32_t textureCount = r.ReadAt<uint32_t>(pos + 20);
        uint32_t shaderType = r.ReadAt<uint32_t>(pos + 32);
        uint32_t flags = r.ReadAt<uint32_t>(pos + 36);
        uint64_t propertyHeadersOffset = r.ReadAt<uint64_t>(pos + 40);
        uint64_t textureHeadersOffset = r.ReadAt<uint64_t>(pos + 48);
        uint64_t propertiesDataOffset = r.ReadAt<uint64_t>(pos + 64);
        uint64_t masterMaterialPathOffset = r.ReadAt<uint64_t>(pos + 72);
        pos += 80;

        MaterialEntry entry;
        entry.name = r.ReadWStringAt(nameOffset);
        entry.masterMaterialPath = r.ReadWStringAt(masterMaterialPathOffset);
        entry.shaderType = shaderType;
        entry.flags = flags;
        entry.propertyBlockSize = propertyBlockSize;

        std::size_t texPos = textureHeadersOffset;
        for (uint32_t j = 0; j < textureCount; j++) {
            MaterialTexture texture;
            texture.type = r.ReadWStringAt(r.ReadAt<uint64_t>(texPos));
            texture.path = r.ReadWStringAt(r.ReadAt<uint64_t>(texPos + 16));
            entry.textures.push_back(std::move(texture));
            texPos += 32;
        }

        std::size_t propPos = propertyHeadersOffset;
        for (uint32_t j = 0; j < propertyCount; j++) {
            MaterialProperty prop;
            prop.name = r.ReadWStringAt(r.ReadAt<uint64_t>(propPos));
            prop.dataOffset = r.ReadAt<uint32_t>(propPos + 16);
            uint32_t paramCount = r.ReadAt<uint32_t>(propPos + 20);
            for (uint32_t k = 0; k < paramCount; k++) {
                prop.values.push_back(r.ReadAt<float>(propertiesDataOffset + prop.dataOffset + k * 4));
            }
            entry.properties.push_back(std::move(prop));
            propPos += 24;
        }

        mat.materials.push_back(std::move(entry));
    }

    return mat;
}
