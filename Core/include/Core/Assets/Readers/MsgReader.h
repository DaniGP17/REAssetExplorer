#ifndef REASSETEXPLORER_MSGREADER_H
#define REASSETEXPLORER_MSGREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/MessageData.h"

// Versions above 12 obfuscate the string pool (chained XOR with a fixed 16-byte key).
class MsgReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".msg.") != std::string_view::npos; }

    MessageData Read(std::span<const uint8_t> data) const;
    static bool IsMessage(std::span<const uint8_t> data);
};

#endif
