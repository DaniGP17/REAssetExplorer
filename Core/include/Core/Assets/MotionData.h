#ifndef REASSETEXPLORER_MOTIONDATA_H
#define REASSETEXPLORER_MOTIONDATA_H
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MotVec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct MotQuat {
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 1;
};

// Bind pose, local space.
struct MotBone {
    std::string name;
    int32_t parent = -1;  // index in the same list
    MotVec3 translation;
    MotQuat rotation;
    int32_t index = 0;
    uint32_t hash = 0;
};

// flags: value type (bits 0-11), key compression (12-19), frame index width (20+).
struct MotTrack {
    uint32_t flags = 0;
    std::vector<uint32_t> frames;  // empty when the track has no index table
    std::vector<MotVec3> vectors;
    std::vector<MotQuat> rotations;

    uint32_t Compression() const { return flags & 0xFF000; }
    std::size_t KeyCount() const { return vectors.size() + rotations.size(); }
};

struct MotBoneClip {
    uint16_t boneIndex = 0;
    uint32_t boneHash = 0;
    std::string boneName;  // from the bone list by hash, empty if absent
    std::optional<MotTrack> translation;
    std::optional<MotTrack> rotation;
    std::optional<MotTrack> scale;
};

struct MotData {
    uint32_t version = 0;
    std::string name;
    std::string jointMapPath;
    float frameCount = 0;
    float blending = 0;
    float startFrame = 0;
    float endFrame = 0;
    uint16_t frameRate = 0;
    std::vector<MotBoneClip> boneClips;
    // Motlists store one bone list; mots without their own share the first one's.
    std::shared_ptr<const std::vector<MotBone>> bones;
};

struct MotlistEntry {
    uint16_t motionId = 0;
    int32_t motion = -1;  // index into MotlistData::motions, -1 = empty or unsupported slot
    std::string other;    // when motion is -1: "empty", "motion tree", "external <path>"...
};

struct MotlistData {
    uint32_t version = 0;
    std::string name;
    std::string baseMotListPath;
    std::vector<MotData> motions;
    std::vector<MotlistEntry> entries;
    std::vector<std::string> warnings;
};

#endif
