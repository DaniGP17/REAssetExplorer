#include "Core/PakReaderV4.h"

#include "Core/IO/Compression.h"

PakFile PakReaderV4::Open(const std::filesystem::path& path, const PakFileList& fileList) {
    BinaryReader reader(path.string());
    PakFile pak(path);

    uint32_t entryCount = ReadHeader(reader);
    ReadEntries(reader, pak, fileList, entryCount);

    return pak;
}

std::vector<uint8_t> PakReaderV4::ExtractFile(const PakFile& file, const PakEntry& entry) const {
    std::vector<uint8_t> data = file.File().ReadAt(static_cast<uint64_t>(entry.offset), static_cast<std::size_t>(entry.compressedSize));

    switch (entry.GetCompressionType()) {
        case CompressionType::Uncompressed: return data;
        case CompressionType::Deflated: return Compression::InflateRaw(data, entry.uncompressedSize);
        case CompressionType::ZStandard: return Compression::DecompressZstd(data, entry.uncompressedSize);
        default: throw std::runtime_error("ExtractFile: unsupported compression");
    }
}

uint32_t PakReaderV4::ReadHeader(BinaryReader& reader) {
    if (reader.Read<uint32_t>() != PAK_MAGIC) {
        throw std::runtime_error("PakReaderV4: bad magic");
    }
    if (reader.Read<uint32_t>() != PAK_V4_VERSION) {
        throw std::runtime_error("PakReaderV4: unsupported version");
    }
    uint32_t entryCount = reader.Read<uint32_t>();
    reader.Read<uint32_t>();
    return entryCount;
}

void PakReaderV4::ReadEntries(BinaryReader& reader, PakFile& pak, const PakFileList& fileList, uint32_t entryCount) {
    for (uint32_t i = 0; i < entryCount; i++) {
        PakEntry entry;
        entry.lowerCaseHash = reader.Read<uint32_t>();
        entry.upperCaseHash = reader.Read<uint32_t>();
        entry.offset = reader.Read<int64_t>();
        entry.compressedSize = reader.Read<int64_t>();
        entry.uncompressedSize = reader.Read<int64_t>();
        entry.flags = reader.Read<std::array<std::uint8_t, FLAGS_SIZE>>();
        entry.checksum = reader.Read<int64_t>();

        const HashListEntry* result = fileList.GetByLowerHash(entry.lowerCaseHash);
        entry.filePath = result != nullptr ? result->path : "Unknown_" + std::to_string(entry.lowerCaseHash);

        pak.AddEntry(std::move(entry));
    }
}
