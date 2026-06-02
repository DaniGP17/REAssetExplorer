#ifndef REASSETEXPLORER_GUIDATA_H
#define REASSETEXPLORER_GUIDATA_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using GuiId = std::array<uint8_t, 16>;

struct GuiValue {
    uint8_t type = 0;       // via.timeline.PropertyType
    std::string text;       // strings, enums, assets, GUIDs (formatted)
    double numbers[4]{};
    uint8_t count = 0;

    std::string ToString() const;
    bool Bool() const { return count > 0 && numbers[0] != 0; }
};

struct GuiAttribute {
    std::string name;
    uint32_t flags = 0;
    GuiValue value;
};

struct GuiElement {
    GuiId id{};
    GuiId containerId{};  // container of this element's children (zero when none)
    std::string name;
    std::string className;
    std::vector<GuiAttribute> attributes;
    std::vector<GuiAttribute> extraAttributes;
    std::vector<uint8_t> data;  // TextureSet regions, Scale9Grid and BlurFilter parameters

    const GuiAttribute* Find(const std::string& attribute) const;
};

// via.timeline interpolation: 1 discrete, 2 linear, 5 hermite (handles: x1, y1, x2, y2).
struct GuiClipKey {
    float frame = 0;
    uint8_t interpolation = 1;
    GuiValue value;
    float handles[4] = { 0.33f, 0, 0.67f, 1 };
};

struct GuiClipCurve {
    std::string attribute;
    std::string component;      // "x", "w", "r"... for vector and color attributes
    uint8_t attributeType = 0;  // PropertyType (0x1C Float4...)
    std::vector<GuiClipKey> keys;
};

// The root track animates the container's own element; the others a child, by name.
struct GuiClipTrack {
    std::string name;
    bool root = false;
    std::vector<GuiClipCurve> curves;
};

// A container state (DEFAULT, FADE_IN...), stored as an embedded via.timeline clip.
struct GuiClipInfo {
    GuiId id{};
    // REE-Lib's IsDefault; set on the cyclic clips (LOOP, FLASH, CHARGE...), so taken as looping.
    bool loop = false;
    std::string name;
    float frames = 0;   // <= 0 for a static state
    // Clip names repeat once per state pattern; the element's StatePattern attribute picks the set.
    int32_t pattern = 0;
    std::string next;   // clip played when this one ends ("" to hold)
    std::vector<GuiClipTrack> tracks;
};

// A standalone via.timeline clip (.clip.40): each keyed property with the names of the properties above it
// (component, sub-object, element...), and the clip's object names (game objects, components).
struct TimelineClipCurve {
    std::vector<std::string> path;
    uint8_t type = 0;
    std::vector<GuiClipKey> keys;
};

struct TimelineClip {
    float frames = 0;
    std::vector<std::string> objectNames;
    std::vector<TimelineClipCurve> curves;
};

struct GuiContainer {
    GuiId id{};
    std::string name;
    std::string className;
    std::vector<GuiElement> elements;
    std::vector<GuiClipInfo> clips;
};

struct GuiAttributeOverride {
    std::string targetPath;
    std::string targetClass;
    GuiAttribute attribute;
};

struct GuiParameter {
    GuiId id{};
    std::string name;
    std::string className;
    std::string targetType;
    GuiValue value;
};

struct GuiData {
    uint32_t version = 0;  // the layout follows version % 100
    std::vector<GuiContainer> containers;
    GuiElement view;       // root via.gui.View
    std::vector<GuiAttributeOverride> overrides;
    std::vector<GuiParameter> parameters;
    std::vector<std::string> linkedGuis;
    std::vector<std::string> resources;

    const GuiContainer* FindContainer(const GuiId& id) const;
};

std::string GuiIdText(const GuiId& id);

#endif
