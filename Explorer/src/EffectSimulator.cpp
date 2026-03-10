#include "Explorer/EffectSimulator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float TWO_PI = 6.2831853f;

Mat4 EulerXYZ(const float r[3]) {
    float cx = std::cos(r[0]), sx = std::sin(r[0]);
    float cy = std::cos(r[1]), sy = std::sin(r[1]);
    float cz = std::cos(r[2]), sz = std::sin(r[2]);
    Mat4 rx{{ 1, 0, 0, 0,  0, cx, sx, 0,  0, -sx, cx, 0,  0, 0, 0, 1 }};
    Mat4 ry{{ cy, 0, -sy, 0,  0, 1, 0, 0,  sy, 0, cy, 0,  0, 0, 0, 1 }};
    Mat4 rz{{ cz, sz, 0, 0,  -sz, cz, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 }};
    return Mul(Mul(rx, ry), rz);
}

}

Mat4 EmitterLocalTransform(const EfxTransform3D& t) {
    Mat4 scale{{ t.scale[0], 0, 0, 0,  0, t.scale[1], 0, 0,  0, 0, t.scale[2], 0,  0, 0, 0, 1 }};
    Mat4 m = Mul(scale, EulerXYZ(t.rotation));
    m.m[12] = t.position[0];
    m.m[13] = t.position[1];
    m.m[14] = t.position[2];
    return m;
}

namespace {

void TransformDir(const Mat4& m, const float v[3], float out[3]) {
    for (int c = 0; c < 3; c++) {
        out[c] = v[0] * m.m[c] + v[1] * m.m[4 + c] + v[2] * m.m[8 + c];
    }
}

bool Normalize(float v[3]) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f) return false;
    for (int i = 0; i < 3; i++) v[i] /= len;
    return true;
}

ParticleBlend ToParticleBlend(EfxBlend blend) {
    if (blend == EfxBlend::Physical) return ParticleBlend::Physical;
    if (blend == EfxBlend::AddContrast) return ParticleBlend::Additive;
    return ParticleBlend::AlphaBlend;
}

uint32_t ParticleFlags(ParticleBlend blend, bool physicalAlpha, const EffectSprite& sprite) {
    return static_cast<uint32_t>(blend) | (physicalAlpha ? PARTICLE_PHYSICAL_ALPHA : 0) | (sprite.alphaGamma ? PARTICLE_ALPHA_GAMMA : 0);
}

float SoftDistance(const EfxEmitter& def) {
    return def.shaderSettings ? def.shaderSettings->softParticleDistance : 0.0f;
}

float DetonemapRate(const EfxEmitter& def) {
    return def.shaderSettings && def.shaderSettings->detonemap ? def.shaderSettings->detonemapRate : 0.0f;
}

// The engine skips the per-frame decay when a coefficient is 0 or 1.
float Decay(float value, float coef) {
    return coef == 0.0f || coef == 1.0f ? value : value * coef;
}

}

bool IsDrawableEmitter(const EfxEmitter& e) {
    return e.spawn.has_value() && (e.billboard.has_value() || e.ribbonLength.has_value() || IsMaterialEmitter(e)) && !e.distortion;
}

bool IsMaterialEmitter(const EfxEmitter& e) {
    return e.spawn.has_value() && e.material.has_value() && (e.ribbonLength.has_value() || e.polygon.has_value());
}

bool IsStaticMeshEmitter(const EfxEmitter& e) {
    if (!e.mesh || !e.spawn || e.velocity || e.mesh->meshPath.empty() || e.mesh->mdfPath.empty()) return false;
    bool holdsForever = e.life && e.life->Type() != EfxLifeType::Normal && e.life->keepHoldFrame.min == 0;
    bool respawnsForever = e.spawn->loopNum.min == 0 && e.spawn->loopNum.max == 0;
    return holdsForever || respawnsForever;
}

EffectInstance::EffectInstance(EffectData effect, std::vector<EffectEmitterAssets> assets, const Mat4& root,
                               uint32_t seed)
    : effect(std::move(effect)), assets(std::move(assets)), root(root), rng(seed) {
    emitters.resize(this->effect.emitters.size());
    Restart();
}

float EffectInstance::Rand01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

float EffectInstance::RandSigned() {
    return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
}

float EffectInstance::Sample(const EfxRange& r) {
    return r.base + RandSigned() * r.spread;
}

int32_t EffectInstance::Sample(const EfxRangeI& r) {
    if (r.max <= r.min) return r.min;
    return r.min + static_cast<int32_t>(rng() % static_cast<uint32_t>(r.max - r.min + 1));
}

// UVSequence picks sequence/pattern numbers as s + rand % max(1, r - s).
uint32_t EffectInstance::PickIndex(const EfxRangeI& r) {
    int32_t span = std::max(1, r.max - r.min);
    return static_cast<uint32_t>(std::max(0, r.min + static_cast<int32_t>(rng() % static_cast<uint32_t>(span))));
}

void EffectInstance::Restart() {
    pendingFrames = 0;
    for (std::size_t i = 0; i < emitters.size(); i++) {
        const EfxEmitter& def = effect.emitters[i];
        Emitter& e = emitters[i];
        e.particles.clear();
        e.world = def.transform ? Mul(EmitterLocalTransform(*def.transform), root) : root;
        e.interval = 0;
        e.delay = 0;
        e.loopsLeft = 0;
        e.userMaterial.clear();
        if (!IsDrawableEmitter(def)) continue;
        if (i < assets.size() && assets[i].material) {
            const EffectMaterialAssets& material = *assets[i].material;
            e.userMaterial = material.userMaterial;
            for (const EffectMaterialAssets::RandomFloat& r : material.randomFloats) {
                float value = r.base + r.low + (r.high - r.low) * Rand01();
                if (r.offset + 4 <= e.userMaterial.size()) std::memcpy(e.userMaterial.data() + r.offset, &value, 4);
            }
        }
        const EfxSpawn& spawn = *def.spawn;
        e.delay = static_cast<float>(Sample(spawn.emitterDelayFrame));
        if (spawn.loopNum.min == 0 && spawn.loopNum.max == 0) {
            e.loopsLeft = -1;
        } else {
            e.loopsLeft = spawn.loopNum.min + static_cast<int32_t>(rng() % static_cast<uint32_t>(spawn.loopNum.max + 1));
        }
    }
}

void EffectInstance::RefreshTransforms() {
    for (std::size_t i = 0; i < emitters.size(); i++) {
        const EfxEmitter& def = effect.emitters[i];
        emitters[i].world = def.transform ? Mul(EmitterLocalTransform(*def.transform), root) : root;
    }
}

void EffectInstance::Update(float seconds) {
    pendingFrames += seconds * 60.0f;
    int steps = 0;
    while (pendingFrames >= 1.0f && steps < 8) {
        Step();
        pendingFrames -= 1.0f;
        steps++;
    }
    if (pendingFrames > 1.0f) pendingFrames = 0;
}

bool EffectInstance::Finished() const {
    for (std::size_t i = 0; i < emitters.size(); i++) {
        if (!IsDrawableEmitter(effect.emitters[i])) continue;
        if (emitters[i].loopsLeft != 0 || !emitters[i].particles.empty()) return false;
    }
    return true;
}

void EffectInstance::Step() {
    for (std::size_t i = 0; i < emitters.size(); i++) {
        const EfxEmitter& def = effect.emitters[i];
        if (!IsDrawableEmitter(def)) continue;
        Emitter& e = emitters[i];

        std::erase_if(e.particles, [&](Particle& p) { return !StepParticle(def, i, p); });

        if (e.delay >= 1.0f) {
            e.delay -= 1.0f;
            continue;
        }
        if (e.loopsLeft == 0) continue;
        // Matches the engine's (interval + 1) / 60 s spawn period.
        if (e.interval > 1.0f) {
            e.interval -= 1.0f;
            continue;
        }
        Spawn(i);
        e.interval = static_cast<float>(Sample(def.spawn->intervalFrame) + 1);
        if (e.loopsLeft > 0) e.loopsLeft--;
    }
}

void EffectInstance::Spawn(std::size_t emitterIndex) {
    const EfxEmitter& def = effect.emitters[emitterIndex];
    Emitter& e = emitters[emitterIndex];
    const EfxSpawn& spawn = *def.spawn;
    uint32_t capacity = spawn.maxParticles == 0 ? 1024 : spawn.maxParticles;

    int32_t count = Sample(spawn.spawnNum);
    for (int32_t n = 0; n < count; n++) {
        if (e.particles.size() >= capacity) {
            if (!spawn.ringBuffer) break;
            e.particles.erase(e.particles.begin());
        }

        Particle p{};
        float local[3]{};
        if (def.shape) {
            const EfxShape3D& s = *def.shape;
            auto lerp = [](const EfxMinMax& r, float t) { return r.min + (r.max - r.min) * t; };
            switch (s.type) {
                case EfxShapeType::Sphere: {
                    float dir[3];
                    do {
                        for (float& d : dir) d = RandSigned();
                    } while (!Normalize(dir));
                    float radius = lerp(s.range[0], Rand01());
                    for (int c = 0; c < 3; c++) local[c] = dir[c] * radius;
                    break;
                }
                case EfxShapeType::Cylinder: {
                    float angle = s.arcStart + Rand01() * s.arcLength;
                    float t = Rand01();
                    local[0] = std::cos(angle) * lerp(s.range[0], t);
                    local[2] = std::sin(angle) * lerp(s.range[2], t);
                    local[1] = lerp(s.range[1], Rand01());
                    break;
                }
                default: {
                    for (int c = 0; c < 3; c++) local[c] = RandSigned() * s.range[c].max;
                    // Hollow box: push one random axis out to the [min, max] shell.
                    int axis = static_cast<int>(rng() % 3);
                    if (s.range[axis].min > 0) {
                        float sign = local[axis] < 0 ? -1.0f : 1.0f;
                        local[axis] = sign * lerp(s.range[axis], Rand01());
                    }
                    break;
                }
            }
            Mat4 rotation = EulerXYZ(s.localRotation);
            float rotated[3];
            TransformDir(rotation, local, rotated);
            for (int c = 0; c < 3; c++) local[c] = rotated[c];
        }
        TransformPoint(e.world, local, p.position);

        p.velocityCoef = 1.0f;
        if (def.velocity) {
            const EfxVelocity3D& v = *def.velocity;
            float dir[3] = { Sample(v.direction[0]), Sample(v.direction[1]), Sample(v.direction[2]) };
            if (v.type == EfxVelocityType::Radial || v.type == EfxVelocityType::Normal) {
                float radial[3] = { local[0], local[1], local[2] };
                if (Normalize(radial)) std::copy(radial, radial + 3, dir);
            } else if (v.type == EfxVelocityType::Spread) {
                float spread = Sample(v.spread);
                for (float& d : dir) d += RandSigned() * spread;
            }
            Normalize(dir);
            float speed = Sample(v.speed) / 60.0f;
            float localVelocity[3] = { dir[0] * speed, dir[1] * speed, dir[2] * speed };
            TransformDir(e.world, localVelocity, p.velocity);
            p.gravity = Sample(v.gravityRate) * (1.0f / 3600.0f) * -9.80665f;
            p.velocityCoef = Sample(v.speedCoef);
        }

        if (def.life) {
            const EfxLife& life = *def.life;
            p.appear = static_cast<float>(std::max(0, Sample(life.appearFrame)));
            p.keep = static_cast<float>(std::max(0, Sample(life.keepFrame)));
            p.vanish = static_cast<float>(std::max(0, Sample(life.vanishFrame)));
            if (life.Type() != EfxLifeType::Normal) {
                p.hold = life.keepHoldFrame.min == 0 ? -1.0f : static_cast<float>(Sample(life.keepHoldFrame));
                if (life.Type() == EfxLifeType::KeepHoldRepeat) p.repeatKeep = p.keep;
            }
        } else {
            p.keep = 60;
        }
        if (p.appear + p.keep + p.vanish < 1.0f) p.keep = 1;

        if (def.polygon) {
            const EfxPolygon& pg = *def.polygon;
            float t = pg.ColorRangeEnabled() ? Rand01() : 0.0f;
            for (int c = 0; c < 4; c++) {
                p.color[c] = (pg.color[c] + (pg.colorRange[c] - pg.color[c]) * t) / 255.0f;
            }
            p.sizeScalar = Sample(pg.sizeScalar);
            p.sizeX = Sample(pg.width);
            p.sizeY = Sample(pg.height);
            for (int c = 0; c < 3; c++) p.polygonRotation[c] = Sample(pg.rotation[c]);
        } else if (def.ribbonLength) {
            const EfxRibbonLength& rl = *def.ribbonLength;
            float t = rl.ColorRangeEnabled() ? Rand01() : 0.0f;
            for (int c = 0; c < 4; c++) {
                p.color[c] = (rl.color[c] + (rl.colorRange[c] - rl.color[c]) * t) / 255.0f;
            }
            p.sizeScalar = Sample(rl.sizeScalar);
            p.sizeX = Sample(rl.width);
            p.sizeY = Sample(rl.length);
            float dir[3] = { Sample(rl.direction[0]), Sample(rl.direction[1]), Sample(rl.direction[2]) };
            TransformDir(e.world, dir, p.ribbonDirection);
            if (!Normalize(p.ribbonDirection)) p.ribbonDirection[1] = 1.0f;
        } else {
            const EfxBillboard3D& bb = *def.billboard;
            float t = bb.ColorRangeEnabled() ? Rand01() : 0.0f;
            for (int c = 0; c < 4; c++) {
                p.color[c] = (bb.color[c] + (bb.colorRange[c] - bb.color[c]) * t) / 255.0f;
            }
            p.sizeScalar = Sample(bb.sizeScalar);
            p.sizeX = Sample(bb.sizeX);
            p.sizeY = Sample(bb.sizeY);
            p.rotation = Sample(bb.rotation);
        }

        if (def.scaleAnim) {
            const EfxScaleAnim& s = *def.scaleAnim;
            p.scalarAdd = Sample(s.scalarAdd);
            p.scalarCoef = Sample(s.scalarCoef);
            p.sizeAddX = Sample(s.addX);
            p.sizeCoefX = Sample(s.coefX);
            p.sizeAddY = Sample(s.addY);
            p.sizeCoefY = Sample(s.coefY);
        }
        if (def.rotateAnim) {
            const EfxRotateAnim& r = *def.rotateAnim;
            p.rotationAdd = Sample(r.add[0]);
            p.rotationCoef = Sample(r.coef[0]);
            if (r.ReverseRandom() && (rng() & 1)) p.rotationAdd = -p.rotationAdd;
        }

        if (def.uvSequence) {
            const EfxUVSequence& u = *def.uvSequence;
            p.sequence = PickIndex(u.sequenceNo);
            p.pattern = PickIndex(u.patternNo);
            float speed = u.playSpeed.min + (u.playSpeed.max - u.playSpeed.min) * Rand01();
            p.framesPerPattern = speed > 0 ? 1.0f / speed : 0.0f;
            auto pick = [this](uint32_t mode) { return mode == 1 || (mode == 2 && (rng() & 1)); };
            p.flipU = pick(u.HFlip());
            p.flipV = pick(u.VFlip());
            p.reverse = pick(u.PlayOrder());
        }

        e.particles.push_back(p);
    }
}

bool EffectInstance::StepParticle(const EfxEmitter& def, std::size_t emitterIndex, Particle& p) {
    p.age += 1.0f;
    // Life::updateInstanceParticle: holding pins the particle to the end of its keep span.
    if (p.age >= p.appear + p.keep && (p.hold != 0 || p.repeatKeep > 0)) {
        p.age = p.appear + p.keep;
        if (p.hold > 0) {
            p.hold = std::max(0.0f, p.hold - 1.0f);
        } else if (p.hold == 0) {
            p.repeatKeep -= 1.0f;
        }
    }
    if (p.age >= p.appear + p.keep + p.vanish) return false;

    p.gravityVelocity += p.gravity;
    p.position[0] += p.velocity[0];
    p.position[1] += p.velocity[1] + p.gravityVelocity;
    p.position[2] += p.velocity[2];
    for (float& v : p.velocity) v *= p.velocityCoef;

    if (def.scaleAnim) {
        p.sizeScalar = std::max(0.0f, p.sizeScalar + p.scalarAdd);
        p.sizeX += p.sizeAddX;
        p.sizeY += p.sizeAddY;
        p.scalarAdd = Decay(p.scalarAdd, p.scalarCoef);
        p.sizeAddX = Decay(p.sizeAddX, p.sizeCoefX);
        p.sizeAddY = Decay(p.sizeAddY, p.sizeCoefY);
    }
    if (def.rotateAnim) {
        p.rotation = std::fmod(p.rotation + p.rotationAdd, TWO_PI);
        p.rotationAdd = Decay(p.rotationAdd, p.rotationCoef);
    }

    if (def.uvSequence && def.uvSequence->PlayType() != EfxUvPlayType::Pause && p.framesPerPattern > 0) {
        const std::vector<std::vector<EffectSprite>>& sequences = assets[emitterIndex].sequences;
        uint32_t count = p.sequence < sequences.size() ? static_cast<uint32_t>(sequences[p.sequence].size()) : 0;
        p.patternTime += 1.0f;
        if (count > 0 && p.patternTime >= p.framesPerPattern) {
            // The engine keeps only the fractional pattern count as the remainder.
            float advanced = p.patternTime / p.framesPerPattern;
            auto steps = static_cast<uint32_t>(advanced);
            p.patternTime = advanced - static_cast<float>(steps);
            bool loop = def.uvSequence->PlayType() == EfxUvPlayType::Loop;
            if (!p.reverse) {
                p.pattern += steps;
                if (p.pattern >= count) p.pattern = loop ? 0 : count - 1;
            } else if (p.pattern >= steps) {
                p.pattern -= steps;
            } else {
                p.pattern = loop ? count - steps % count : 0;
            }
            p.pattern = std::min(p.pattern, count - 1);
        }
    }
    return true;
}

void EffectInstance::Collect(std::vector<ViewerParticle>& out, std::vector<ViewerParticleLight>* lights) const {
    for (std::size_t i = 0; i < emitters.size(); i++) {
        const EfxEmitter& def = effect.emitters[i];
        if (!IsDrawableEmitter(def) || IsMaterialEmitter(def) || (i < hiddenEmitters.size() && hiddenEmitters[i])) continue;
        const EffectEmitterAssets& emitterAssets = assets[i];
        const bool lit = lights && def.shaderSettings && def.shaderSettings->lightingType != 0;

        for (const Particle& p : emitters[i].particles) {
            float lifeAlpha = 1.0f;
            if (p.age < p.appear) {
                lifeAlpha = p.age / p.appear;
            } else if (p.age >= p.appear + p.keep && p.vanish > 0) {
                lifeAlpha = 1.0f - (p.age - p.appear - p.keep) / p.vanish;
            }

            EffectSprite sprite = emitterAssets.fallback;
            if (def.uvSequence && p.sequence < emitterAssets.sequences.size() &&
                !emitterAssets.sequences[p.sequence].empty()) {
                const std::vector<EffectSprite>& patterns = emitterAssets.sequences[p.sequence];
                sprite = patterns[std::min<std::size_t>(p.pattern, patterns.size() - 1)];
            }

            std::size_t first = out.size();
            uint32_t lightIndex = UINT32_MAX;
            if (lit) {
                ViewerParticleLight light{};
                std::copy(p.position, p.position + 3, light.position);
                light.segment = 1 | def.shaderSettings->PrimitiveFlags() << 16;
                light.lightShadowRatio = def.shaderSettings->lightShadowRatio;
                light.backfaceLightRatio = def.shaderSettings->backfaceLightRatio;
                light.directionalLightShadowRatio = 1.0f;
                lightIndex = static_cast<uint32_t>(lights->size());
                lights->push_back(light);
            }
            if (def.ribbonLength) {
                CollectRibbon(def, p, sprite, lifeAlpha, out);
                for (std::size_t k = first; k < out.size(); k++) out[k].lightIndex = lightIndex;
                continue;
            }
            const EfxBillboard3D& bb = *def.billboard;
            ViewerParticle v{};
            std::copy(p.position, p.position + 3, v.position);
            v.rotation = p.rotation;
            v.size[0] = p.sizeX * p.sizeScalar;
            v.size[1] = p.sizeY * p.sizeScalar;
            v.textureIndex = sprite.textureIndex;
            v.flags = ParticleFlags(ToParticleBlend(bb.Blend()), bb.PhysicalAlphaBlend(), sprite);
            v.color[0] = p.color[0];
            v.color[1] = p.color[1];
            v.color[2] = p.color[2];
            v.color[3] = p.color[3] * std::clamp(lifeAlpha, 0.0f, 1.0f);
            v.uvRect[0] = p.flipU ? sprite.uvRect[2] : sprite.uvRect[0];
            v.uvRect[2] = p.flipU ? sprite.uvRect[0] : sprite.uvRect[2];
            v.uvRect[1] = p.flipV ? sprite.uvRect[3] : sprite.uvRect[1];
            v.uvRect[3] = p.flipV ? sprite.uvRect[1] : sprite.uvRect[3];
            v.alphaRate = bb.alphaRate;
            v.emissiveRate = bb.emissiveRate;
            v.softDistance = SoftDistance(def);
            v.evPow2 = sprite.evPow2;
            v.detonemapRate = DetonemapRate(def);
            v.lightIndex = lightIndex;
            out.push_back(v);
        }
    }
}

// RibbonLength::updateControlPoint: the strip runs 1 - basingPoint[1] of its length behind the particle and the rest ahead,
// v = texRepeatNum at the base and 0 at the tip; place colors and scales are indexed from the tip.
void EffectInstance::CollectRibbon(const EfxEmitter& def, const Particle& p, const EffectSprite& sprite, float lifeAlpha,
                                   std::vector<ViewerParticle>& out) const {
    const EfxRibbonLength& rl = *def.ribbonLength;
    uint32_t last = std::clamp<uint32_t>(rl.shapeDivision, 2, 256) - 1;
    float length = p.sizeY * p.sizeScalar;
    float width = p.sizeX * p.sizeScalar;
    float base[3];
    for (int c = 0; c < 3; c++) base[c] = p.position[c] - (1.0f - rl.basingPoint[1]) * length * p.ribbonDirection[c];

    auto place = [last](uint32_t fromTip, float head, float place1, float place2, float ratio1, float ratio2) {
        auto index1 = static_cast<uint32_t>(static_cast<float>(last) * ratio1);
        auto index2 = std::max(index1, static_cast<uint32_t>(static_cast<float>(last) * ratio2));
        if (fromTip >= index2) return place2;
        if (fromTip >= index1) return place1 + (place2 - place1) * (fromTip - index1) / static_cast<float>(index2 - index1);
        return head + (place1 - head) * fromTip / static_cast<float>(index1);
    };
    auto scale = [&](uint32_t point) {
        if (rl.SizePlaceType() == 0) return 1.0f;
        const EfxPlace<float>& s = rl.scalePlace;
        return place(last - point, s.head, s.place1, s.place2, s.place1Ratio, s.place2Ratio);
    };
    auto tint = [&](uint32_t point, int channel) {
        if (rl.ColorPlaceType() == 0) return 1.0f;
        const EfxPlace<uint32_t>& c = rl.colorPlace;
        auto byte = [channel](uint32_t rgba) { return static_cast<float>((rgba >> (channel * 8)) & 0xFF) / 255.0f; };
        return place(last - point, byte(c.head), byte(c.place1), byte(c.place2), c.place1Ratio, c.place2Ratio);
    };

    float top = p.flipV ? sprite.uvRect[3] : sprite.uvRect[1];
    float bottom = p.flipV ? sprite.uvRect[1] : sprite.uvRect[3];
    float repeat = rl.texRepeatNum > 0 ? rl.texRepeatNum : 1.0f;
    for (uint32_t k = 0; k < last; k++) {
        float t0 = static_cast<float>(k) / static_cast<float>(last);
        float t1 = static_cast<float>(k + 1) / static_cast<float>(last);
        ViewerParticle v{};
        for (int c = 0; c < 3; c++) {
            v.position[c] = base[c] + length * t0 * p.ribbonDirection[c];
            v.axis[c] = length * (t1 - t0) * p.ribbonDirection[c];
        }
        v.size[0] = width * 0.5f * (scale(k) + scale(k + 1));
        v.textureIndex = sprite.textureIndex;
        v.flags = ParticleFlags(ToParticleBlend(rl.Blend()), rl.PhysicalAlphaBlend(), sprite) | PARTICLE_AXIS_ALIGNED;
        for (int c = 0; c < 4; c++) v.color[c] = p.color[c] * 0.5f * (tint(k, c) + tint(k + 1, c));
        v.color[3] *= std::clamp(lifeAlpha, 0.0f, 1.0f);
        v.uvRect[0] = p.flipU ? sprite.uvRect[2] : sprite.uvRect[0];
        v.uvRect[2] = p.flipU ? sprite.uvRect[0] : sprite.uvRect[2];
        v.uvRect[1] = top + (bottom - top) * repeat * (1.0f - t0);
        v.uvRect[3] = top + (bottom - top) * repeat * (1.0f - t1);
        v.alphaRate = rl.alphaRate;
        v.emissiveRate = rl.colorRate;
        v.softDistance = SoftDistance(def);
        v.evPow2 = sprite.evPow2;
        v.detonemapRate = DetonemapRate(def);
        out.push_back(v);
    }
}

EffectSprite EffectInstance::CurrentSprite(const EfxEmitter& def, std::size_t emitterIndex, const Particle& p) const {
    const EffectEmitterAssets& emitterAssets = assets[emitterIndex];
    EffectSprite sprite = emitterAssets.fallback;
    if (def.uvSequence && p.sequence < emitterAssets.sequences.size() && !emitterAssets.sequences[p.sequence].empty()) {
        const std::vector<EffectSprite>& patterns = emitterAssets.sequences[p.sequence];
        sprite = patterns[std::min<std::size_t>(p.pattern, patterns.size() - 1)];
    }
    return sprite;
}

namespace {

Mat4 EulerOrdered(const float r[3], uint32_t order) {
    float cx = std::cos(r[0]), sx = std::sin(r[0]);
    float cy = std::cos(r[1]), sy = std::sin(r[1]);
    float cz = std::cos(r[2]), sz = std::sin(r[2]);
    Mat4 rx{{ 1, 0, 0, 0,  0, cx, sx, 0,  0, -sx, cx, 0,  0, 0, 0, 1 }};
    Mat4 ry{{ cy, 0, -sy, 0,  0, 1, 0, 0,  sy, 0, cy, 0,  0, 0, 0, 1 }};
    Mat4 rz{{ cz, sz, 0, 0,  -sz, cz, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 }};
    switch (order) {
        case 1: return Mul(Mul(ry, rz), rx);
        case 2: return Mul(Mul(rz, rx), ry);
        case 3: return Mul(Mul(rx, rz), ry);
        case 4: return Mul(Mul(ry, rx), rz);
        case 5: return Mul(Mul(rz, ry), rx);
        default: return Mul(Mul(rx, ry), rz);
    }
}

// q for the rotation whose rows (row-vector convention) are the normalized rows of m.
void RowsToQuaternion(const Mat4& m, float q[4]) {
    float r[3][3];
    for (int row = 0; row < 3; row++) {
        float len = std::sqrt(m.m[row * 4] * m.m[row * 4] + m.m[row * 4 + 1] * m.m[row * 4 + 1] + m.m[row * 4 + 2] * m.m[row * 4 + 2]);
        if (len < 1e-8f) len = 1;
        // Column-vector matrix element (i, j) = row j, component i.
        for (int c = 0; c < 3; c++) r[c][row] = m.m[row * 4 + c] / len;
    }
    float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        float s = std::sqrt(trace + 1.0f) * 2;
        q[3] = 0.25f * s;
        q[0] = (r[2][1] - r[1][2]) / s;
        q[1] = (r[0][2] - r[2][0]) / s;
        q[2] = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q[3] = (r[2][1] - r[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (r[0][1] + r[1][0]) / s;
        q[2] = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q[3] = (r[0][2] - r[2][0]) / s;
        q[0] = (r[0][1] + r[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (r[1][2] + r[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q[3] = (r[1][0] - r[0][1]) / s;
        q[0] = (r[0][2] + r[2][0]) / s;
        q[1] = (r[1][2] + r[2][1]) / s;
        q[2] = 0.25f * s;
    }
}

float RowLength(const Mat4& m, int row) {
    return std::sqrt(m.m[row * 4] * m.m[row * 4] + m.m[row * 4 + 1] * m.m[row * 4 + 1] + m.m[row * 4 + 2] * m.m[row * 4 + 2]);
}

}

// The vertices PrimitiveContext writes for the Polygon and Ribbon material programs (their VS expands them):
// polygons as a center, a quaternion and signed half extents, ribbons as spine points, the tangent and a
// signed half width. Lit polygons get a grid of lit points; a lit ribbon one camera facing point at its middle.
void EffectInstance::CollectMaterial(std::vector<ViewerMaterialVertex>& vertices, std::vector<ViewerMaterialBatch>& batches,
                                     std::vector<ViewerParticleLight>& lights) const {
    static const float CORNERS[6][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (std::size_t i = 0; i < emitters.size(); i++) {
        const EfxEmitter& def = effect.emitters[i];
        if (!IsMaterialEmitter(def) || !assets[i].material || (i < hiddenEmitters.size() && hiddenEmitters[i])) continue;
        const Emitter& e = emitters[i];
        if (e.particles.empty()) continue;
        ViewerMaterialBatch batch;
        batch.program = assets[i].material->program;
        batch.firstVertex = static_cast<uint32_t>(vertices.size());
        batch.userMaterial = e.userMaterial;
        batch.textures = assets[i].material->textures;
        for (int c = 0; c < 3; c++) batch.emitterPosition[c] = e.world.m[12 + c];
        batch.textures.emplace_back("PrimitiveMaterialTex", CurrentSprite(def, i, e.particles.front()).textureIndex);
        float scale = (RowLength(e.world, 0) + RowLength(e.world, 1) + RowLength(e.world, 2)) / 3.0f;
        const bool lit = assets[i].material->lit && def.shaderSettings;
        batch.primitiveFlags = def.shaderSettings ? def.shaderSettings->PrimitiveFlags() : 0;
        const uint32_t grid = lit && def.polygon ? def.shaderSettings->LightGrid() : 1;
        auto addLight = [&](const float position[3], uint32_t shape) {
            ViewerParticleLight light{};
            std::copy(position, position + 3, light.position);
            light.segment = grid | shape << 8 | def.shaderSettings->PrimitiveFlags() << 16;
            light.lightShadowRatio = def.shaderSettings->lightShadowRatio;
            light.backfaceLightRatio = def.shaderSettings->backfaceLightRatio;
            light.directionalLightShadowRatio = 1.0f;
            lights.push_back(light);
            return &lights.back();
        };

        for (const Particle& p : e.particles) {
            float lifeAlpha = 1.0f;
            if (p.age < p.appear) {
                lifeAlpha = p.age / p.appear;
            } else if (p.age >= p.appear + p.keep && p.vanish > 0) {
                lifeAlpha = 1.0f - (p.age - p.appear - p.keep) / p.vanish;
            }
            EffectSprite sprite = CurrentSprite(def, i, p);
            ViewerMaterialVertex v{};
            if (lit) {
                v.generic2[2] = grid;
                v.generic4[0] = static_cast<uint32_t>(lights.size());
            }
            for (int c = 0; c < 4; c++) {
                float value = p.color[c] * (c == 3 ? std::clamp(lifeAlpha, 0.0f, 1.0f) : 1.0f);
                v.color[c] = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            if (def.polygon) {
                Mat4 rotation = Mul(EulerOrdered(p.polygonRotation, def.polygon->rotationOrder), e.world);
                float q[4];
                RowsToQuaternion(rotation, q);
                float halfX = 0.5f * p.sizeX * p.sizeScalar;
                float halfY = 0.5f * p.sizeY * p.sizeScalar;
                for (uint32_t k = 0; lit && k < grid * grid; k++) {
                    ViewerParticleLight* light = addLight(p.position, 2);
                    light->size[0] = halfX * scale;
                    light->size[1] = halfY * scale;
                    std::copy(q, q + 4, light->rotation);
                    light->attributes = k / grid | (k % grid) << 8;
                }
                for (const auto& c : CORNERS) {
                    std::copy(p.position, p.position + 3, v.position);
                    std::copy(q, q + 4, v.generic5);
                    v.generic0[0] = c[0] * halfX;
                    v.generic0[1] = c[1] * halfY;
                    v.generic6[0] = v.generic6[1] = v.generic6[2] = scale;
                    float t[2] = { c[0] * 0.5f + 0.5f, c[1] * 0.5f + 0.5f };
                    v.uv[0] = sprite.uvRect[0] + (sprite.uvRect[2] - sprite.uvRect[0]) * t[0];
                    v.uv[1] = sprite.uvRect[3] + (sprite.uvRect[1] - sprite.uvRect[3]) * t[1];
                    vertices.push_back(v);
                }
                continue;
            }
            const EfxRibbonLength& rl = *def.ribbonLength;
            uint32_t last = std::clamp<uint32_t>(rl.shapeDivision, 2, 256) - 1;
            float length = p.sizeY * p.sizeScalar;
            float halfWidth = 0.5f * p.sizeX * p.sizeScalar;
            float base[3];
            for (int c = 0; c < 3; c++) base[c] = p.position[c] - (1.0f - rl.basingPoint[1]) * length * p.ribbonDirection[c];
            float repeat = rl.texRepeatNum > 0 ? rl.texRepeatNum : 1.0f;
            if (lit) {
                float middle[3];
                for (int c = 0; c < 3; c++) middle[c] = base[c] + 0.5f * length * p.ribbonDirection[c];
                addLight(middle, 0);
            }
            for (uint32_t k = 0; k < last; k++) {
                float ends[2] = { static_cast<float>(k) / static_cast<float>(last), static_cast<float>(k + 1) / static_cast<float>(last) };
                for (const auto& c : CORNERS) {
                    float t = ends[c[1] > 0 ? 1 : 0];
                    for (int a = 0; a < 3; a++) {
                        v.position[a] = base[a] + length * t * p.ribbonDirection[a];
                        v.generic5[a] = p.ribbonDirection[a];
                    }
                    v.generic0[0] = c[0] * halfWidth;
                    v.generic0[1] = -c[1];
                    float across = c[0] * 0.5f + 0.5f;
                    v.uv[0] = sprite.uvRect[0] + (sprite.uvRect[2] - sprite.uvRect[0]) * across;
                    v.uv[1] = sprite.uvRect[1] + (sprite.uvRect[3] - sprite.uvRect[1]) * repeat * (1.0f - t);
                    vertices.push_back(v);
                }
            }
        }
        batch.vertexCount = static_cast<uint32_t>(vertices.size()) - batch.firstVertex;
        if (batch.vertexCount > 0) batches.push_back(std::move(batch));
    }
}
