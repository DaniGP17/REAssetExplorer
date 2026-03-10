#include "Core/Assets/Readers/EfxReader.h"

#include <array>
#include <string>

#include "Core/IO/MemoryReader.h"

namespace {

// Payload size after the u32 type id, from RE7's per-type Do_call deserializers.
std::size_t ItemSizeRE7(uint32_t type, const MemoryReader& r, std::size_t pos) {
    auto u32 = [&](std::size_t offset) { return static_cast<std::size_t>(r.ReadAt<uint32_t>(pos + offset)); };
    auto expression = [&](std::size_t header) { return header + u32(header - 4); };
    auto clip = [&]() { return 44 + u32(32) + u32(36) + u32(40); };
    auto prefixed = [&](std::size_t header) { return header + 4 + u32(header); };
    auto optionalStrings = [&](std::size_t header) {
        std::size_t size = header;
        std::size_t flags = u32(4);
        for (std::size_t bit : { 1u, 2u }) {
            if (flags & bit) size += 4 + u32(size);
        }
        return size;
    };

    switch (type) {
        case 1: return 44;
        case 3: return 64;
        case 4: return expression(40);
        case 5: return 24;
        case 10: return 44;
        case 13: return clip();
        case 14: return expression(48);
        case 15: return prefixed(69);
        case 19: return 68;
        case 20: return expression(60);
        case 21: return 80;
        case 22: return expression(60);
        case 26: {
            std::size_t size = 136;
            for (int i = 0; i < 5; i++) size += 4 + u32(size);
            return size;
        }
        case 27: return 48 + u32(32) + u32(36) + u32(40) + 16 * (u32(4) >> 1) + 4 * u32(44);
        case 28: return 116 + u32(112) + u32(8) + 4 * u32(12);
        case 29: return 136;
        case 30: return 160;
        case 31: return 228;
        case 54: return expression(48);
        case 59: return 96;
        case 61: return expression(84);
        case 65: return 52;
        case 70: return 68;
        case 73: return 56;
        case 74: return expression(44);
        case 76: return 108;
        case 77: return expression(92);
        case 79: return 56;
        case 80: return expression(60);
        case 82: return 68;
        case 83: return expression(44);
        case 86: return 40;
        case 87: return expression(24);
        case 88: return 36 + 2 * u32(32);
        case 90: return expression(36);
        case 95: return 36;
        case 97: return 85;
        case 98: return expression(48);
        case 99: return 20;
        case 108: return prefixed(257);
        case 113: return 136;
        case 114: return expression(40);
        case 115: return 24;
        case 118: return prefixed(8);
        case 119: return 16;
        case 120: {
            std::size_t size = 16 + u32(8);
            for (std::size_t i = 0, n = u32(12); i < n; i++) size += 4 + u32(size);
            return size;
        }
        case 122: return prefixed(4);
        case 123: return 32;
        case 126: return 20;
        case 128: return 20;
        case 131: return 32;
        case 133: return 252;
        case 135: return 48;
        case 136: return 44;
        case 139: {
            std::size_t size = 204 + u32(184) + u32(188) + u32(192) + u32(196) + u32(200);
            if (u32(4) & 0x400000) size += 8 * u32(180);
            return size;
        }
        case 140: return prefixed(4);
        case 141: return 40;
        case 142: return clip();
        case 145: return 20;
        case 146: return clip();
        case 149: return optionalStrings(20);
        case 150: return optionalStrings(64);
        case 151: return 16;
        case 152: return clip();
        case 153: return 16;
        case 154: return clip();
        default: break;
    }
    throw std::runtime_error("efx: unknown item type " + std::to_string(type) + " at " + std::to_string(pos - 4));
}

const char* ItemNameRE7(uint32_t type) {
    switch (type) {
        case 1: return "FixRandomGenerator";
        case 3: return "Spawn";
        case 4: return "SpawnExpression";
        case 5: return "Transform2D";
        case 10: return "Transform3D";
        case 13: return "Transform3DClip";
        case 14: return "Transform3DExpression";
        case 15: return "ParentOptions";
        case 19: return "TypeBillboard2D";
        case 20: return "TypeBillboard2DExpression";
        case 21: return "TypeBillboard3D";
        case 22: return "TypeBillboard3DExpression";
        case 26: return "TypeMesh";
        case 27: return "TypeMeshClip";
        case 28: return "TypeMeshExpression";
        case 29: return "TypeRibbonFollow";
        case 30: return "TypeRibbonLength";
        case 31: return "TypeRibbonChain";
        case 54: return "TypeRibbonLengthExpression";
        case 70: return "TypeNoDraw";
        case 73: return "Velocity2D";
        case 74: return "Velocity2DExpression";
        case 76: return "Velocity3D";
        case 77: return "Velocity3DExpression";
        case 79: return "RotateAnim";
        case 80: return "RotateAnimExpression";
        case 82: return "ScaleAnim";
        case 83: return "ScaleAnimExpression";
        case 86: return "Life";
        case 87: return "LifeExpression";
        case 88: return "UVSequence";
        case 90: return "UVSequenceExpression";
        case 97: return "EmitterShape3D";
        case 98: return "EmitterShape3DExpression";
        case 113: return "ShaderSettings";
        case 115: return "Distortion";
        case 120: return "PtBehavior";
        case 139: return "FluidSimulator2D";
        case 122: return "PlayEfx";
        case 140: return "PlayEmitter";
        case 141: return "PtTransform3D";
        case 142: return "PtTransform3DClip";
        case 151: return "PtColor";
        case 152: return "PtColorClip";
        case 153: return "PtUvSequence";
        case 154: return "PtUvSequenceClip";
        default: return nullptr;
    }
}

// How re8.exe's ItemX::initialize reads each item; checked against every RE8 .efx.
struct SizeRule {
    enum class Kind : uint8_t {
        Fixed,                 // size bytes
        Expression,            // size bytes ending in solverSize, then the solvers
        Clip,                  // size bytes ending in ClipData's property, key and interpolator block sizes
        MaterialClip,          // ClipData + updateIndexCount, clip blocks, 16-byte parameters, update indices
        MaterialExpression,    // ItemTypeCustomMaterialBaseExpression first; solverSize last
        CustomMaterial,        // size bytes ending in ItemCustomMaterial
        Prefixed,              // size bytes, then count u32-length-prefixed blocks
        Utf16,                 // size bytes; UTF-16 strings whose char counts sit at offsets
        RibbonMaterial,        // the ribbon type's data, flags, ItemCustomMaterial
        PtBehavior,            // class name, then named parameters
        FluidSimulator,
        StrainRibbonMaterial,
        CustomComputeShader,
    };
    Kind kind = Kind::Fixed;
    uint16_t size = 0;
    uint16_t count = 0;  // Prefixed: blocks; RibbonMaterial: the ribbon's item type; Utf16: offsets used
    std::array<uint16_t, 5> offsets{};
};

constexpr SizeRule Rule(SizeRule::Kind kind, uint16_t size = 0, uint16_t count = 0) {
    SizeRule rule;
    rule.kind = kind;
    rule.size = size;
    rule.count = count;
    return rule;
}

constexpr SizeRule Fixed(uint16_t size) { return Rule(SizeRule::Kind::Fixed, size); }
constexpr SizeRule Expression(uint16_t size) { return Rule(SizeRule::Kind::Expression, size); }
constexpr SizeRule Clip(uint16_t size) { return Rule(SizeRule::Kind::Clip, size); }
constexpr SizeRule MaterialClip() { return Rule(SizeRule::Kind::MaterialClip); }
constexpr SizeRule MaterialExpression(uint16_t size) { return Rule(SizeRule::Kind::MaterialExpression, size); }
constexpr SizeRule CustomMaterial(uint16_t size) { return Rule(SizeRule::Kind::CustomMaterial, size); }
constexpr SizeRule Prefixed(uint16_t size, uint16_t blocks) { return Rule(SizeRule::Kind::Prefixed, size, blocks); }
constexpr SizeRule RibbonMaterial(uint16_t ribbonType) { return Rule(SizeRule::Kind::RibbonMaterial, 0, ribbonType); }
constexpr SizeRule PtBehavior() { return Rule(SizeRule::Kind::PtBehavior); }
constexpr SizeRule FluidSimulator() { return Rule(SizeRule::Kind::FluidSimulator); }
constexpr SizeRule StrainRibbonMaterial() { return Rule(SizeRule::Kind::StrainRibbonMaterial); }
constexpr SizeRule CustomComputeShader() { return Rule(SizeRule::Kind::CustomComputeShader); }

template <typename... Offsets>
constexpr SizeRule Utf16(uint16_t size, Offsets... lengthOffsets) {
    SizeRule rule = Rule(SizeRule::Kind::Utf16, size, sizeof...(lengthOffsets));
    rule.offsets = { static_cast<uint16_t>(lengthOffsets)... };
    return rule;
}

struct ItemInfo {
    uint16_t type;
    const char* name;
    SizeRule rule;
};

// via.effect.graph.ItemType; 119 has no class in RE8.
constexpr ItemInfo RE8_ITEMS[] = {
    { 1, "FixRandomGenerator", Fixed(44) },
    { 2, "EffectOptimizeShader", Prefixed(16, 1) },
    { 3, "Spawn", Fixed(60) },
    { 4, "SpawnExpression", Expression(36) },
    { 5, "Transform2D", Fixed(24) },
    { 6, "Transform2DModifierDelayFrame", Fixed(12) },
    { 7, "Transform2DModifier", Fixed(128) },
    { 8, "Transform2DClip", Clip(44) },
    { 9, "Transform2DExpression", Expression(32) },
    { 10, "Transform3D", Fixed(44) },
    { 11, "Transform3DModifierDelayFrame", Fixed(12) },
    { 12, "Transform3DModifier", Fixed(224) },
    { 13, "Transform3DClip", Clip(44) },
    { 14, "Transform3DExpression", Expression(48) },
    { 15, "ParentOptions", Prefixed(69, 1) },
    { 16, "EmitterColor", Fixed(16) },
    { 17, "EmitterColorClip", Clip(44) },
    { 18, "PtSort", Fixed(8) },
    { 19, "TypeBillboard2D", Fixed(68) },
    { 20, "TypeBillboard2DExpression", Expression(60) },
    { 21, "TypeBillboard3D", Fixed(76) },
    { 22, "TypeBillboard3DExpression", Expression(60) },
    { 23, "TypeBillboard3DMaterial", CustomMaterial(88) },
    { 24, "TypeBillboard3DMaterialClip", MaterialClip() },
    { 25, "TypeBillboard3DMaterialExpression", MaterialExpression(72) },
    { 26, "TypeMesh", Prefixed(136, 5) },
    { 27, "TypeMeshClip", MaterialClip() },
    { 28, "TypeMeshExpression", MaterialExpression(116) },
    { 29, "TypeRibbonFollow", Fixed(132) },
    { 30, "TypeRibbonLength", Fixed(156) },
    { 31, "TypeRibbonChain", Fixed(216) },
    { 32, "TypeRibbonFixEnd", Fixed(128) },
    { 33, "TypeRibbonLightweight", Fixed(60) },
    { 34, "TypeRibbonParticle", Fixed(112) },
    { 35, "TypeRibbonFollowMaterial", RibbonMaterial(29) },
    { 36, "TypeRibbonFollowMaterialClip", MaterialClip() },
    { 37, "TypeRibbonFollowMaterialExpression", MaterialExpression(56) },
    { 38, "TypeRibbonLengthMaterial", RibbonMaterial(30) },
    { 39, "TypeRibbonLengthMaterialClip", MaterialClip() },
    { 40, "TypeRibbonLengthMaterialExpression", MaterialExpression(56) },
    { 41, "TypeRibbonChainMaterial", RibbonMaterial(31) },
    { 42, "TypeRibbonChainMaterialClip", MaterialClip() },
    { 43, "TypeRibbonChainMaterialExpression", MaterialExpression(56) },
    { 44, "TypeRibbonFixEndMaterial", RibbonMaterial(32) },
    { 45, "TypeRibbonFixEndMaterialClip", MaterialClip() },
    { 46, "TypeRibbonFixEndMaterialExpression", MaterialExpression(56) },
    { 47, "TypeRibbonLightweightMaterial", RibbonMaterial(33) },
    { 48, "TypeRibbonLightweightMaterialClip", MaterialClip() },
    { 49, "TypeRibbonLightweightMaterialExpression", MaterialExpression(56) },
    { 50, "TypeStrainRibbonMaterial", StrainRibbonMaterial() },
    { 51, "TypeStrainRibbonMaterialClip", MaterialClip() },
    { 52, "TypeStrainRibbonMaterialExpression", MaterialExpression(80) },
    { 53, "TypeRibbonFollowExpression", Expression(48) },
    { 54, "TypeRibbonLengthExpression", Expression(48) },
    { 55, "TypeRibbonChainExpression", Expression(48) },
    { 56, "TypeRibbonFixEndExpression", Expression(48) },
    { 57, "TypeRibbonLightweightExpression", Expression(48) },
    { 58, "TypeRibbonParticleExpression", Expression(48) },
    { 59, "TypePolygon", Fixed(92) },
    { 60, "TypePolygonClip", Clip(44) },
    { 61, "TypePolygonExpression", Expression(84) },
    { 62, "TypePolygonMaterial", CustomMaterial(108) },
    { 63, "TypePolygonMaterialExpression", MaterialExpression(88) },
    { 64, "TypeRibbonTrail", Fixed(48) },
    { 65, "TypePolygonTrail", Fixed(84) },
    { 66, "TypePolygonTrailExpression", Expression(40) },
    { 67, "TypePolygonTrailMaterial", CustomMaterial(100) },
    { 68, "TypePolygonTrailMaterialExpression", MaterialExpression(48) },
    { 69, "TypeNoDraw", Fixed(68) },
    { 70, "TypeNoDrawExpression", Expression(76) },
    { 71, "Velocity2DDelayFrame", Fixed(12) },
    { 72, "Velocity2D", Fixed(56) },
    { 73, "Velocity2DExpression", Expression(44) },
    { 74, "Velocity3DDelayFrame", Fixed(12) },
    { 75, "Velocity3D", Fixed(108) },
    { 76, "Velocity3DExpression", Expression(92) },
    { 77, "RotateAnimDelayFrame", Fixed(12) },
    { 78, "RotateAnim", Fixed(56) },
    { 79, "RotateAnimExpression", Expression(60) },
    { 80, "ScaleAnimDelayFrame", Fixed(12) },
    { 81, "ScaleAnim", Fixed(68) },
    { 82, "ScaleAnimExpression", Expression(20) },
    { 83, "VanishArea3D", Prefixed(52, 1) },
    { 84, "VanishArea3DExpression", Expression(68) },
    { 85, "Life", Fixed(40) },
    { 86, "LifeExpression", Expression(24) },
    { 87, "UVSequence", Utf16(36, 32) },
    { 88, "UVSequenceModifier", Fixed(32) },
    { 89, "UVSequenceExpression", Expression(36) },
    { 90, "UVScroll", Fixed(56) },
    { 91, "TextureUnit", Utf16(488, 476, 480, 484) },
    { 92, "TextureUnitExpression", Expression(488) },
    { 93, "TextureFilter", Fixed(16) },
    { 94, "EmitterShape2D", Fixed(36) },
    { 95, "EmitterShape2DExpression", Expression(32) },
    { 96, "EmitterShape3D", Fixed(85) },
    { 97, "EmitterShape3DExpression", Expression(48) },
    { 98, "AlphaCorrection", Fixed(20) },
    { 99, "ContrastHighlighter", Fixed(28) },
    { 100, "ColorGrading", Fixed(28) },
    { 101, "Blink", Fixed(44) },
    { 102, "Noise", Fixed(36) },
    { 103, "TexelChannelOperator", Fixed(72) },
    { 104, "TexelChannelOperatorClip", Clip(44) },
    { 105, "TexelChannelOperatorExpression", Expression(56) },
    { 106, "TypeStrainRibbon", Prefixed(168 + 85, 1) },
    { 107, "TypeStrainRibbonExpression", Expression(72) },
    { 108, "TypeLightning3D", Fixed(308) },
    { 109, "TypeLightning3DExpression", Expression(128) },
    { 110, "TypeLightning3DMaterial", CustomMaterial(324) },
    { 111, "ShaderSettings", Fixed(112) },
    { 112, "ShaderSettingsExpression", Expression(40) },
    { 113, "Distortion", Fixed(24) },
    { 114, "DistortionExpression", Expression(32) },
    { 115, "VolumetricLighting", Fixed(20) },
    { 116, "RenderTarget", Prefixed(8, 1) },
    { 117, "PtLife", Fixed(16) },
    { 118, "PtBehavior", PtBehavior() },
    { 120, "PlayEfx", Prefixed(4, 1) },
    { 121, "FadeByAngle", Fixed(32) },
    { 122, "FadeByAngleExpression", Expression(20) },
    { 123, "FadeByEmitterAngle", Fixed(28) },
    { 124, "FadeByDepth", Fixed(20) },
    { 125, "FadeByDepthExpression", Expression(28) },
    { 126, "FadeByOcclusion", Fixed(20) },
    { 127, "FadeByOcclusionExpression", Expression(28) },
    { 128, "FakeDoF", Fixed(24) },
    { 129, "LuminanceBleed", Fixed(32) },
    { 130, "ScaleByDepth", Fixed(32) },
    { 131, "TypeNodeBillboard", Fixed(252) },
    { 132, "TypeNodeBillboardExpression", Expression(184) },
    { 133, "UnitCulling", Fixed(48) },
    { 134, "FluidEmitter2D", Fixed(44) },
    { 135, "FluidEmitter2DClip", Clip(44) },
    { 136, "FluidEmitter2DExpression", Expression(20) },
    { 137, "FluidSimulator2D", FluidSimulator() },
    { 138, "PlayEmitter", Prefixed(4, 1) },
    { 139, "PtTransform3D", Fixed(40) },
    { 140, "PtTransform3DClip", Clip(44) },
    { 141, "PtTransform2D", Fixed(24) },
    { 142, "PtTransform2DClip", Clip(44) },
    { 143, "PtVelocity3D", Fixed(20) },
    { 144, "PtVelocity3DClip", Clip(44) },
    { 145, "PtVelocity2D", Fixed(16) },
    { 146, "PtVelocity2DClip", Clip(44) },
    { 147, "PtColliderAction", Fixed(20) },
    { 148, "PtCollision", Fixed(64) },
    { 149, "PtColor", Fixed(16) },
    { 150, "PtColorClip", Clip(44) },
    { 151, "PtUvSequence", Fixed(16) },
    { 152, "PtUvSequenceClip", Clip(44) },
    { 153, "MeshEmitter", Utf16(104, 0x54, 0x58, 0x5C, 0x60, 0x64) },
    { 154, "MeshEmitterClip", Clip(44) },
    { 155, "MeshEmitterExpression", Expression(76) },
    { 156, "ScreenSpaceEmitter", Fixed(12) },
    { 157, "VectorFieldParameter", Fixed(84) },
    { 158, "VectorFieldParameterClip", Clip(44) },
    { 159, "VectorFieldParameterExpression", Expression(92) },
    { 160, "GlobalVectorField", Fixed(48) },
    { 161, "GlobalVectorFieldClip", Clip(44) },
    { 162, "GlobalVectorFieldExpression", Expression(56) },
    { 163, "DirectionalFieldParameter", Fixed(60) },
    { 164, "DirectionalFieldParameterClip", Clip(44) },
    { 165, "DirectionalFieldParameterExpression", Expression(60) },
    { 166, "DepthOperator", Fixed(20) },
    { 167, "PlaneCollider", Fixed(68) },
    { 168, "PlaneColliderExpression", Expression(60) },
    { 169, "DepthOcclusion", Fixed(8) },
    { 170, "ShapeOperator", Fixed(48) },
    { 171, "ShapeOperatorExpression", Expression(56) },
    { 172, "WindInfluence3DDelayFrame", Fixed(12) },
    { 173, "WindInfluence3D", Fixed(28) },
    { 174, "Attractor", Prefixed(36, 1) },
    { 175, "AttractorClip", Clip(44) },
    { 176, "AttractorExpression", Expression(52) },
    { 177, "CustomComputeShader", CustomComputeShader() },
    { 178, "TypeGpuBillboard", Fixed(56) },
    { 179, "TypeGpuBillboardExpression", Expression(40) },
    { 180, "TypeGpuPolygon", Fixed(88) },
    { 181, "TypeGpuPolygonExpression", Expression(80) },
    { 182, "TypeGpuRibbonFollow", Fixed(64) },
    { 183, "TypeGpuRibbonFollowExpression", Expression(40) },
    { 184, "TypeGpuRibbonLength", Fixed(104) },
    { 185, "TypeGpuRibbonLengthExpression", Expression(56) },
    { 186, "TypeGpuMesh", Prefixed(96, 5) },
    { 187, "TypeGpuMeshExpression", Expression(88) },
    { 188, "TypeGpuMeshTrail", Prefixed(184, 5) },
    { 189, "TypeGpuMeshTrailClip", Clip(44) },
    { 190, "TypeGpuMeshTrailExpression", Expression(44) },
    { 191, "TypeGpuLightning3D", Fixed(352) },
    { 192, "EmitterPriority", Fixed(4) },
    { 193, "DrawOverlay", Fixed(16) },
    { 194, "VectorField", Utf16(32, 28) },
    { 195, "VolumeField", Fixed(28) },
    { 196, "DirectionalField", Prefixed(4, 1) },
    { 197, "AngularVelocity3DDelayFrame", Fixed(12) },
    { 198, "AngularVelocity3D", Fixed(84) },
    { 199, "PtAngularVelocity3D", Fixed(36) },
    { 200, "PtAngularVelocity3DExpression", Expression(40) },
    { 201, "AngularVelocity2DDelayFrame", Fixed(12) },
    { 202, "AngularVelocity2D", Fixed(64) },
    { 203, "PtAngularVelocity2D", Fixed(28) },
    { 204, "PtAngularVelocity2DExpression", Expression(36) },
    { 205, "IgnorePlayerColor", Fixed(4) },
    { 206, "ProceduralDistortionDelayFrame", Fixed(12) },
    { 207, "ProceduralDistortion", Fixed(80) },
    { 208, "ProceduralDistortionClip", Clip(44) },
    { 209, "ProceduralDistortionExpression", Expression(84) },
    { 210, "TestBehaviorUpdater", Fixed(16) },
    { 211, "StretchBlur", Fixed(44) },
    { 212, "StretchBlurExpression", Expression(32) },
    { 213, "EmitterHSV", Fixed(88) },
    { 214, "EmitterHSVExpression", Expression(96) },
    { 215, "FlowMap", Fixed(36) },
    { 216, "RgbCommon", Fixed(96) },
    { 217, "RgbWater", Fixed(115) },
    { 218, "PtFreezer", Fixed(44) },
    { 219, "AssignCSV", Prefixed(14, 4) },
    { 220, "EmitMask", Fixed(8) },
    { 221, "TypeModularBillboard", Fixed(48) },
    { 222, "TypeModularRibbonFollow", Fixed(44) },
    { 223, "TypeModularRibbonLength", Fixed(84) },
    { 224, "TypeModularPolygon", Fixed(64) },
    { 225, "TypeModularMesh", Prefixed(81, 4) },
};

constexpr uint32_t RE8_ITEM_TYPES = 226;

const ItemInfo* FindRE8(uint32_t type) {
    static const std::array<const ItemInfo*, RE8_ITEM_TYPES> table = [] {
        std::array<const ItemInfo*, RE8_ITEM_TYPES> t{};
        for (const ItemInfo& info : RE8_ITEMS) t[info.type] = &info;
        return t;
    }();
    return type < RE8_ITEM_TYPES ? table[type] : nullptr;
}

std::size_t CustomMaterialTail(const MemoryReader& r, std::size_t material) {
    auto u32 = [&](std::size_t o) { return static_cast<std::size_t>(r.ReadAt<uint32_t>(material + o)); };
    return 32 * u32(8) + 2 * (u32(20) + u32(24) + u32(28));
}

std::size_t ItemSizeRE8(uint32_t type, const MemoryReader& r, std::size_t pos) {
    const ItemInfo* info = FindRE8(type);
    if (info == nullptr) {
        throw std::runtime_error("efx: unknown item type " + std::to_string(type) + " at " + std::to_string(pos - 4));
    }
    const SizeRule& rule = info->rule;
    auto u32 = [&](std::size_t o) { return static_cast<std::size_t>(r.ReadAt<uint32_t>(pos + o)); };
    std::size_t n = rule.size;
    switch (rule.kind) {
        case SizeRule::Kind::Fixed:
            return n;
        case SizeRule::Kind::Expression:
            return n + u32(n - 4);
        case SizeRule::Kind::Clip:
            return n + u32(n - 12) + u32(n - 8) + u32(n - 4);
        case SizeRule::Kind::MaterialClip:
            return 48 + u32(32) + u32(36) + u32(40) + 16 * (u32(8) >> 1) + 4 * u32(44);
        case SizeRule::Kind::MaterialExpression:
            return n + u32(n - 4) + u32(8) + 4 * u32(12);
        case SizeRule::Kind::CustomMaterial:
            return n + CustomMaterialTail(r, pos + n - 32);
        case SizeRule::Kind::Prefixed: {
            std::size_t size = n;
            for (uint16_t i = 0; i < rule.count; i++) size += 4 + u32(size);
            return size;
        }
        case SizeRule::Kind::Utf16: {
            std::size_t size = n;
            for (uint16_t i = 0; i < rule.count; i++) size += 2 * u32(rule.offsets[i]);
            return size;
        }
        case SizeRule::Kind::RibbonMaterial: {
            std::size_t size = ItemSizeRE8(rule.count, r, pos) + 4;
            return size + 32 + CustomMaterialTail(r, pos + size);
        }
        case SizeRule::Kind::PtBehavior: {
            std::size_t size = 16 + u32(8);
            for (std::size_t i = 0, count = u32(12); i < count; i++) size += 4 + u32(size);
            return size;
        }
        case SizeRule::Kind::FluidSimulator: {
            std::size_t size = 200 + u32(180) + u32(184) + u32(188) + u32(192) + u32(196);
            if (u32(4) & 0x400000) size += 8 * u32(176);
            return size;
        }
        case SizeRule::Kind::StrainRibbonMaterial: {
            // Its own 8 bytes, the StrainRibbon (members, terminal EmitterShape3D, joint name), the material.
            std::size_t size = 8 + 253;
            size += 4 + u32(size);
            return size + 32 + CustomMaterialTail(r, pos + size);
        }
        case SizeRule::Kind::CustomComputeShader:
            return 20 + 20 * u32(8) + 2 * u32(16) + 4 * (u32(4) + 1);
    }
    return n;
}

std::string ReadFixedWString(const MemoryReader& r, std::size_t offset, std::size_t chars) {
    std::string result;
    for (std::size_t i = 0; i < chars; i++) {
        uint16_t ch = r.ReadAt<uint16_t>(offset + i * 2);
        if (ch == 0) break;
        result += ch < 0x80 ? static_cast<char>(ch) : '?';
    }
    return result;
}

// The item kinds the simulator reads. Payload offsets; every payload starts with a u32 uid.
enum class ItemKind { Other, Spawn, Transform3D, Billboard3D, Velocity3D, RotateAnim, ScaleAnim, Life, UVSequence,
                      Shape3D, ShaderSettings, Distortion, RibbonLength, Mesh, RibbonLengthMaterial, PolygonMaterial };

ItemKind KindRE7(uint32_t type) {
    switch (type) {
        case 3: return ItemKind::Spawn;
        case 10: return ItemKind::Transform3D;
        case 21: return ItemKind::Billboard3D;
        case 76: return ItemKind::Velocity3D;
        case 79: return ItemKind::RotateAnim;
        case 82: return ItemKind::ScaleAnim;
        case 86: return ItemKind::Life;
        case 88: return ItemKind::UVSequence;
        case 97: return ItemKind::Shape3D;
        case 113: return ItemKind::ShaderSettings;
        case 115: return ItemKind::Distortion;
        default: return ItemKind::Other;
    }
}

ItemKind KindRE8(uint32_t type) {
    switch (type) {
        case 3: return ItemKind::Spawn;
        case 10: return ItemKind::Transform3D;
        case 21: return ItemKind::Billboard3D;
        case 26: return ItemKind::Mesh;
        case 30: return ItemKind::RibbonLength;
        case 38: return ItemKind::RibbonLengthMaterial;
        case 62: return ItemKind::PolygonMaterial;
        case 75: return ItemKind::Velocity3D;
        case 78: return ItemKind::RotateAnim;
        case 81: return ItemKind::ScaleAnim;
        case 85: return ItemKind::Life;
        case 87: return ItemKind::UVSequence;
        case 96: return ItemKind::Shape3D;
        case 111: return ItemKind::ShaderSettings;
        case 113: return ItemKind::Distortion;
        default: return ItemKind::Other;
    }
}

// ItemCustomMaterial at pos: a 32-byte header (parameter count at 8, string lengths in chars at 20, 24 and 28),
// 32-byte parameters, then the mdf2, master material and NUL-separated texture strings.
// MaterialParameter, 32 bytes.
EfxMaterialParam ReadMaterialParam(const MemoryReader& r, std::size_t p) {
    EfxMaterialParam param;
    param.nameHash = r.ReadAt<uint32_t>(p);
    param.type = r.ReadAt<uint16_t>(p + 10);
    for (int c = 0; c < 4; c++) param.values[c] = r.ReadAt<float>(p + 16 + c * 4);
    if (param.type == 3) param.textureIndex = r.ReadAt<int32_t>(p + 20);
    return param;
}

EfxCustomMaterial ReadCustomMaterial(const MemoryReader& r, std::size_t pos) {
    EfxCustomMaterial m;
    uint32_t count = r.ReadAt<uint32_t>(pos + 8);
    uint32_t chars[3] = { r.ReadAt<uint32_t>(pos + 20), r.ReadAt<uint32_t>(pos + 24), r.ReadAt<uint32_t>(pos + 28) };
    std::size_t p = pos + 32;
    for (uint32_t i = 0; i < count; i++, p += 32) m.params.push_back(ReadMaterialParam(r, p));
    m.mdfPath = ReadFixedWString(r, p, chars[0]);
    p += chars[0] * 2;
    m.masterPath = ReadFixedWString(r, p, chars[1]);
    p += chars[1] * 2;
    std::size_t end = p + chars[2] * 2;
    while (p < end) {
        std::string texture = ReadFixedWString(r, p, (end - p) / 2);
        p += (texture.size() + 1) * 2;
        if (!texture.empty()) m.textures.push_back(std::move(texture));
    }
    return m;
}

void ParseItem(ItemKind kind, EfxReader::Game game, const MemoryReader& r, std::size_t pos, EfxEmitter& emitter) {
    auto f32 = [&](std::size_t o) { return r.ReadAt<float>(pos + o); };
    auto u32 = [&](std::size_t o) { return r.ReadAt<uint32_t>(pos + o); };
    auto range = [&](std::size_t o) { return EfxRange{ f32(o), f32(o + 4) }; };
    auto rangeI = [&](std::size_t o) { return EfxRangeI{ r.ReadAt<int32_t>(pos + o), r.ReadAt<int32_t>(pos + o + 4) }; };
    auto minMax = [&](std::size_t o) { return EfxMinMax{ f32(o), f32(o + 4) }; };
    auto rgba = [&](std::size_t o, uint8_t out[4]) {
        for (int i = 0; i < 4; i++) out[i] = r.ReadAt<uint8_t>(pos + o + i);
    };

    switch (kind) {
        case ItemKind::Spawn: {
            EfxSpawn s;
            s.maxParticles = u32(4);
            s.spawnNum = rangeI(12);
            s.intervalFrame = rangeI(20);
            s.loopNum = rangeI(40);
            s.emitterDelayFrame = rangeI(48);
            s.ringBuffer = u32(56) != 0;
            emitter.spawn = s;
            break;
        }
        case ItemKind::Transform3D: {
            EfxTransform3D t;
            for (int i = 0; i < 3; i++) {
                t.position[i] = f32(4 + i * 4);
                t.rotation[i] = f32(16 + i * 4);
                t.scale[i] = f32(28 + i * 4);
            }
            t.rotationOrder = u32(40);
            emitter.transform = t;
            break;
        }
        case ItemKind::Billboard3D: {
            EfxBillboard3D b;
            b.flags = u32(4);
            rgba(8, b.color);
            rgba(12, b.colorRange);
            b.emissiveRate = f32(16);
            b.intensity = f32(20);
            b.edgeBlendRange = f32(24);
            b.rotation = range(28);
            b.sizeScalar = range(36);
            b.sizeX = range(44);
            b.sizeY = range(52);
            b.alphaRate = f32(60);
            // RE8 dropped shadowMultiplier.
            std::size_t tail = game == EfxReader::Game::RE7 ? 68 : 64;
            if (game == EfxReader::Game::RE7) b.shadowMultiplier = f32(64);
            b.flags2 = u32(tail);
            b.offset[0] = f32(tail + 4);
            b.offset[1] = f32(tail + 8);
            emitter.billboard = b;
            break;
        }
        case ItemKind::Velocity3D: {
            EfxVelocity3D v;
            v.flags = u32(4);
            for (int i = 0; i < 3; i++) {
                v.direction[i] = range(8 + i * 8);
                v.offset[i] = f32(48 + i * 4);
                v.size[i] = f32(60 + i * 4);
            }
            v.speed = range(32);
            v.speedCoef = range(40);
            v.type = static_cast<EfxVelocityType>(u32(72));
            v.gravityRate = range(76);
            v.inheritRate = range(84);
            v.inheritDistance = range(92);
            v.spread = range(100);
            emitter.velocity = v;
            break;
        }
        case ItemKind::RotateAnim: {
            EfxRotateAnim a;
            a.flags = u32(4);
            for (int i = 0; i < 3; i++) {
                a.add[i] = range(8 + i * 8);
                a.coef[i] = range(32 + i * 8);
            }
            emitter.rotateAnim = a;
            break;
        }
        case ItemKind::ScaleAnim: {
            EfxScaleAnim a;
            a.scalarAdd = range(4);
            a.scalarCoef = range(12);
            a.addX = range(20);
            a.coefX = range(28);
            a.addY = range(36);
            a.coefY = range(44);
            a.addZ = range(52);
            a.coefZ = range(60);
            emitter.scaleAnim = a;
            break;
        }
        case ItemKind::Life: {
            EfxLife l;
            l.appearFrame = rangeI(4);
            l.keepFrame = rangeI(12);
            l.vanishFrame = rangeI(20);
            l.keepHoldFrame = rangeI(28);
            l.flags = u32(36);
            emitter.life = l;
            break;
        }
        case ItemKind::UVSequence: {
            EfxUVSequence u;
            u.sequenceNo = rangeI(4);
            u.patternNo = rangeI(12);
            u.playSpeed = minMax(20);
            u.flags = u32(28);
            u.uvsPath = ReadFixedWString(r, pos + 36, u32(32));
            emitter.uvSequence = u;
            break;
        }
        case ItemKind::Shape3D: {
            EfxShape3D s;
            for (int i = 0; i < 3; i++) {
                s.range[i] = minMax(4 + i * 8);
                s.localRotation[i] = f32(48 + i * 4);
            }
            s.type = static_cast<EfxShapeType>(u32(28));
            s.divideNum = u32(32);
            s.divideAxis = u32(36);
            s.rotationOrder = u32(60);
            // Packed after a 1-byte flag, hence the unaligned offsets.
            s.arcStart = f32(69);
            s.arcLength = f32(73);
            emitter.shape = s;
            break;
        }
        case ItemKind::Mesh: {
            // TypeMesh::initialize: 136 fixed bytes, then size-prefixed mesh path, mirror mesh path, mdf2 path,
            // material parameters and material string.
            EfxTypeMesh m;
            std::size_t block = pos + 136;
            for (int b = 0; b < 3; b++) {
                uint32_t size = r.ReadAt<uint32_t>(block);
                if (b == 0) m.meshPath = ReadFixedWString(r, block + 4, size / 2);
                if (b == 2) m.mdfPath = ReadFixedWString(r, block + 4, size / 2);
                block += 4 + size;
            }
            uint32_t paramBytes = r.ReadAt<uint32_t>(block);
            for (uint32_t k = 0; k + 32 <= paramBytes; k += 32) m.params.push_back(ReadMaterialParam(r, block + 4 + k));
            emitter.mesh = std::move(m);
            break;
        }
        case ItemKind::ShaderSettings: {
            EfxShaderSettings s;
            s.softParticleDistance = f32(4);
            s.lightingType = u32(8);
            s.flags = u32(24);
            s.lightShadowRatio = f32(68);
            s.backfaceLightRatio = f32(72) * 8.0f;
            s.detonemap = u32(80) == 1;
            s.detonemapRate = f32(84);
            emitter.shaderSettings = s;
            break;
        }
        case ItemKind::Distortion:
            emitter.distortion = true;
            break;
        case ItemKind::RibbonLengthMaterial:
            // TypeRibbonLength's data, a flags word, then the material.
            ParseItem(ItemKind::RibbonLength, game, r, pos, emitter);
            emitter.material = ReadCustomMaterial(r, pos + 160);
            break;
        case ItemKind::PolygonMaterial: {
            EfxPolygon p;
            p.flags = u32(4);
            rgba(8, p.color);
            rgba(12, p.colorRange);
            p.rotationOrder = u32(16);
            for (int i = 0; i < 3; i++) p.rotation[i] = range(20 + i * 8);
            p.sizeScalar = range(44);
            p.width = range(52);
            p.height = range(60);
            p.offset[0] = f32(68);
            p.offset[1] = f32(72);
            emitter.polygon = p;
            emitter.material = ReadCustomMaterial(r, pos + 76);
            break;
        }
        case ItemKind::RibbonLength: {
            // ItemRibbonBase (52 bytes), then ItemRibbonLength's own fields.
            EfxRibbonLength l;
            l.flags = u32(4);
            rgba(8, l.color);
            rgba(12, l.colorRange);
            l.colorRate = f32(16);
            l.intensity = f32(20);
            l.edgeBlendRange = f32(24);
            l.sizeScalar = range(28);
            l.width = range(36);
            l.texRepeatNum = f32(44);
            l.alphaRate = f32(48);
            l.lengthFlags = u32(52);
            l.length = range(56);
            l.shapeDivision = u32(64);
            l.basingPoint[0] = f32(68);
            l.basingPoint[1] = f32(72);
            l.releaseFixEnd = range(76);
            for (int i = 0; i < 3; i++) l.direction[i] = range(84 + i * 8);
            l.colorPlace = { u32(108), u32(112), u32(116), f32(120), f32(124) };
            l.scalePlace = { f32(128), f32(132), f32(136), f32(140), f32(144) };
            l.ghostStretch = range(148);
            emitter.ribbonLength = l;
            break;
        }
        case ItemKind::Other:
            break;
    }
}

}

const char* EfxReader::ItemName(uint32_t type) const {
    if (game == Game::RE7) return ItemNameRE7(type);
    const ItemInfo* info = FindRE8(type);
    return info ? info->name : nullptr;
}

// Unread header fields: 0x1C group count, 0x20 groupDataSize.
EffectData EfxReader::Read(std::span<const uint8_t> data) const {
    MemoryReader r(data);
    if (r.ReadAt<uint32_t>(0x00) != 0x72786665) {
        throw std::runtime_error("efx: bad magic");
    }

    EffectData effect;
    effect.version = r.ReadAt<uint32_t>(0x04);
    uint32_t emitterCount = r.ReadAt<uint32_t>(0x08);
    uint32_t stringSize = r.ReadAt<uint32_t>(0x0C);
    uint32_t actionCount = r.ReadAt<uint32_t>(0x10);
    uint32_t fieldCount = r.ReadAt<uint32_t>(0x14);
    uint32_t externCount = r.ReadAt<uint32_t>(0x18);
    uint32_t bindingCount = r.ReadAt<uint32_t>(0x24);
    uint32_t propertyIndexCount = r.ReadAt<uint32_t>(0x28);

    // Binding params put ASCII+UTF-16 name pairs first; action, field and
    // emitter names (UTF-8) close the table in that order.
    std::vector<std::string> strings;
    {
        std::size_t begin = 0x30;
        std::size_t end = begin + stringSize;
        std::string current;
        for (std::size_t p = begin; p < end; p++) {
            char c = static_cast<char>(r.ReadAt<uint8_t>(p));
            if (c == 0) {
                strings.push_back(std::move(current));
                current.clear();
            } else {
                current += c;
            }
        }
    }
    std::size_t namedCount = actionCount + fieldCount + emitterCount;
    std::size_t emitterNameBase = strings.size() >= namedCount ? strings.size() - emitterCount : SIZE_MAX;

    std::size_t pos = 0x30 + stringSize + 24ull * externCount + 8ull * bindingCount + 2ull * propertyIndexCount;

    auto skipContainer = [&](std::size_t headerSize, std::size_t countOffset, EfxEmitter* emitter) {
        uint32_t itemCount = r.ReadAt<uint32_t>(pos + countOffset);
        pos += headerSize;
        for (uint32_t i = 0; i < itemCount; i++) {
            uint32_t type = r.ReadAt<uint32_t>(pos);
            pos += 4;
            std::size_t size = game == Game::RE7 ? ItemSizeRE7(type, r, pos) : ItemSizeRE8(type, r, pos);
            r.BytesAt(pos, size);
            if (emitter != nullptr) {
                emitter->itemTypes.push_back(type);
                ParseItem(game == Game::RE7 ? KindRE7(type) : KindRE8(type), game, r, pos, *emitter);
            }
            pos += size;
        }
    };

    for (uint32_t i = 0; i < actionCount + fieldCount; i++) skipContainer(12, 8, nullptr);
    for (uint32_t i = 0; i < emitterCount; i++) {
        EfxEmitter emitter;
        if (emitterNameBase != SIZE_MAX) emitter.name = strings[emitterNameBase + i];
        skipContainer(16, 12, &emitter);
        effect.emitters.push_back(std::move(emitter));
    }
    return effect;
}
