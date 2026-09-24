#include "animation/Animation.h"
#include "resources/ResourceTypes.h"
#include <algorithm>
#include <cmath>

namespace {
Quaternion normalized(Quaternion q) {
    const float length = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (length < 1e-8f) return {};
    return {q.x/length, q.y/length, q.z/length, q.w/length};
}
Vec3 interpolate(Vec3 a, Vec3 b, float t) {
    return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t};
}
Quaternion interpolate(Quaternion a, Quaternion b, float t) { return Animation::slerp(a,b,t); }
template<class T> T sample(const std::vector<AnimationKey<T>>& keys, double time, T fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    auto next = std::upper_bound(keys.begin(), keys.end(), time,
        [](double t, const AnimationKey<T>& key) { return t < key.time; });
    const auto& previous = *(next-1);
    const double span = next->time - previous.time;
    return interpolate(previous.value, next->value, span > 0 ? float((time-previous.time)/span) : 0);
}
}
Quaternion Animation::slerp(Quaternion a, Quaternion b, float t) {
    a = normalized(a); b = normalized(b);
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (dot < 0) { b = {-b.x,-b.y,-b.z,-b.w}; dot = -dot; }
    float left = 1-t, right = t;
    if (dot < 0.9995f) {
        const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
        left = std::sin((1-t)*angle)/std::sin(angle);
        right = std::sin(t*angle)/std::sin(angle);
    }
    return normalized({left*a.x+right*b.x,left*a.y+right*b.y,left*a.z+right*b.z,left*a.w+right*b.w});
}
Mat4 Animation::compose(Vec3 p, Quaternion q, Vec3 s) {
    q = normalized(q);
    Mat4 m = Mat4::identity();
    m.values[0] = (1-2*(q.y*q.y+q.z*q.z))*s.x;
    m.values[1] = 2*(q.x*q.y+q.z*q.w)*s.x;
    m.values[2] = 2*(q.x*q.z-q.y*q.w)*s.x;
    m.values[4] = 2*(q.x*q.y-q.z*q.w)*s.y;
    m.values[5] = (1-2*(q.x*q.x+q.z*q.z))*s.y;
    m.values[6] = 2*(q.y*q.z+q.x*q.w)*s.y;
    m.values[8] = 2*(q.x*q.z+q.y*q.w)*s.z;
    m.values[9] = 2*(q.y*q.z-q.x*q.w)*s.z;
    m.values[10] = (1-2*(q.x*q.x+q.y*q.y))*s.z;
    m.values[12]=p.x; m.values[13]=p.y; m.values[14]=p.z;
    return m;
}
double Animation::wrapTime(double time, double duration) {
    if (duration <= 0 || !std::isfinite(time)) return 0;
    time = std::fmod(time, duration);
    return time < 0 ? time+duration : time;
}
void Animation::preparePose(const MeshData& mesh, AnimationPose& pose) {
    pose.globals.resize(mesh.skeleton.nodes.size());
    pose.palettes.resize(mesh.subMeshes.size());
    for (size_t i=0; i<mesh.subMeshes.size(); ++i) pose.palettes[i].resize(mesh.subMeshes[i].bones.size());
}
void Animation::evaluate(const MeshData& mesh, unsigned int clipIndex, double time, AnimationPose& pose) {
    const auto& skeleton = mesh.skeleton;
    const AnimationClip* clip = clipIndex < skeleton.clips.size() ? &skeleton.clips[clipIndex] : nullptr;
    if (clip) time = wrapTime(time, clip->duration);
    for (size_t i=0; i<skeleton.nodes.size(); ++i) {
        const auto& node = skeleton.nodes[i];
        Mat4 local = node.bindLocal;
        if (clip && i < clip->tracks.size()) {
            const auto& track = clip->tracks[i];
            if (!track.positions.empty() || !track.rotations.empty() || !track.scales.empty())
                local = compose(sample(track.positions,time,node.translation),
                    sample(track.rotations,time,node.rotation), sample(track.scales,time,node.scale));
        }
        pose.globals[i] = node.parent < 0 ? local : Math::multiply(pose.globals[node.parent], local);
    }
    for (size_t i=0; i<mesh.subMeshes.size(); ++i) {
        const auto& bones = mesh.subMeshes[i].bones;
        for (size_t b=0; b<bones.size(); ++b)
            pose.palettes[i][b] = Math::multiply(skeleton.rootInverse,
                Math::multiply(pose.globals[bones[b].node], bones[b].inverseBind));
    }
}
