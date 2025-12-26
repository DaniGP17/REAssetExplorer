#include "Explorer/SkeletalAnimator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "Core/Hashing/MurmurHash3.h"

namespace {

Mat4 ToMat4(const std::array<float, 16>& m) {
    Mat4 out;
    std::memcpy(out.m, m.data(), sizeof(out.m));
    return out;
}

// Row-vector rotation matrix (ComposeTRS layout) back to a quaternion.
MotQuat QuatFromRows(const float r[3][3]) {
    float trace = r[0][0] + r[1][1] + r[2][2];
    MotQuat q;
    if (trace > 0) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r[1][2] - r[2][1]) / s;
        q.y = (r[2][0] - r[0][2]) / s;
        q.z = (r[0][1] - r[1][0]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q.w = (r[1][2] - r[2][1]) / s;
        q.x = 0.25f * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[2][0] + r[0][2]) / s;
    } else if (r[1][1] > r[2][2]) {
        float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q.w = (r[2][0] - r[0][2]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q.w = (r[0][1] - r[1][0]) / s;
        q.x = (r[2][0] + r[0][2]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

float KeyFrame(const MotTrack& track, std::size_t key) {
    return track.frames.empty() ? static_cast<float>(key) : static_cast<float>(track.frames[key]);
}

void FindKeys(const MotTrack& track, float frame, std::size_t& lo, std::size_t& hi, float& t) {
    std::size_t count = track.KeyCount();
    std::size_t upper;
    if (track.frames.empty()) {
        upper = frame < 0 ? 0 : std::min(count, static_cast<std::size_t>(frame) + 1);
    } else {
        upper = static_cast<std::size_t>(std::upper_bound(track.frames.begin(), track.frames.end(), frame,
            [](float f, uint32_t key) { return f < static_cast<float>(key); }) - track.frames.begin());
    }
    if (upper == 0) { lo = hi = 0; t = 0; return; }
    if (upper >= count) { lo = hi = count - 1; t = 0; return; }
    lo = upper - 1;
    hi = upper;
    float f0 = KeyFrame(track, lo);
    float f1 = KeyFrame(track, hi);
    t = f1 > f0 ? (frame - f0) / (f1 - f0) : 0.0f;
}

MotVec3 SampleVector(const MotTrack& track, float frame) {
    std::size_t lo, hi;
    float t;
    FindKeys(track, frame, lo, hi, t);
    const MotVec3& a = track.vectors[lo];
    const MotVec3& b = track.vectors[hi];
    return MotVec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

MotQuat SampleRotation(const MotTrack& track, float frame) {
    std::size_t lo, hi;
    float t;
    FindKeys(track, frame, lo, hi, t);
    MotQuat a = track.rotations[lo];
    MotQuat b = track.rotations[hi];
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0) {
        b = MotQuat{ -b.x, -b.y, -b.z, -b.w };
        dot = -dot;
    }
    float wa = 1.0f - t;
    float wb = t;
    if (dot < 0.9995f) {
        float theta = std::acos(dot);
        float s = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / s;
        wb = std::sin(t * theta) / s;
    }
    MotQuat q{ a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-6f) q = MotQuat{ q.x / len, q.y / len, q.z / len, q.w / len };
    return q;
}

}

std::shared_ptr<const SkeletonData> SkeletonData::FromMesh(const MeshData& mesh) {
    auto skeleton = std::make_shared<SkeletonData>();
    for (const MeshJoint& joint : mesh.joints) {
        skeleton->names.push_back(joint.name);
        skeleton->hashes.push_back(Murmur3::MakeHash(joint.name));
        bool root = joint.parentIndex >= mesh.joints.size();
        skeleton->parents.push_back(root ? -1 : joint.parentIndex);
        skeleton->bindLocal.push_back(ToMat4(joint.localMatrix));
        skeleton->inverseBind.push_back(ToMat4(joint.inverseBindMatrix));
    }
    skeleton->remap = mesh.jointRemap;
    return skeleton;
}

std::vector<Mat4> BindPoseWorld(const SkeletonData& skeleton, const Mat4& world) {
    std::size_t count = skeleton.parents.size();
    std::vector<Mat4> result(count);
    // 0 = pending, 1 = in progress (a cycle makes the joint a root), 2 = done.
    std::vector<uint8_t> state(count, 0);
    std::function<void(std::size_t)> solve = [&](std::size_t j) {
        if (state[j] != 0) return;
        state[j] = 1;
        int32_t parent = skeleton.parents[j];
        bool hasParent = parent >= 0 && static_cast<std::size_t>(parent) < count;
        if (hasParent) solve(static_cast<std::size_t>(parent));
        bool usable = hasParent && state[static_cast<std::size_t>(parent)] == 2;
        result[j] = Mul(skeleton.bindLocal[j], usable ? result[static_cast<std::size_t>(parent)] : world);
        state[j] = 2;
    };
    for (std::size_t j = 0; j < count; j++) solve(j);
    return result;
}

SkeletalAnimator::SkeletalAnimator(std::shared_ptr<const SkeletonData> skeletonData)
    : skeleton(std::move(skeletonData)) {
    std::size_t count = skeleton->names.size();
    std::vector<uint8_t> visited(count, 0);
    std::function<void(std::size_t)> visit = [&](std::size_t j) {
        if (visited[j]) return;
        visited[j] = 1;
        if (skeleton->parents[j] >= 0) visit(static_cast<std::size_t>(skeleton->parents[j]));
        order.push_back(static_cast<int32_t>(j));
    };
    for (std::size_t j = 0; j < count; j++) visit(j);

    for (const Mat4& local : skeleton->bindLocal) {
        MotVec3 scale;
        float rows[3][3];
        float* lengths[3] = { &scale.x, &scale.y, &scale.z };
        for (int r = 0; r < 3; r++) {
            float len = std::sqrt(local.m[r * 4] * local.m[r * 4] + local.m[r * 4 + 1] * local.m[r * 4 + 1] +
                                  local.m[r * 4 + 2] * local.m[r * 4 + 2]);
            *lengths[r] = len;
            for (int c = 0; c < 3; c++) rows[r][c] = len > 1e-6f ? local.m[r * 4 + c] / len : 0.0f;
        }
        bindScale.push_back(scale);
        bindRotation.push_back(QuatFromRows(rows));
        bindTranslation.push_back(MotVec3{ local.m[12], local.m[13], local.m[14] });
    }
    channels.assign(count, Channel{});
}

std::size_t SkeletalAnimator::SetMotion(const MotData* motion) {
    channels.assign(skeleton->names.size(), Channel{});
    if (motion == nullptr) return 0;

    std::unordered_map<uint32_t, std::size_t> jointByHash;
    for (std::size_t j = 0; j < skeleton->hashes.size(); j++) jointByHash.emplace(skeleton->hashes[j], j);

    std::size_t driven = 0;
    for (const MotBoneClip& clip : motion->boneClips) {
        auto it = jointByHash.find(clip.boneHash);
        if (it == jointByHash.end()) continue;
        Channel& channel = channels[it->second];
        if (clip.translation && clip.translation->KeyCount() > 0) channel.translation = &*clip.translation;
        if (clip.rotation && clip.rotation->KeyCount() > 0) channel.rotation = &*clip.rotation;
        if (clip.scale && clip.scale->KeyCount() > 0) channel.scale = &*clip.scale;
        driven++;
    }
    return driven;
}

void SkeletalAnimator::Evaluate(float frame, const Mat4& world, float* out, std::vector<Mat4>* jointWorld) const {
    std::vector<Mat4> pose(skeleton->names.size());
    for (int32_t j : order) {
        const Channel& channel = channels[j];
        Mat4 local;
        if (channel.translation == nullptr && channel.rotation == nullptr && channel.scale == nullptr) {
            local = skeleton->bindLocal[j];
        } else {
            MotVec3 t = channel.translation ? SampleVector(*channel.translation, frame) : bindTranslation[j];
            MotQuat r = channel.rotation ? SampleRotation(*channel.rotation, frame) : bindRotation[j];
            MotVec3 s = channel.scale ? SampleVector(*channel.scale, frame) : bindScale[j];
            float tv[3] = { t.x, t.y, t.z };
            float rv[4] = { r.x, r.y, r.z, r.w };
            float sv[3] = { s.x, s.y, s.z };
            local = ComposeTRS(tv, rv, sv);
        }
        int32_t parent = skeleton->parents[j];
        pose[j] = parent >= 0 ? Mul(local, pose[parent]) : local;
    }
    if (jointWorld != nullptr) {
        jointWorld->resize(pose.size());
        for (std::size_t j = 0; j < pose.size(); j++) (*jointWorld)[j] = Mul(pose[j], world);
    }
    for (std::size_t slot = 0; slot < skeleton->remap.size(); slot++) {
        uint16_t joint = skeleton->remap[slot];
        Mat4 skin = joint < pose.size() ? Mul(Mul(skeleton->inverseBind[joint], pose[joint]), world) : world;
        ToFloat3x4(skin, out + slot * 12);
    }
}

std::vector<int32_t> MatchJoints(const SkeletonData& skeleton, const SkeletonData& other) {
    std::unordered_map<uint32_t, int32_t> byHash;
    for (std::size_t j = 0; j < other.hashes.size(); j++) byHash.emplace(other.hashes[j], static_cast<int32_t>(j));
    std::vector<int32_t> match(skeleton.hashes.size(), -1);
    for (std::size_t j = 0; j < skeleton.hashes.size(); j++) {
        if (auto it = byHash.find(skeleton.hashes[j]); it != byHash.end()) match[j] = it->second;
    }
    return match;
}

void EvaluateFollower(const SkeletonData& skeleton, const Mat4& world, const std::vector<int32_t>& leaderJoint,
                      const std::vector<Mat4>& leaderWorld, float* out, std::vector<Mat4>& jointWorld) {
    std::size_t count = skeleton.parents.size();
    jointWorld.assign(count, world);
    std::vector<uint8_t> solved(count, 0);
    std::function<void(std::size_t)> solve = [&](std::size_t j) {
        if (solved[j]) return;
        solved[j] = 1;  // set first: a parent cycle ends at the instance world
        int32_t leader = j < leaderJoint.size() ? leaderJoint[j] : -1;
        if (leader >= 0 && static_cast<std::size_t>(leader) < leaderWorld.size()) {
            jointWorld[j] = leaderWorld[static_cast<std::size_t>(leader)];
            return;
        }
        int32_t parent = skeleton.parents[j];
        if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
            solve(static_cast<std::size_t>(parent));
            jointWorld[j] = Mul(skeleton.bindLocal[j], jointWorld[static_cast<std::size_t>(parent)]);
        } else {
            jointWorld[j] = Mul(skeleton.bindLocal[j], world);
        }
    };
    for (std::size_t j = 0; j < count; j++) solve(j);
    for (std::size_t slot = 0; slot < skeleton.remap.size(); slot++) {
        uint16_t joint = skeleton.remap[slot];
        Mat4 skin = joint < count ? Mul(skeleton.inverseBind[joint], jointWorld[joint]) : world;
        ToFloat3x4(skin, out + slot * 12);
    }
}
