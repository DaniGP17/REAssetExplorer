#include "Explorer/EffectParams.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace {

constexpr float DEGREES = 57.2957795f;

enum class Shape {
    Float,
    Uint,
    RangeI,
    Range,
    MinMax,
    Vec2,
    Vec3,
    RangeBases,    // the three bases of EfxRange[3]
    RangeSpreads,  // the three spreads of EfxRange[3]
    Color          // uint8_t[4]
};

struct Binding {
    Shape shape;
    void* data;
    float scale = 1;  // shown = stored * scale
};

std::vector<float> Read(const Binding& b) {
    auto f = static_cast<const float*>(b.data);
    switch (b.shape) {
        case Shape::Float: return { *f * b.scale };
        case Shape::Uint: return { static_cast<float>(*static_cast<const uint32_t*>(b.data)) };
        case Shape::RangeI: {
            auto r = static_cast<const EfxRangeI*>(b.data);
            return { static_cast<float>(r->min), static_cast<float>(r->max) };
        }
        case Shape::Range: {
            auto r = static_cast<const EfxRange*>(b.data);
            return { r->base * b.scale, r->spread * b.scale };
        }
        case Shape::MinMax: {
            auto r = static_cast<const EfxMinMax*>(b.data);
            return { r->min * b.scale, r->max * b.scale };
        }
        case Shape::Vec2: return { f[0] * b.scale, f[1] * b.scale };
        case Shape::Vec3: return { f[0] * b.scale, f[1] * b.scale, f[2] * b.scale };
        case Shape::RangeBases:
        case Shape::RangeSpreads: {
            auto r = static_cast<const EfxRange*>(b.data);
            bool bases = b.shape == Shape::RangeBases;
            std::vector<float> out;
            for (int i = 0; i < 3; i++) out.push_back((bases ? r[i].base : r[i].spread) * b.scale);
            return out;
        }
        case Shape::Color: {
            auto c = static_cast<const uint8_t*>(b.data);
            return { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f };
        }
    }
    return {};
}

void Write(const Binding& b, std::span<const float> v) {
    auto f = static_cast<float*>(b.data);
    auto at = [&](std::size_t i) { return i < v.size() ? v[i] : 0.0f; };
    auto whole = [&](std::size_t i) { return static_cast<int32_t>(std::lround(at(i))); };
    switch (b.shape) {
        case Shape::Float:
            *f = at(0) / b.scale;
            break;
        case Shape::Uint:
            *static_cast<uint32_t*>(b.data) = static_cast<uint32_t>(std::max(0, whole(0)));
            break;
        case Shape::RangeI:
            *static_cast<EfxRangeI*>(b.data) = { whole(0), whole(1) };
            break;
        case Shape::Range:
            *static_cast<EfxRange*>(b.data) = { at(0) / b.scale, at(1) / b.scale };
            break;
        case Shape::MinMax:
            *static_cast<EfxMinMax*>(b.data) = { at(0) / b.scale, at(1) / b.scale };
            break;
        case Shape::Vec2:
        case Shape::Vec3:
            for (std::size_t i = 0; i < (b.shape == Shape::Vec2 ? 2u : 3u); i++) f[i] = at(i) / b.scale;
            break;
        case Shape::RangeBases:
        case Shape::RangeSpreads: {
            auto r = static_cast<EfxRange*>(b.data);
            for (std::size_t i = 0; i < 3; i++) {
                (b.shape == Shape::RangeBases ? r[i].base : r[i].spread) = at(i) / b.scale;
            }
            break;
        }
        case Shape::Color: {
            auto c = static_cast<uint8_t*>(b.data);
            for (std::size_t i = 0; i < 4; i++) c[i] = static_cast<uint8_t>(std::lround(std::clamp(at(i), 0.0f, 1.0f) * 255.0f));
            break;
        }
    }
}

// Inspector order. EffectParams only reads through it.
template <typename F>
void VisitFields(EfxEmitter& e, F&& field) {
    const char* minMax = "Min,Max";
    const char* baseSpread = "Base,Spread";
    const char* xyz = "X,Y,Z";
    if (e.spawn) {
        EfxSpawn& s = *e.spawn;
        field("spawn.max", "Spawn", "Max particles", "", Binding{ Shape::Uint, &s.maxParticles });
        field("spawn.count", "Spawn", "Count", minMax, Binding{ Shape::RangeI, &s.spawnNum });
        field("spawn.interval", "Spawn", "Interval (frames)", minMax, Binding{ Shape::RangeI, &s.intervalFrame });
        field("spawn.loops", "Spawn", "Loops (0 = forever)", "Min,Rand", Binding{ Shape::RangeI, &s.loopNum });
        field("spawn.delay", "Spawn", "Delay (frames)", minMax, Binding{ Shape::RangeI, &s.emitterDelayFrame });
    }
    if (e.life) {
        EfxLife& l = *e.life;
        field("life.appear", "Life", "Fade in (frames)", minMax, Binding{ Shape::RangeI, &l.appearFrame });
        field("life.keep", "Life", "Keep (frames)", minMax, Binding{ Shape::RangeI, &l.keepFrame });
        field("life.vanish", "Life", "Fade out (frames)", minMax, Binding{ Shape::RangeI, &l.vanishFrame });
    }
    if (e.transform) {
        EfxTransform3D& t = *e.transform;
        field("transform.position", "Transform", "Position", xyz, Binding{ Shape::Vec3, t.position });
        field("transform.rotation", "Transform", "Rotation", xyz, Binding{ Shape::Vec3, t.rotation, DEGREES });
        field("transform.scale", "Transform", "Scale", xyz, Binding{ Shape::Vec3, t.scale });
    }
    if (e.billboard) {
        EfxBillboard3D& b = *e.billboard;
        field("billboard.color", "Billboard", "Color", "R,G,B,A", Binding{ Shape::Color, b.color });
        field("billboard.colorRange", "Billboard", "Color range", "R,G,B,A", Binding{ Shape::Color, b.colorRange });
        field("billboard.emissive", "Billboard", "Emissive rate", "", Binding{ Shape::Float, &b.emissiveRate });
        field("billboard.alphaRate", "Billboard", "Alpha rate", "", Binding{ Shape::Float, &b.alphaRate });
        field("billboard.size", "Billboard", "Size", baseSpread, Binding{ Shape::Range, &b.sizeScalar });
        field("billboard.sizeX", "Billboard", "Size X", baseSpread, Binding{ Shape::Range, &b.sizeX });
        field("billboard.sizeY", "Billboard", "Size Y", baseSpread, Binding{ Shape::Range, &b.sizeY });
        field("billboard.rotation", "Billboard", "Rotation", baseSpread, Binding{ Shape::Range, &b.rotation, DEGREES });
        field("billboard.offset", "Billboard", "Offset", "X,Y", Binding{ Shape::Vec2, b.offset });
    }
    if (e.velocity) {
        EfxVelocity3D& v = *e.velocity;
        field("velocity.direction", "Velocity", "Direction", xyz, Binding{ Shape::RangeBases, v.direction });
        field("velocity.directionSpread", "Velocity", "Direction spread", xyz, Binding{ Shape::RangeSpreads, v.direction });
        field("velocity.speed", "Velocity", "Speed (m/s)", baseSpread, Binding{ Shape::Range, &v.speed });
        field("velocity.speedCoef", "Velocity", "Speed decay", baseSpread, Binding{ Shape::Range, &v.speedCoef });
        field("velocity.gravity", "Velocity", "Gravity rate", baseSpread, Binding{ Shape::Range, &v.gravityRate });
        field("velocity.spread", "Velocity", "Spread", baseSpread, Binding{ Shape::Range, &v.spread });
    }
    if (e.rotateAnim) {
        EfxRotateAnim& r = *e.rotateAnim;
        field("rotate.add", "Rotate Anim", "Speed (deg/frame)", xyz, Binding{ Shape::RangeBases, r.add, DEGREES });
        field("rotate.addSpread", "Rotate Anim", "Speed spread", xyz, Binding{ Shape::RangeSpreads, r.add, DEGREES });
        field("rotate.coef", "Rotate Anim", "Decay", xyz, Binding{ Shape::RangeBases, r.coef });
    }
    if (e.scaleAnim) {
        EfxScaleAnim& s = *e.scaleAnim;
        field("scale.add", "Scale Anim", "Size add", baseSpread, Binding{ Shape::Range, &s.scalarAdd });
        field("scale.coef", "Scale Anim", "Size decay", baseSpread, Binding{ Shape::Range, &s.scalarCoef });
        field("scale.addX", "Scale Anim", "Size X add", baseSpread, Binding{ Shape::Range, &s.addX });
        field("scale.coefX", "Scale Anim", "Size X decay", baseSpread, Binding{ Shape::Range, &s.coefX });
        field("scale.addY", "Scale Anim", "Size Y add", baseSpread, Binding{ Shape::Range, &s.addY });
        field("scale.coefY", "Scale Anim", "Size Y decay", baseSpread, Binding{ Shape::Range, &s.coefY });
    }
    if (e.uvSequence) {
        EfxUVSequence& u = *e.uvSequence;
        field("uv.sequence", "UV Sequence", "Sequence", minMax, Binding{ Shape::RangeI, &u.sequenceNo });
        field("uv.pattern", "UV Sequence", "Start pattern", minMax, Binding{ Shape::RangeI, &u.patternNo });
        field("uv.speed", "UV Sequence", "Speed (patterns/frame)", minMax, Binding{ Shape::MinMax, &u.playSpeed });
    }
    if (e.shape) {
        EfxShape3D& s = *e.shape;
        field("shape.x", "Shape", "Range X", minMax, Binding{ Shape::MinMax, &s.range[0] });
        field("shape.y", "Shape", "Range Y", minMax, Binding{ Shape::MinMax, &s.range[1] });
        field("shape.z", "Shape", "Range Z", minMax, Binding{ Shape::MinMax, &s.range[2] });
        field("shape.rotation", "Shape", "Rotation", xyz, Binding{ Shape::Vec3, s.localRotation, DEGREES });
        field("shape.arcStart", "Shape", "Arc start", "", Binding{ Shape::Float, &s.arcStart, DEGREES });
        field("shape.arcLength", "Shape", "Arc length", "", Binding{ Shape::Float, &s.arcLength, DEGREES });
    }
    if (e.shaderSettings) {
        field("shader.soft", "Shader", "Soft distance", "", Binding{ Shape::Float, &e.shaderSettings->softParticleDistance });
    }
}

}

std::vector<EffectParam> EffectParams(const EfxEmitter& emitter) {
    std::vector<EffectParam> params;
    VisitFields(const_cast<EfxEmitter&>(emitter), [&](const char* id, const char* section, const char* label,
                                                       const char* components, const Binding& binding) {
        params.push_back({ id, section, label, components, Read(binding) });
    });
    return params;
}

bool SetEffectParam(EfxEmitter& emitter, std::string_view id, std::span<const float> values) {
    bool found = false;
    VisitFields(emitter, [&](const char* fieldId, const char*, const char*, const char*, const Binding& binding) {
        if (found || id != fieldId) return;
        Write(binding, values);
        found = true;
    });
    return found;
}

std::string EffectParamKey(const std::string& efxPakPath, std::size_t emitter, std::string_view id) {
    return "param:" + efxPakPath + '|' + std::to_string(emitter) + '|' + std::string(id);
}

bool ParseEffectParamKey(std::string_view key, std::string_view efxPakPath, std::size_t& emitter, std::string& id) {
    std::string_view prefix = "param:";
    if (!key.starts_with(prefix)) return false;
    key.remove_prefix(prefix.size());
    if (!key.starts_with(efxPakPath) || key.size() <= efxPakPath.size() || key[efxPakPath.size()] != '|') return false;
    key.remove_prefix(efxPakPath.size() + 1);
    std::size_t bar = key.find('|');
    if (bar == std::string_view::npos) return false;
    auto [end, error] = std::from_chars(key.data(), key.data() + bar, emitter);
    if (error != std::errc() || end != key.data() + bar) return false;
    id = key.substr(bar + 1);
    return true;
}
