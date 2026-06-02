#include "Core/Assets/Readers/GuiReader.h"

#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <tuple>

#include "Core/IO/MemoryReader.h"

namespace {

constexpr uint32_t GUIR_MAGIC = 0x52495547;
// Values of version % 100, as in REE-Lib's GuiVersion.
constexpr int VERSION_RE8 = 23;
constexpr int VERSION_RE_RT = 27;
constexpr int VERSION_RE4 = 34;
constexpr int VERSION_DD2 = 35;
constexpr int VERSION_MHWILDS = 41;

GuiId ReadId(const MemoryReader& r, std::size_t at) {
    GuiId id{};
    auto bytes = r.BytesAt(at, 16);
    std::copy(bytes.begin(), bytes.end(), id.begin());
    return id;
}

// Values that do not fit the 8-byte slot live at the offset stored in it.
GuiValue ReadValue(const MemoryReader& r, uint8_t type, std::size_t slot) {
    GuiValue v;
    v.type = type;
    auto number = [&](double x) {
        if (v.count < 4) v.numbers[v.count++] = x;
    };
    auto target = [&] { return static_cast<std::size_t>(r.ReadAt<uint64_t>(slot)); };
    auto floats = [&](std::size_t at, int n) {
        for (int i = 0; i < n; i++) number(r.ReadAt<float>(at + i * 4));
    };
    auto ints = [&](std::size_t at, int n) {
        for (int i = 0; i < n; i++) number(r.ReadAt<int32_t>(at + i * 4));
    };
    switch (type) {
    case 0x01: number(r.ReadAt<uint8_t>(slot) != 0 ? 1 : 0); break;
    case 0x02: number(r.ReadAt<int8_t>(slot)); break;
    case 0x03: number(r.ReadAt<uint8_t>(slot)); break;
    case 0x04: number(r.ReadAt<int16_t>(slot)); break;
    case 0x05: number(r.ReadAt<uint16_t>(slot)); break;
    case 0x06: number(r.ReadAt<int32_t>(slot)); break;
    case 0x07: number(r.ReadAt<uint32_t>(slot)); break;
    case 0x08: number(static_cast<double>(r.ReadAt<int64_t>(slot))); break;
    case 0x09: number(static_cast<double>(r.ReadAt<uint64_t>(slot))); break;
    case 0x0A:
    case 0x0B: number(r.ReadAt<double>(slot)); break;
    case 0x0C:
    case 0x0E: v.text = r.ReadCStringAt(target()); break;
    case 0x0D:
    case 0x20:
    case 0x38: v.text = r.ReadWStringAt(target()); break;
    case 0x0F: floats(target(), 4); break;
    case 0x15:
    case 0x1A:
    case 0x1F:
    case 0x19: floats(target(), 2); break;
    case 0x16:
    case 0x1B:
    case 0x1E:
    case 0x2C: floats(target(), 3); break;
    case 0x17:
    case 0x1C:
    case 0x2B: floats(target(), 4); break;
    case 0x39: floats(slot, 3); break;
    case 0x18: {
        uint32_t rgba = r.ReadAt<uint32_t>(target());
        for (int i = 0; i < 4; i++) number((rgba >> (8 * i)) & 0xFF);
        break;
    }
    case 0x1D:
    case 0x26: ints(target(), 2); break;
    case 0x27: ints(target(), 3); break;
    case 0x28: ints(target(), 4); break;
    case 0x23:
    case 0x24:
    case 0x25: {
        int n = type - 0x21;
        for (int i = 0; i < n; i++) number(r.ReadAt<uint32_t>(target() + i * 4));
        break;
    }
    case 0x22: v.text = GuiIdText(ReadId(r, target())); break;
    default: number(static_cast<double>(r.ReadAt<int64_t>(slot))); break;
    }
    return v;
}

std::vector<GuiAttribute> ReadAttributes(const MemoryReader& r, uint64_t offset, int version) {
    std::vector<GuiAttribute> out;
    if (offset == 0) return out;
    int64_t count = r.ReadAt<int64_t>(offset);
    if (count < 0 || count > 100000) throw std::runtime_error("bad gui attribute count");
    std::size_t stride = version > VERSION_RE_RT ? 32 : 24;
    for (int64_t i = 0; i < count; i++) {
        std::size_t at = offset + 8 + i * stride;
        GuiAttribute attribute;
        uint8_t type = r.ReadAt<uint8_t>(at);
        attribute.flags = r.ReadAt<uint32_t>(at + 4);
        attribute.name = r.ReadCStringAt(r.ReadAt<uint64_t>(at + 8));
        try {
            attribute.value = ReadValue(r, type, at + 16);
        } catch (const std::exception&) {
            attribute.value.type = type;
            attribute.value.text = "(unreadable)";
        }
        out.push_back(std::move(attribute));
    }
    return out;
}

GuiElement ReadElement(const MemoryReader& r, std::size_t at, int version) {
    GuiElement e;
    e.id = ReadId(r, at);
    e.containerId = ReadId(r, at + 16);
    std::size_t p = at + 32;
    if (version >= VERSION_RE8) p += 16;
    e.name = r.ReadWStringAt(r.ReadAt<uint64_t>(p));
    e.className = r.ReadCStringAt(r.ReadAt<uint64_t>(p + 8));
    uint64_t attributesOffset = r.ReadAt<uint64_t>(p + 16);
    p += 24;
    if (version >= VERSION_RE4) p += 8;
    uint64_t extraOffset = r.ReadAt<uint64_t>(p);
    p += 8;
    if (version >= VERSION_RE_RT) p += 8;
    uint64_t dataOffset = r.ReadAt<uint64_t>(p);
    e.attributes = ReadAttributes(r, attributesOffset, version);
    e.extraAttributes = ReadAttributes(r, extraOffset, version);
    if (dataOffset != 0) {
        std::size_t size = 0;
        std::size_t start = dataOffset;
        if (e.className == "via.gui.TextureSet") {
            size = static_cast<std::size_t>(r.ReadAt<int32_t>(dataOffset)) * 24;
            start += 4;
        } else if (e.className == "via.gui.Scale9Grid" || e.className == "via.gui.BlurFilter") {
            size = 160;
        }
        if (size > 0) {
            auto bytes = r.BytesAt(start, size);
            e.data.assign(bytes.begin(), bytes.end());
        }
    }
    return e;
}

// Embedded via.timeline clip, layouts 40 (RE8) and 43 (RE7 RT). Offsets are relative to the
// clip; key values that do not fit their slot live in the clip's string tables.
constexpr uint32_t CLIP_MAGIC = 0x50494C43;
constexpr uint32_t CLIP_VERSION_RE8 = 40;
constexpr std::size_t CLIP_TRACK_SIZE = 40;
constexpr std::size_t CLIP_PROPERTY_SIZE = 72;
constexpr std::size_t CLIP_KEY_SIZE = 32;
constexpr uint8_t INTERPOLATION_HERMITE = 5;

bool IsPropertyContainer(uint8_t type) {
    return (type >= 0x0F && type <= 0x1F) || (type >= 0x23 && type <= 0x25) || type == 0x29 || type == 0x2A || type == 0x2B ||
           (type >= 0x2D && type <= 0x31) || type == 0x36 || type == 0x37 || type == 0x39;
}

GuiValue ReadKeyValue(const MemoryReader& r, uint8_t type, std::size_t slot, std::size_t names, std::size_t unicodeNames) {
    GuiValue v;
    v.type = type;
    auto number = [&](double x) { v.numbers[v.count++] = x; };
    switch (type) {
    case 0x01: number(r.ReadAt<uint8_t>(slot) != 0 ? 1 : 0); break;
    case 0x02: number(r.ReadAt<int8_t>(slot)); break;
    case 0x03: number(r.ReadAt<uint8_t>(slot)); break;
    case 0x04: number(r.ReadAt<int16_t>(slot)); break;
    case 0x05: number(r.ReadAt<uint16_t>(slot)); break;
    case 0x06: number(r.ReadAt<int32_t>(slot)); break;
    case 0x07: number(r.ReadAt<uint32_t>(slot)); break;
    case 0x0A:
    case 0x0B: number(r.ReadAt<double>(slot)); break;
    case 0x0C:
    case 0x0E: v.text = r.ReadCStringAt(names + r.ReadAt<uint64_t>(slot)); break;
    case 0x0D:
    case 0x20:
    case 0x22:
    case 0x38: v.text = r.ReadWStringAt(unicodeNames + r.ReadAt<uint64_t>(slot) * 2); break;
    default: number(static_cast<double>(r.ReadAt<int64_t>(slot))); break;
    }
    return v;
}

void ReadEmbeddedClip(const MemoryReader& r, std::size_t base, GuiClipInfo& clip) {
    if (r.ReadAt<uint32_t>(base) != CLIP_MAGIC) return;
    uint32_t version = r.ReadAt<uint32_t>(base + 4);
    clip.frames = r.ReadAt<float>(base + 8);
    int32_t trackCount = r.ReadAt<int32_t>(base + 12);
    int32_t propertyCount = r.ReadAt<int32_t>(base + 16);
    std::size_t tracks = base + r.ReadAt<uint64_t>(base + 24);
    std::size_t properties = base + r.ReadAt<uint64_t>(base + 32);
    std::size_t keys = base + r.ReadAt<uint64_t>(base + 40);
    std::size_t hermite = base + r.ReadAt<uint64_t>(base + 56);
    std::size_t names = base + r.ReadAt<uint64_t>(base + 80);
    std::size_t unicodeNames = base + r.ReadAt<uint64_t>(base + 88);

    struct Property {
        std::string name;
        uint8_t type;
        uint64_t childStart;
        uint16_t childCount;
    };
    std::vector<Property> props;
    for (int32_t p = 0; p < propertyCount; p++) {
        std::size_t at = properties + p * CLIP_PROPERTY_SIZE;
        Property prop;
        prop.name = r.ReadCStringAt(names + r.ReadAt<uint64_t>(at + 16));
        prop.childStart = r.ReadAt<uint64_t>(at + 32);
        prop.childCount = r.ReadAt<uint16_t>(at + 40);
        prop.type = r.ReadAt<uint8_t>(at + (version > CLIP_VERSION_RE8 ? 45 : 46));
        props.push_back(std::move(prop));
    }
    auto readKeys = [&](const Property& leaf, GuiClipCurve& curve) {
        for (uint64_t k = leaf.childStart; k < leaf.childStart + leaf.childCount; k++) {
            std::size_t at = keys + k * CLIP_KEY_SIZE;
            GuiClipKey key;
            key.frame = r.ReadAt<float>(at);
            key.interpolation = r.ReadAt<uint8_t>(at + 8);
            key.value = ReadKeyValue(r, leaf.type, at + 16, names, unicodeNames);
            if (key.interpolation == INTERPOLATION_HERMITE) {
                std::size_t handle = hermite + static_cast<std::size_t>(r.ReadAt<int32_t>(at + 24)) * 16;
                for (int i = 0; i < 4; i++) key.handles[i] = r.ReadAt<float>(handle + i * 4);
            }
            curve.keys.push_back(std::move(key));
        }
    };

    for (int32_t t = 0; t < trackCount; t++) {
        std::size_t at = tracks + t * CLIP_TRACK_SIZE;
        GuiClipTrack track;
        track.root = t == 0;
        track.name = r.ReadWStringAt(unicodeNames + r.ReadAt<uint64_t>(at + 16) * 2);
        int16_t count = r.ReadAt<int16_t>(at + 2);
        uint64_t first = r.ReadAt<uint64_t>(at + 32);
        for (uint64_t p = first; p < first + count && p < props.size(); p++) {
            const Property& prop = props[p];
            if (!IsPropertyContainer(prop.type)) {
                GuiClipCurve curve{ prop.name, {}, prop.type, {} };
                readKeys(prop, curve);
                track.curves.push_back(std::move(curve));
                continue;
            }
            for (uint64_t c = prop.childStart; c < prop.childStart + prop.childCount && c < props.size(); c++) {
                GuiClipCurve curve{ prop.name, props[c].name, prop.type, {} };
                readKeys(props[c], curve);
                track.curves.push_back(std::move(curve));
            }
        }
        clip.tracks.push_back(std::move(track));
    }
}

std::vector<std::string> ReadStringList(const MemoryReader& r, uint64_t offset) {
    std::vector<std::string> out;
    if (offset == 0) return out;
    int64_t count = r.ReadAt<int64_t>(offset);
    for (int64_t i = 0; i < count; i++) out.push_back(r.ReadWStringAt(r.ReadAt<uint64_t>(offset + 8 + i * 8)));
    return out;
}

}

std::string GuiIdText(const GuiId& g) {
    char text[40];
    // .NET Guid layout: the first three groups are little-endian.
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2],
                  g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return text;
}

std::string GuiValue::ToString() const {
    if (!text.empty() || count == 0) return text;
    std::string out;
    char buffer[32];
    for (int i = 0; i < count; i++) {
        std::snprintf(buffer, sizeof(buffer), "%s%.6g", i ? ", " : "", numbers[i]);
        out += buffer;
    }
    return out;
}

const GuiAttribute* GuiElement::Find(const std::string& attribute) const {
    for (const GuiAttribute& a : attributes) {
        if (a.name == attribute) return &a;
    }
    for (const GuiAttribute& a : extraAttributes) {
        if (a.name == attribute) return &a;
    }
    return nullptr;
}

const GuiContainer* GuiData::FindContainer(const GuiId& id) const {
    for (const GuiContainer& c : containers) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

// Standalone layout 40: counts at 0x20 (properties) and 0x24 (keys); offsets at 0x50 (properties), 0x58 (keys),
// 0x60 (names), 0x88 (unicode names, up to 0x90); property and key records as in the embedded clips. A property's
// children are properties when it is a container and keys otherwise.
TimelineClip ReadTimelineClip(std::span<const uint8_t> data) {
    MemoryReader r(data);
    TimelineClip clip;
    if (r.ReadAt<uint32_t>(0) != CLIP_MAGIC) return clip;
    clip.frames = r.ReadAt<float>(8);
    int32_t propertyCount = r.ReadAt<int32_t>(0x20);
    std::size_t properties = r.ReadAt<uint64_t>(0x50);
    std::size_t keys = r.ReadAt<uint64_t>(0x58);
    std::size_t names = r.ReadAt<uint64_t>(0x60);
    std::size_t unicodeNames = r.ReadAt<uint64_t>(0x88);
    std::size_t unicodeEnd = r.ReadAt<uint64_t>(0x90);
    for (std::size_t at = unicodeNames; at + 2 <= unicodeEnd;) {
        std::string name = r.ReadWStringAt(at);
        at += (name.size() + 1) * 2;
        if (!name.empty()) clip.objectNames.push_back(std::move(name));
    }
    struct Property {
        std::string name;
        uint8_t type;
        uint64_t childStart;
        uint16_t childCount;
    };
    std::vector<Property> props;
    std::vector<uint8_t> isChild(propertyCount, 0);
    for (int32_t p = 0; p < propertyCount; p++) {
        std::size_t at = properties + p * CLIP_PROPERTY_SIZE;
        Property prop;
        prop.name = r.ReadCStringAt(names + r.ReadAt<uint64_t>(at + 16));
        prop.childStart = r.ReadAt<uint64_t>(at + 32);
        prop.childCount = r.ReadAt<uint16_t>(at + 40);
        prop.type = r.ReadAt<uint8_t>(at + 46);
        props.push_back(std::move(prop));
    }
    for (const Property& prop : props) {
        if (!IsPropertyContainer(prop.type)) continue;
        for (uint64_t c = prop.childStart; c < prop.childStart + prop.childCount && c < props.size(); c++) isChild[c] = 1;
    }
    std::vector<std::string> path;
    std::function<void(std::size_t)> walk = [&](std::size_t p) {
        const Property& prop = props[p];
        path.push_back(prop.name);
        if (IsPropertyContainer(prop.type)) {
            for (uint64_t c = prop.childStart; c < prop.childStart + prop.childCount && c < props.size(); c++) walk(c);
        } else {
            TimelineClipCurve curve{ path, prop.type, {} };
            for (uint64_t k = prop.childStart; k < prop.childStart + prop.childCount; k++) {
                std::size_t at = keys + k * CLIP_KEY_SIZE;
                GuiClipKey key;
                key.frame = r.ReadAt<float>(at);
                key.interpolation = r.ReadAt<uint8_t>(at + 8);
                key.value = ReadKeyValue(r, prop.type, at + 16, names, unicodeNames);
                curve.keys.push_back(std::move(key));
            }
            clip.curves.push_back(std::move(curve));
        }
        path.pop_back();
    };
    for (std::size_t p = 0; p < props.size(); p++) {
        if (!isChild[p]) walk(p);
    }
    return clip;
}

GuiData GuiReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (data.size() < 64 || r.ReadAt<uint32_t>(4) != GUIR_MAGIC) throw std::runtime_error("not a gui file (no GUIR magic)");
    GuiData gui;
    gui.version = r.ReadAt<uint32_t>(0);
    int version = static_cast<int>(gui.version % 100);
    if (version >= VERSION_MHWILDS) throw std::runtime_error("gui version " + std::to_string(gui.version) + " not supported");

    uint64_t offsetsStartOffset = r.ReadAt<uint64_t>(8);
    std::size_t p = 16;
    if (version < VERSION_DD2) p += 8;
    uint64_t overridesOffset = r.ReadAt<uint64_t>(p);
    p += 8;
    uint64_t parameterOffset = 0;
    if (version >= VERSION_RE_RT) {
        parameterOffset = r.ReadAt<uint64_t>(p);
        p += 8;
    }
    uint64_t guiFilesOffset = r.ReadAt<uint64_t>(p);
    uint64_t resourcesOffset = r.ReadAt<uint64_t>(p + 8);
    uint64_t viewOffset = r.ReadAt<uint64_t>(offsetsStartOffset + 8);

    int64_t containerCount = r.ReadAt<int64_t>(offsetsStartOffset + 16);
    // A clip's next pointer is the offset of the clip it hands over to.
    std::vector<std::tuple<std::size_t, std::size_t, uint64_t>> nextClips;
    std::map<uint64_t, std::string> clipNames;
    if (containerCount < 0 || containerCount > 100000) throw std::runtime_error("bad gui container count");
    for (int64_t i = 0; i < containerCount; i++) {
        std::size_t at = r.ReadAt<uint64_t>(offsetsStartOffset + 24 + i * 8);
        GuiContainer container;
        container.id = ReadId(r, at);
        container.name = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 16));
        container.className = r.ReadCStringAt(r.ReadAt<uint64_t>(at + 24));
        uint64_t elementsOffset = r.ReadAt<uint64_t>(at + 32);
        uint64_t clipsOffset = r.ReadAt<uint64_t>(at + 40);
        if (elementsOffset != 0) {
            int64_t count = r.ReadAt<int64_t>(elementsOffset);
            for (int64_t e = 0; e < count; e++) {
                container.elements.push_back(ReadElement(r, r.ReadAt<uint64_t>(elementsOffset + 8 + e * 8), version));
            }
        }
        if (clipsOffset != 0) {
            int32_t total = r.ReadAt<int32_t>(clipsOffset);
            int32_t distinct = r.ReadAt<int32_t>(clipsOffset + 4);
            for (int32_t c = 0; c < total; c++) {
                std::size_t clip = r.ReadAt<uint64_t>(clipsOffset + 8 + c * 8);
                GuiClipInfo info;
                info.id = ReadId(r, clip);
                info.loop = r.ReadAt<int64_t>(clip + 16) != 0;
                info.name = r.ReadWStringAt(r.ReadAt<uint64_t>(clip + 24));
                info.pattern = distinct > 0 ? c / distinct : 0;
                if (uint64_t next = r.ReadAt<uint64_t>(clip + 32); next != 0) {
                    nextClips.emplace_back(gui.containers.size(), container.clips.size(), next);
                }
                clipNames[clip] = info.name;
                try {
                    ReadEmbeddedClip(r, clip + 40, info);
                } catch (const std::exception&) {
                    info.tracks.clear();
                }
                container.clips.push_back(std::move(info));
            }
        }
        gui.containers.push_back(std::move(container));
    }

    for (const auto& [container, index, next] : nextClips) {
        if (auto it = clipNames.find(next); it != clipNames.end()) gui.containers[container].clips[index].next = it->second;
    }
    gui.view = ReadElement(r, viewOffset, version);

    if (overridesOffset != 0) {
        int64_t count = r.ReadAt<int64_t>(overridesOffset);
        std::size_t stride = version > VERSION_RE_RT ? 48 : 40;
        for (int64_t i = 0; i < count; i++) {
            std::size_t at = overridesOffset + 8 + i * stride;
            GuiAttributeOverride o;
            o.targetPath = r.ReadWStringAt(r.ReadAt<uint64_t>(at));
            o.targetClass = r.ReadCStringAt(r.ReadAt<uint64_t>(at + 8));
            uint8_t type = r.ReadAt<uint8_t>(at + 16);
            o.attribute.flags = r.ReadAt<uint32_t>(at + 20);
            o.attribute.name = r.ReadCStringAt(r.ReadAt<uint64_t>(at + 24));
            o.attribute.value = ReadValue(r, type, at + 32);
            gui.overrides.push_back(std::move(o));
        }
    }

    if (parameterOffset != 0) {
        uint64_t list = r.ReadAt<uint64_t>(parameterOffset);
        int64_t count = r.ReadAt<int64_t>(list);
        for (int64_t i = 0; i < count; i++) {
            std::size_t at = list + 8 + i * 56;
            GuiParameter parameter;
            parameter.id = ReadId(r, at);
            parameter.name = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 16));
            parameter.className = r.ReadCStringAt(r.ReadAt<uint64_t>(at + 24));
            parameter.targetType = r.ReadWStringAt(r.ReadAt<uint64_t>(at + 32));
            parameter.value = ReadValue(r, r.ReadAt<uint8_t>(at + 40), at + 48);
            gui.parameters.push_back(std::move(parameter));
        }
    }

    gui.linkedGuis = ReadStringList(r, guiFilesOffset);
    gui.resources = ReadStringList(r, resourcesOffset);
    return gui;
}
