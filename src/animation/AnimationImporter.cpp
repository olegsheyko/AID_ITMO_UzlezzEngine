#include "animation/AnimationImporter.h"
#include "resources/ResourceTypes.h"
#include "core/Logger.h"
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace {
Mat4 matrix(const aiMatrix4x4& m) {
    Mat4 result;
    result.values = {m.a1,m.b1,m.c1,m.d1,m.a2,m.b2,m.c2,m.d2,
        m.a3,m.b3,m.c3,m.d3,m.a4,m.b4,m.c4,m.d4};
    return result;
}
Vec3 vector(const aiVector3D& v) { return {v.x,v.y,v.z}; }
Quaternion quaternion(const aiQuaternion& q) { return {q.x,q.y,q.z,q.w}; }
}
bool importAnimation(const aiScene& scene, MeshData& mesh) {
    bool animated = scene.HasAnimations();
    for (unsigned int i=0; i<scene.mNumMeshes; ++i) animated |= scene.mMeshes[i]->HasBones();
    if (!animated) return true; // Retain the existing static-mesh path.
    auto& skeleton = mesh.skeleton;
    std::unordered_map<std::string, unsigned int> nodes;
    std::unordered_map<const aiNode*, unsigned int> nodeIndices;
    bool valid = true;
    std::function<void(const aiNode*, int)> readNode = [&](const aiNode* node, int parent) {
        unsigned int index = static_cast<unsigned int>(skeleton.nodes.size());
        if (!nodes.emplace(node->mName.C_Str(), index).second) valid = false;
        nodeIndices[node] = index;
        aiVector3D scale, position; aiQuaternion rotation;
        node->mTransformation.Decompose(scale, rotation, position);
        skeleton.nodes.push_back({node->mName.C_Str(), parent, matrix(node->mTransformation),
            vector(position), vector(scale), quaternion(rotation)});
        skeleton.bindGlobals.push_back(parent < 0 ? matrix(node->mTransformation) :
            Math::multiply(skeleton.bindGlobals[parent], matrix(node->mTransformation)));
        for (unsigned int i=0; i<node->mNumChildren; ++i) readNode(node->mChildren[i], static_cast<int>(index));
    };
    readNode(scene.mRootNode, -1);
    if (!valid) { LOG_ERROR("Animation import: duplicate node names are unsupported"); return false; }
    aiMatrix4x4 inverse = scene.mRootNode->mTransformation;
    if (std::abs(inverse.Determinant()) < 1e-8f) {
        LOG_ERROR("Animation import: singular root transform"); return false;
    }
    inverse.Inverse(); skeleton.rootInverse = matrix(inverse);
    size_t subIndex = 0;
    unsigned int reducedVertices = 0;
    // Same traversal as MeshLoader::processNode, including mesh instances.
    std::function<void(const aiNode*)> readSkin = [&](const aiNode* node) {
        for (unsigned int i=0; i<node->mNumMeshes && valid; ++i) {
            const auto& source = *scene.mMeshes[node->mMeshes[i]];
            auto& sub = mesh.subMeshes.at(subIndex++);
            sub.skeletonNode = nodeIndices.at(node);
            if (source.mNumBones > kMaxSkinBones) {
                LOG_ERROR("Animation import: more than 128 bones in a submesh"); valid=false; return;
            }
            std::vector<unsigned int> influences(sub.vertices.size());
            for (unsigned int b=0; b<source.mNumBones; ++b) {
                const auto& bone = *source.mBones[b];
                auto found = nodes.find(bone.mName.C_Str());
                if (found == nodes.end()) { valid=false; return; }
                sub.bones.push_back({found->second, matrix(bone.mOffsetMatrix)});
                for (unsigned int w=0; w<bone.mNumWeights; ++w) {
                    const auto weight = bone.mWeights[w];
                    if (weight.mVertexId >= sub.vertices.size() || !std::isfinite(weight.mWeight) || weight.mWeight < 0) {
                        valid=false; return;
                    }
                    if (weight.mWeight == 0) continue;
                    auto& vertex = sub.vertices[weight.mVertexId];
                    if (++influences[weight.mVertexId] == 5) ++reducedVertices;
                    auto smallest = std::min_element(vertex.boneWeights.begin(), vertex.boneWeights.end());
                    if (weight.mWeight > *smallest) {
                        vertex.boneIds[smallest-vertex.boneWeights.begin()] = static_cast<int>(b);
                        *smallest = weight.mWeight;
                    }
                }
            }
            for (auto& vertex : sub.vertices) {
                float total = 0; for (float w : vertex.boneWeights) total += w;
                if (total > 0) for (float& w : vertex.boneWeights) w /= total;
            }
        }
        for (unsigned int i=0; i<node->mNumChildren && valid; ++i) readSkin(node->mChildren[i]);
    };
    readSkin(scene.mRootNode);
    if (!valid) { LOG_ERROR("Animation import: invalid skin"); return false; }
    if (reducedVertices) LOG_INFO("Animation import: kept four largest weights on " + std::to_string(reducedVertices) + " vertices");
    for (unsigned int c=0; c<scene.mNumAnimations; ++c) {
        const auto& source = *scene.mAnimations[c];
        if (source.mNumMeshChannels || source.mNumMorphMeshChannels) {
            LOG_ERROR("Animation import: vertex/morph animation is unsupported; export skeletal animation"); return false;
        }
        const double ticks = source.mTicksPerSecond > 0 ? source.mTicksPerSecond : 25.0;
        AnimationClip clip;
        clip.name = source.mName.length ? source.mName.C_Str() : "Clip " + std::to_string(c);
        clip.duration = source.mDuration / ticks;
        clip.tracks.resize(skeleton.nodes.size());
        for (unsigned int t=0; t<source.mNumChannels; ++t) {
            const auto& channel = *source.mChannels[t];
            auto found = nodes.find(channel.mNodeName.C_Str());
            if (found == nodes.end()) { LOG_ERROR("Animation import: missing channel node"); return false; }
            auto& track = clip.tracks[found->second];
            for (unsigned int k=0; k<channel.mNumPositionKeys; ++k)
                track.positions.push_back({channel.mPositionKeys[k].mTime/ticks,vector(channel.mPositionKeys[k].mValue)});
            for (unsigned int k=0; k<channel.mNumRotationKeys; ++k)
                track.rotations.push_back({channel.mRotationKeys[k].mTime/ticks,quaternion(channel.mRotationKeys[k].mValue)});
            for (unsigned int k=0; k<channel.mNumScalingKeys; ++k)
                track.scales.push_back({channel.mScalingKeys[k].mTime/ticks,vector(channel.mScalingKeys[k].mValue)});
            const auto sortKeys = [&](auto& keys) {
                std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
                if (!keys.empty()) clip.duration = std::max(clip.duration, keys.back().time);
            };
            sortKeys(track.positions); sortKeys(track.rotations); sortKeys(track.scales);
        }
        skeleton.clips.push_back(std::move(clip));
    }
    return true;
}
