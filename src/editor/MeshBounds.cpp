#include "editor/MeshBounds.h"

#include "animation/Animation.h"
#include "editor/EditorMath.h"
#include "resources/ResourceTypes.h"

#include <algorithm>
#include <limits>

using namespace EditorMath;

namespace MeshBounds {
std::size_t vertexCount(const MeshData& data) {
    std::size_t count = data.vertices.size();
    for (const SubMesh& subMesh : data.subMeshes) {
        count += subMesh.vertices.size();
    }
    return count;
}

bool computeBindPose(const MeshData& data, Vec3& outMin, Vec3& outMax) {
    outMin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    outMax = {-outMin.x, -outMin.y, -outMin.z};
    bool any = false;
    auto consume = [&](const Vec3& p) {
        any = true;
        outMin = {std::min(outMin.x, p.x), std::min(outMin.y, p.y), std::min(outMin.z, p.z)};
        outMax = {std::max(outMax.x, p.x), std::max(outMax.y, p.y), std::max(outMax.z, p.z)};
    };

    if (data.subMeshes.empty()) {
        for (const Vertex& vertex : data.vertices) {
            consume(vertex.position);
        }
        return any;
    }

    const bool hasSkeleton = !data.skeleton.nodes.empty();
    AnimationPose bind;
    if (hasSkeleton) {
        Animation::preparePose(data, bind);
        Animation::evaluate(data, std::numeric_limits<unsigned int>::max(), 0, bind);
    }
    for (std::size_t s = 0; s < data.subMeshes.size(); ++s) {
        const SubMesh& sub = data.subMeshes[s];
        Mat4 nodeTransform = Mat4::identity();
        if (hasSkeleton && sub.skeletonNode < bind.globals.size()) {
            nodeTransform = Math::multiply(data.skeleton.rootInverse, bind.globals[sub.skeletonNode]);
        }
        const bool skinned = hasSkeleton && !sub.bones.empty() && s < bind.palettes.size();
        for (const Vertex& vertex : sub.vertices) {
            Mat4 transform = nodeTransform;
            float total = 0.0f;
            for (float weight : vertex.boneWeights) {
                total += weight;
            }
            if (skinned && total > 0.0f) {
                transform = {};
                for (std::size_t b = 0; b < 4; ++b) {
                    const int bone = vertex.boneIds[b];
                    if (bone < 0 || static_cast<std::size_t>(bone) >= bind.palettes[s].size()) {
                        continue;
                    }
                    for (std::size_t k = 0; k < 16; ++k) {
                        transform.values[k] += bind.palettes[s][bone].values[k] * vertex.boneWeights[b];
                    }
                }
            }
            consume(transformPoint(transform, vertex.position));
        }
    }
    return any;
}

bool isImportedModel(const std::string& path) {
    return path.rfind("primitive:", 0) != 0;
}

Mat4 sourceBasis(bool yUpSource) {
    return yUpSource ? Math::rotationX(kPi * 0.5f) : Mat4::identity();
}
} // namespace MeshBounds
