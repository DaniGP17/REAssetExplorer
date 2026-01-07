#ifndef REASSETEXPLORER_MOTLISTREADER_H
#define REASSETEXPLORER_MOTLISTREADER_H
#include <span>

#include "Core/Assets/IAssetReader.h"
#include "Core/Assets/MotionData.h"

class MotlistReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".motlist.") != std::string_view::npos; }

    MotlistData Read(std::span<const uint8_t> data) const;
};

#endif
