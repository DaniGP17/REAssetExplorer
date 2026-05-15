#ifndef REASSETEXPLORER_WWISEREADER_H
#define REASSETEXPLORER_WWISEREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/SoundData.h"

class BnkReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".bnk.") != std::string_view::npos; }

    SoundBankData Read(std::span<const uint8_t> data) const;
};

class PckReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".pck.") != std::string_view::npos; }

    static uint32_t HeaderBytes(std::span<const uint8_t> first8);
    SoundPackageData ReadHeader(std::span<const uint8_t> header) const;
};

#endif
