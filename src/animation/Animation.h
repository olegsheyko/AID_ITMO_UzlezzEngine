#pragma once
#include "math/MathTypes.h"
#include <string>
#include <vector>

// Immutable imported data. Nodes are in parent-before-child order.
constexpr unsigned int kMaxSkinBones = 128;
struct Quaternion { float x = 0, y = 0, z = 0, w = 1; };
struct SkeletonNode {
    std::string name;
    int parent = -1;
    Mat4 bindLocal = Mat4::identity();
    Vec3 translation{}, scale{1, 1, 1};
    Quaternion rotation{};
};
template<class T> struct AnimationKey { double time = 0; T value{}; };
struct AnimationTrack {
    std::vector<AnimationKey<Vec3>> positions, scales;
    std::vector<AnimationKey<Quaternion>> rotations;
};
struct AnimationClip {
    std::string name;
    double duration = 0; // seconds
    std::vector<AnimationTrack> tracks; // indexed by skeleton node
};
struct SkinBone { unsigned int node = 0; Mat4 inverseBind = Mat4::identity(); };
struct Skeleton {
    std::vector<SkeletonNode> nodes;
    std::vector<Mat4> bindGlobals;
    std::vector<AnimationClip> clips;
    Mat4 rootInverse = Mat4::identity();
};
struct AnimationPose {
    std::vector<Mat4> globals;
    std::vector<std::vector<Mat4>> palettes; // per submesh, preserves distinct inverse bind matrices
};
struct MeshData;
namespace Animation {
Quaternion slerp(Quaternion a, Quaternion b, float t);
Mat4 compose(Vec3 position, Quaternion rotation, Vec3 scale);
double wrapTime(double time, double duration);
void preparePose(const MeshData& mesh, AnimationPose& pose);
// No allocation after preparePose; no ECS, renderer or mutable shared resources.
void evaluate(const MeshData& mesh, unsigned int clip, double time, AnimationPose& pose);
}
