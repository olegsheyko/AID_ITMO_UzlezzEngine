#pragma once
#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/SkinBounds.h"
#include <unordered_set>

namespace SceneVisibility {
inline Mat4 worldMatrix(const World& world, Entity e, std::unordered_set<Entity>& visited) {
    if (!world.isAlive(e) || !world.hasComponent<Transform>(e) || !visited.insert(e).second)
        return Mat4::identity();
    const auto& t=world.getComponent<Transform>(e);
    Mat4 m=Math::composeTransform(t.position,t.rotation,t.scale);
    if (world.hasComponent<Hierarchy>(e)) {
        Entity parent=world.getComponent<Hierarchy>(e).parent;
        if (parent!=kInvalidEntity) m=Math::multiply(worldMatrix(world,parent,visited),m);
    }
    visited.erase(e);
    return m;
}
// Apply registered edits only; static worlds do no scene traversal here.
// Pending resources are the only polled subset, until their bounds become known.
inline std::size_t synchronize(World& world) {
    const World& read=world;
    auto dirty=world.takeSpatialChanges();
    dirty.insert(world.visibilityPending.begin(),world.visibilityPending.end());
    std::vector<Entity> queue(dirty.begin(),dirty.end());
    for (std::size_t i=0;i<queue.size();++i) {
        const Entity e=queue[i];
        if (read.isAlive(e) && read.hasComponent<Hierarchy>(e))
            for (Entity child:read.getComponent<Hierarchy>(e).children)
                if (dirty.insert(child).second) queue.push_back(child);
    }
    for (Entity e:queue) {
        auto old=world.visibilityHandles.find(e);
        const bool exists=read.isAlive(e) && read.hasComponent<Transform>(e) && read.hasComponent<MeshRenderer>(e);
        const MeshRenderer* mr=exists ? &read.getComponent<MeshRenderer>(e):nullptr;
        if (!mr || !mr->visible || !mr->cachedMesh || !mr->cachedShader) {
            if (old!=world.visibilityHandles.end()) {
                world.visibility.unregisterObject(old->second);
                world.visibilityHandles.erase(old);
            }
            world.visibilityPending.erase(e);
            continue;
        }
        if (mr->cachedMesh->isPending() || mr->cachedShader->isPending()) world.visibilityPending.insert(e);
        else world.visibilityPending.erase(e);
        Visibility::Record record;
        record.entity=e;
        std::unordered_set<Entity> visited;
        record.model=worldMatrix(read,e,visited);
        if (mr->yUpSource) record.model=Math::multiply(record.model,Math::rotationX(1.5707963f));
        Visibility::Bounds local;
        if (mr->cachedMesh->isLoaded()) {
            const auto& mesh=*mr->cachedMesh->getData();
            const AnimationPose* pose=nullptr;
            if (read.hasComponent<Animator>(e)) {
                const auto& animator=read.getComponent<Animator>(e);
                if (animator.evaluatedMesh==&mesh &&
                    animator.pose.globals.size()==mesh.skeleton.nodes.size() &&
                    animator.pose.palettes.size()==mesh.subMeshes.size())
                    pose=&animator.pose;
            }
            record.alwaysVisible=!mesh.skeleton.nodes.empty() && !pose;
            bool any=false;
            auto consume=[&](const Vertex& v) {
                if (!any) { local.min=local.max=v.position; any=true; }
                else {
                    local.min={std::min(local.min.x,v.position.x),std::min(local.min.y,v.position.y),std::min(local.min.z,v.position.z)};
                    local.max={std::max(local.max.x,v.position.x),std::max(local.max.y,v.position.y),std::max(local.max.z,v.position.z)};
                }
            };
            if (mesh.subMeshes.empty()) for (const auto& v:mesh.vertices) consume(v);
            else for (std::size_t s=0;s<mesh.subMeshes.size();++s) {
                const auto& sub=mesh.subMeshes[s];
                Mat4 node=Mat4::identity();
                if (pose && sub.skeletonNode<pose->globals.size())
                    node=Math::multiply(mesh.skeleton.rootInverse,pose->globals[sub.skeletonNode]);
                Visibility::Bounds bounds;
                if (SkinBounds::evaluate(sub,node,pose ? &pose->palettes[s]:nullptr,bounds)) {
                    Vertex low{},high{}; low.position=bounds.min; high.position=bounds.max;
                    consume(low); consume(high);
                } else record.alwaysVisible=true;
            }
            record.alwaysVisible=record.alwaysVisible || !any;
        }
        record.bounds=Visibility::transformed(local,record.model);
        if (old==world.visibilityHandles.end())
            world.visibilityHandles[e]=world.visibility.registerObject(record);
        else world.visibility.update(old->second,record);
    }
    return queue.size();
}
}
