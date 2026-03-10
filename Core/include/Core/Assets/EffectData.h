#ifndef REASSETEXPLORER_EFFECTDATA_H
#define REASSETEXPLORER_EFFECTDATA_H
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// via::Range: base + rand(-1, 1) * spread.
struct EfxRange {
    float base = 0;
    float spread = 0;
};

// Integer range sampled as min + rand % (max - min + 1).
struct EfxRangeI {
    int32_t min = 0;
    int32_t max = 0;
};

struct EfxMinMax {
    float min = 0;
    float max = 0;
};

struct EfxSpawn {
    uint32_t maxParticles = 0;
    EfxRangeI spawnNum;
    EfxRangeI intervalFrame;
    // {s, r}: s + rand % (r + 1) spawn events; {0, 0} loops forever.
    EfxRangeI loopNum;
    EfxRangeI emitterDelayFrame;
    bool ringBuffer = false;
};

enum class EfxLifeType : uint32_t {
    Normal = 0,
    // At the end of keep the particle holds for keepHoldFrame frames, forever when its min is 0.
    KeepHold = 1,
    // KeepHold followed by one more keep span.
    KeepHoldRepeat = 2
};

struct EfxLife {
    EfxRangeI appearFrame;
    EfxRangeI keepFrame;
    EfxRangeI vanishFrame;
    EfxRangeI keepHoldFrame;
    uint32_t flags = 0;

    EfxLifeType Type() const { return static_cast<EfxLifeType>(flags & 3); }
};

struct EfxTransform3D {
    float position[3]{};
    float rotation[3]{};  // radians
    float scale[3]{ 1, 1, 1 };
    uint32_t rotationOrder = 0;
};

enum class EfxBlend : uint32_t {
    AlphaBlend = 0,
    Physical = 1,
    AddContrast = 2,
    EdgeBlend = 3,
    Multiply = 4
};

struct EfxBillboard3D {
    uint32_t flags = 0;
    uint8_t color[4]{};
    uint8_t colorRange[4]{};
    float emissiveRate = 1;
    float intensity = 1;
    float edgeBlendRange = 0;
    EfxRange rotation;
    EfxRange sizeScalar;
    EfxRange sizeX;
    EfxRange sizeY;
    float alphaRate = 1;
    float shadowMultiplier = 1;
    uint32_t flags2 = 0;
    float offset[2]{};

    EfxBlend Blend() const { return static_cast<EfxBlend>(flags & 0xF); }
    bool ColorRangeEnabled() const { return (flags & 0x10) != 0; }
    bool PhysicalAlphaBlend() const { return (flags & 0x20) != 0; }
};

enum class EfxVelocityType : uint32_t {
    Direction = 0,
    Normal = 1,
    Radial = 2,
    Spread = 3
};

struct EfxVelocity3D {
    uint32_t flags = 0;
    EfxRange direction[3];
    EfxRange speed;
    EfxRange speedCoef;
    float offset[3]{};
    float size[3]{};
    EfxVelocityType type = EfxVelocityType::Direction;
    EfxRange gravityRate;
    EfxRange inheritRate;
    EfxRange inheritDistance;
    EfxRange spread;
};

struct EfxRotateAnim {
    uint32_t flags = 0;
    EfxRange add[3];
    EfxRange coef[3];

    bool ReverseRandom() const { return (flags & 1) != 0; }
};

struct EfxScaleAnim {
    EfxRange scalarAdd;
    EfxRange scalarCoef;
    EfxRange addX;
    EfxRange coefX;
    EfxRange addY;
    EfxRange coefY;
    EfxRange addZ;
    EfxRange coefZ;
};

// Past the last pattern: Loop restarts at 0, Finish and Clamp hold the last
// one (Finish also raises an end flag in the engine; particles live on).
enum class EfxUvPlayType : uint32_t {
    Pause = 0,
    Loop = 1,
    Finish = 2,
    Clamp = 3
};

struct EfxUVSequence {
    // sequenceNo/patternNo pick s + rand % max(1, r - s).
    EfxRangeI sequenceNo;
    EfxRangeI patternNo;
    EfxMinMax playSpeed;  // patterns per frame
    uint32_t flags = 0;
    std::string uvsPath;

    EfxUvPlayType PlayType() const { return static_cast<EfxUvPlayType>(flags & 3); }
    // Flip and PlayOrder share the encoding 0 = off, 1 = on, 2 = random per particle;
    // PlayOrder "on" plays the sequence backwards.
    uint32_t HFlip() const { return (flags >> 2) & 3; }
    uint32_t VFlip() const { return (flags >> 4) & 3; }
    uint32_t PlayOrder() const { return (flags >> 6) & 3; }
};

enum class EfxShapeType : uint32_t {
    Box = 0,
    Sphere = 1,
    Cylinder = 2
};

struct EfxShape3D {
    EfxMinMax range[3];
    EfxShapeType type = EfxShapeType::Box;
    uint32_t divideNum = 0;
    uint32_t divideAxis = 0;
    float localRotation[3]{};
    uint32_t rotationOrder = 0;
    float arcStart = 0;
    float arcLength = 6.2831853f;
};

struct EfxShaderSettings {
    float softParticleDistance = 0;
    // 0 none, 1 per particle, 2..4 a 2x2, 4x4 or 8x8 grid of lit points per particle.
    uint32_t lightingType = 0;
    uint32_t flags = 0;
    float lightShadowRatio = 0;
    float backfaceLightRatio = 0;
    // Divides the color by the camera exposure (auto exposure included), blended in by detonemapRate.
    bool detonemap = false;
    float detonemapRate = 1;

    // The material programs' pmcFlags: bit 0 reads the precalculated lighting, bit 1 applies the fog.
    uint32_t PrimitiveFlags() const { return (flags >> 9) & 3; }
    uint32_t LightGrid() const { return lightingType >= 2 && lightingType <= 4 ? 1u << (lightingType - 1) : 1u; }
};

// Values at the head, at two places along the ribbon and at its end (place2 holds to the end).
template <typename T>
struct EfxPlace {
    T head{};
    T place1{};
    T place2{};
    float place1Ratio = 0;
    float place2Ratio = 0;
};

// TypeRibbonLength (RE8): each particle draws a strip of shapeDivision points along a sampled
// direction; the particle sits basingPoint[1] of the way along it.
struct EfxRibbonLength {
    uint32_t flags = 0;
    uint8_t color[4]{};
    uint8_t colorRange[4]{};
    float colorRate = 1;
    float intensity = 1;
    float edgeBlendRange = 0;
    EfxRange sizeScalar;
    EfxRange width;
    float texRepeatNum = 1;
    float alphaRate = 1;
    uint32_t lengthFlags = 0;
    EfxRange length;
    uint32_t shapeDivision = 2;
    float basingPoint[2]{};  // across the width (unused here), along the length
    EfxRange releaseFixEnd;
    EfxRange direction[3];
    EfxPlace<uint32_t> colorPlace;  // RGBA8, red in the low byte
    EfxPlace<float> scalePlace;
    EfxRange ghostStretch;

    EfxBlend Blend() const { return static_cast<EfxBlend>(flags & 0xF); }
    bool ColorRangeEnabled() const { return (flags & 0x80) != 0; }
    bool PhysicalAlphaBlend() const { return (flags & 0x400) != 0; }
    uint32_t ColorPlaceType() const { return (lengthFlags >> 3) & 3; }
    uint32_t SizePlaceType() const { return (lengthFlags >> 5) & 3; }
};

// ItemCustomMaterial: the item draws with a master material's own programs; the mdf2 gives the defaults.
struct EfxMaterialParam {
    uint32_t nameHash = 0;  // Murmur3::HashAscii of the property or texture slot name
    uint16_t type = 0;      // 1 = color, 2 = float (values[0] + random in [values[2], values[3]]), 3 = texture
    float values[4]{};
    int32_t textureIndex = -1;  // type 3: into EfxCustomMaterial::textures
};

struct EfxCustomMaterial {
    std::string mdfPath;
    std::string masterPath;
    std::vector<std::string> textures;
    std::vector<EfxMaterialParam> params;
};

// TypePolygon*: a flat quad per particle, rotated by its own Euler angles.
struct EfxPolygon {
    uint32_t flags = 0;
    uint8_t color[4]{};
    uint8_t colorRange[4]{};
    uint32_t rotationOrder = 0;
    EfxRange rotation[3];  // radians
    EfxRange sizeScalar;
    EfxRange width;
    EfxRange height;
    float offset[2]{};

    bool ColorRangeEnabled() const { return (flags & 1) != 0; }
};

// TypeMesh: each particle draws the mesh with its mdf2.
struct EfxTypeMesh {
    std::string meshPath;
    std::string mdfPath;
    std::vector<EfxMaterialParam> params;  // over the mdf2's properties
};

struct EfxEmitter {
    std::string name;
    std::vector<uint32_t> itemTypes;
    std::optional<EfxSpawn> spawn;
    std::optional<EfxLife> life;
    std::optional<EfxTransform3D> transform;
    std::optional<EfxBillboard3D> billboard;
    std::optional<EfxVelocity3D> velocity;
    std::optional<EfxRotateAnim> rotateAnim;
    std::optional<EfxScaleAnim> scaleAnim;
    std::optional<EfxUVSequence> uvSequence;
    std::optional<EfxShape3D> shape;
    std::optional<EfxShaderSettings> shaderSettings;
    std::optional<EfxRibbonLength> ribbonLength;
    std::optional<EfxTypeMesh> mesh;
    std::optional<EfxPolygon> polygon;
    std::optional<EfxCustomMaterial> material;
    // Screen-space refraction: the billboard only offsets the scene behind it.
    bool distortion = false;
};

struct EffectData {
    uint32_t version = 0;
    std::vector<EfxEmitter> emitters;
};

struct UvsPattern {
    uint32_t flags = 0;
    float rect[4]{};  // left, top, right, bottom
    uint32_t textureIndex = 0;
};

struct UvsSequence {
    uint32_t patternCount = 0;
    uint32_t firstPattern = 0;
};

struct UvsData {
    std::vector<std::string> textures;
    std::vector<UvsSequence> sequences;
    std::vector<UvsPattern> patterns;
};

#endif
