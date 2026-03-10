#ifndef REASSETEXPLORER_EFFECTSIMULATOR_H
#define REASSETEXPLORER_EFFECTSIMULATOR_H
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "Core/Assets/EffectData.h"
#include "Renderer/RenderMath.h"
#include "Renderer/RenderTypes.h"

struct EffectSprite {
    float uvRect[4]{ 0, 0, 1, 1 };
    uint32_t textureIndex = 0;
    bool alphaGamma = false;
    float evPow2 = 1;
};

bool IsDrawableEmitter(const EfxEmitter& e);
// Drawn with its master material's own programs (CollectMaterial) instead of the particle shader.
bool IsMaterialEmitter(const EfxEmitter& e);
Mat4 EmitterLocalTransform(const EfxTransform3D& t);
// A TypeMesh emitter whose mesh stays in place: no velocity, and its particle holds forever or it respawns forever.
bool IsStaticMeshEmitter(const EfxEmitter& e);

// program indexes the program list the effect was loaded with; UserMaterial is the mdf2 block with the
// effect's overrides, its random floats drawn per instance.
struct EffectMaterialAssets {
    uint32_t program = 0;
    bool lit = false;  // a *Lighting program: reads the particles' ViewerParticleLight results
    std::vector<uint8_t> userMaterial;
    std::vector<std::pair<std::string, uint32_t>> textures;
    struct RandomFloat {
        uint32_t offset;
        float base;
        float low;
        float high;
    };
    std::vector<RandomFloat> randomFloats;
};

struct EffectEmitterAssets {
    std::vector<std::vector<EffectSprite>> sequences;
    EffectSprite fallback;
    std::optional<EffectMaterialAssets> material;
};

// The engine is frame based (rates are per 1/60 s frame), so this steps at a fixed 60 Hz.
class EffectInstance {
public:
    EffectInstance(EffectData effect, std::vector<EffectEmitterAssets> assets, const Mat4& root, uint32_t seed);

    void Restart();
    void Update(float seconds);
    bool Finished() const;
    // lights: where lit emitters add their particles' ViewerParticleLight points.
    void Collect(std::vector<ViewerParticle>& out, std::vector<ViewerParticleLight>* lights = nullptr) const;
    void CollectMaterial(std::vector<ViewerMaterialVertex>& vertices, std::vector<ViewerMaterialBatch>& batches,
                         std::vector<ViewerParticleLight>& lights) const;

    // Most fields are read at spawn, so edits show on new particles. Call
    // RefreshTransforms after a Transform3D edit.
    EffectData& Data() { return effect; }
    void RefreshTransforms();
    // Indexed by emitter; 1 hides it (it keeps simulating).
    void SetHidden(std::vector<uint8_t> hidden) { hiddenEmitters = std::move(hidden); }

private:
    struct Particle {
        float position[3];
        float velocity[3];
        float gravity;
        float gravityVelocity;
        float velocityCoef;
        float age;
        float appear;
        float keep;
        float vanish;
        float hold;  // frames held at the end of keep, < 0 = forever
        float repeatKeep;
        float rotation;
        float rotationAdd;
        float rotationCoef;
        float sizeScalar;
        float sizeX;
        float sizeY;
        float scalarAdd;
        float scalarCoef;
        float sizeAddX;
        float sizeCoefX;
        float sizeAddY;
        float sizeCoefY;
        float color[4];
        float ribbonDirection[3];
        float polygonRotation[3];
        uint32_t sequence;
        uint32_t pattern;
        float patternTime;       // frames since the last pattern change
        float framesPerPattern;  // 0 = never advances
        bool reverse;
        bool flipU;
        bool flipV;
    };

    struct Emitter {
        Mat4 world;
        float delay = 0;
        float interval = 0;
        int32_t loopsLeft = 0;
        std::vector<Particle> particles;
        std::vector<uint8_t> userMaterial;
    };

    void Step();
    void Spawn(std::size_t emitterIndex);
    EffectSprite CurrentSprite(const EfxEmitter& def, std::size_t emitterIndex, const Particle& p) const;
    void CollectRibbon(const EfxEmitter& def, const Particle& p, const EffectSprite& sprite, float lifeAlpha,
                       std::vector<ViewerParticle>& out) const;
    bool StepParticle(const EfxEmitter& def, std::size_t emitterIndex, Particle& p);

    float Rand01();
    float RandSigned();
    float Sample(const EfxRange& r);
    int32_t Sample(const EfxRangeI& r);
    uint32_t PickIndex(const EfxRangeI& r);

    EffectData effect;
    std::vector<EffectEmitterAssets> assets;
    Mat4 root;
    std::mt19937 rng;
    std::vector<Emitter> emitters;
    std::vector<uint8_t> hiddenEmitters;
    float pendingFrames = 0;
};

#endif
