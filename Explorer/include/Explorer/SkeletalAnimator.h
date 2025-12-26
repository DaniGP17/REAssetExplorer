#ifndef REASSETEXPLORER_SKELETALANIMATOR_H
#define REASSETEXPLORER_SKELETALANIMATOR_H
#include <memory>
#include <string>
#include <vector>

#include "Core/Assets/MeshData.h"
#include "Core/Assets/MotionData.h"
#include "Renderer/RenderMath.h"

// Row-vector convention: world = local * parent world.
struct SkeletonData {
    std::vector<std::string> names;
    std::vector<uint32_t> hashes;  // murmur3 of the UTF-16 name, as mot clips reference bones
    std::vector<int32_t> parents;
    std::vector<Mat4> bindLocal;
    std::vector<Mat4> inverseBind;
    std::vector<uint16_t> remap;   // skin matrix slot -> joint (vertex bone indices address slots)

    static std::shared_ptr<const SkeletonData> FromMesh(const MeshData& mesh);
};

std::vector<Mat4> BindPoseWorld(const SkeletonData& skeleton, const Mat4& world);

// Per joint of skeleton: the same-named joint of other, or -1.
std::vector<int32_t> MatchJoints(const SkeletonData& skeleton, const SkeletonData& other);

// The engine's SameJointsConstraint: joints named like a leader joint take its
// world matrix, the others keep their bind offset from their parent.
void EvaluateFollower(const SkeletonData& skeleton, const Mat4& world, const std::vector<int32_t>& leaderJoint,
                      const std::vector<Mat4>& leaderWorld, float* out, std::vector<Mat4>& jointWorld);

// out: SkinningMatrices as the game's skinning VS expects them, world * pose *
// inverse bind, one per remap slot.
class SkeletalAnimator {
public:
    explicit SkeletalAnimator(std::shared_ptr<const SkeletonData> skeleton);

    // nullptr restores the bind pose. Returns how many joints the mot drives.
    std::size_t SetMotion(const MotData* motion);
    void Evaluate(float frame, const Mat4& world, float* out, std::vector<Mat4>* jointWorld = nullptr) const;


private:
    struct Channel {
        const MotTrack* translation = nullptr;
        const MotTrack* rotation = nullptr;
        const MotTrack* scale = nullptr;
    };

    std::shared_ptr<const SkeletonData> skeleton;
    std::vector<int32_t> order;  // parents before children
    std::vector<MotVec3> bindTranslation;
    std::vector<MotQuat> bindRotation;
    std::vector<MotVec3> bindScale;
    std::vector<Channel> channels;
};

#endif
