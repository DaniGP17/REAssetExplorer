#ifndef REASSETEXPLORER_MOTREADER_H
#define REASSETEXPLORER_MOTREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/MotionData.h"

class MotReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".mot.") != std::string_view::npos; }

    MotData Read(std::span<const uint8_t> data) const;

    // data runs from the embedded mot to the end of the motlist.
    static MotData ReadEmbedded(std::span<const uint8_t> data,
                                std::shared_ptr<const std::vector<MotBone>> sharedBones);
};

#endif
