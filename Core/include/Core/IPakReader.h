#ifndef REASSETEXPLORER_IPAKREADER_H
#define REASSETEXPLORER_IPAKREADER_H
#include <filesystem>

#include "PakFile.h"
#include "PakFileList.h"

enum class PakVersions {
    V4
};

constexpr uint32_t PAK_MAGIC = 0x414B504B; // KPKA

class IPakReader {
public:
    virtual ~IPakReader() = default;

    virtual PakFile Open(const std::filesystem::path& path, const PakFileList& fileList) = 0;
    virtual std::vector<uint8_t> ExtractFile(const PakFile& file, const PakEntry& entry) const = 0;
};

#endif
