#ifndef REASSETEXPLORER_PAKREADERV4_H
#define REASSETEXPLORER_PAKREADERV4_H
#include "IPakReader.h"
#include "IO/BinaryReader.h"

class PakReaderV4 : public IPakReader {
    constexpr static std::size_t FLAGS_SIZE = 8;
    constexpr static uint32_t PAK_V4_VERSION = 4;

public:
    PakFile Open(const std::filesystem::path& path, const PakFileList& fileList) override;
    std::vector<uint8_t> ExtractFile(const PakFile& file, const PakEntry& entry) const override;

private:
    static uint32_t ReadHeader(BinaryReader& reader);
    static void ReadEntries(BinaryReader& reader, PakFile& pak, const PakFileList& fileList, uint32_t entryCount);
};

#endif
