#ifndef REASSETEXPLORER_GUIREADER_H
#define REASSETEXPLORER_GUIREADER_H
#include <span>

#include "Core/Assets/GuiData.h"
#include "Core/Assets/IAssetReader.h"

class GuiReader : public IAssetReader {
public:
    bool SupportsPath(std::string_view path) const override { return path.find(".gui.") != std::string_view::npos; }

    GuiData Read(std::span<const uint8_t> data) const;
};

TimelineClip ReadTimelineClip(std::span<const uint8_t> data);

#endif
