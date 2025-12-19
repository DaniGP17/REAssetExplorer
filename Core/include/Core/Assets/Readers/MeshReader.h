#ifndef REASSETEXPLORER_MeshReader_H
#define REASSETEXPLORER_MeshReader_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/MeshData.h"

class MemoryReader;

class MeshReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".mesh.") != std::string_view::npos; }

    MeshData Read(std::span<const uint8_t> data) const;

private:
    static void ReadLods(const MemoryReader& r, MeshData& mesh, uint64_t meshOffset);
    static void ReadBuffers(const MemoryReader& r, MeshData& mesh, uint64_t bufferOffset);
    static void ReadSkeleton(const MemoryReader& r, MeshData& mesh, uint64_t skeletonOffset, uint64_t jointNamesOffset,
                             const std::vector<std::string>& strings);
};

#endif
