#include "Core/Assets/Readers/TexReader.h"

#include <algorithm>

#include "Core/IO/MemoryReader.h"

TextureData TexReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);

    if (r.ReadAt<uint32_t>(0x00) != 0x00584554) {
        throw std::runtime_error("tex: bad magic");
    }

    TextureData tex;
    tex.version = r.ReadAt<uint32_t>(0x04);
    tex.width = r.ReadAt<uint16_t>(0x08);
    tex.height = r.ReadAt<uint16_t>(0x0A);
    tex.depthAndType = r.ReadAt<uint16_t>(0x0C);
    tex.mipInfo = r.ReadAt<uint16_t>(0x0E);
    tex.format = r.ReadAt<uint32_t>(0x10);
    tex.layoutFlags = r.ReadAt<uint64_t>(0x14);
    tex.streamingFlags = r.ReadAt<uint32_t>(0x1C);
    tex.dataSizeTotal = r.ReadAt<uint32_t>(0x20);
    tex.tileMode = r.ReadAt<uint16_t>(0x24);
    tex.alignment = r.ReadAt<uint16_t>(0x26);

    if (tex.width == 0 || tex.height == 0 || tex.width > 16384 || tex.height > 16384) {
        throw std::runtime_error("tex: invalid dimensions");
    }
    if (tex.MipsPerImage() == 0 || tex.MipsPerImage() > 16) {
        throw std::runtime_error("tex: invalid mip count");
    }

    uint32_t totalMips = tex.NumImages() * tex.MipsPerImage();
    for (uint32_t i = 0; i < totalMips; i++) {
        std::size_t headerOffset = 0x28 + i * 16;
        TextureMip mip;
        mip.offset = r.ReadAt<uint64_t>(headerOffset);
        mip.pitch = r.ReadAt<uint32_t>(headerOffset + 8);
        mip.size = r.ReadAt<uint32_t>(headerOffset + 12);

        // A volume's size is one slice; its slices follow each other.
        uint32_t depth = tex.depthAndType > 1 ? std::max<uint32_t>(tex.depthAndType >> (i % tex.MipsPerImage()), 1) : 1;
        auto bytes = r.BytesAt(mip.offset, static_cast<std::size_t>(mip.size) * depth);
        mip.data.assign(bytes.begin(), bytes.end());
        tex.mips.push_back(std::move(mip));
    }

    return tex;
}
