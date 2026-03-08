#ifndef REASSETEXPLORER_SdfReader_H
#define REASSETEXPLORER_SdfReader_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/SdfData.h"

class MemoryReader;

class SdfReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override {
        return path.find(".mmtr.") != std::string_view::npos || path.find(".sdf.") != std::string_view::npos;
    }

    SdfData Read(std::span<const uint8_t> data) const;

private:
    static SdfProgram ReadProgram(const MemoryReader& r, std::size_t at);
};

#endif
