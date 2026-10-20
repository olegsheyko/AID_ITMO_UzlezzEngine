#pragma once
#include "resources/ResourceTypes.h"
#include "ecs/Visibility.h"
#include <cmath>

namespace SkinBounds {
inline void include(SkinBoundsBox& box, Vec3 p) {
    if (!box.valid) { box.min=box.max=p; box.valid=true; return; }
    box.min={std::min(box.min.x,p.x),std::min(box.min.y,p.y),std::min(box.min.z,p.z)};
    box.max={std::max(box.max.x,p.x),std::max(box.max.y,p.y),std::max(box.max.z,p.z)};
}
inline const SkinBoundsCache& prepare(const SubMesh& sub) {
    auto& cache=sub.visibilityBounds;
    if (cache.ready) return cache;
    cache={}; cache.ready=true; cache.bones.resize(sub.bones.size());
    for (const auto& vertex:sub.vertices) {
        include(cache.all,vertex.position);
        float sum=0;
        for (float weight:vertex.boneWeights) sum+=weight;
        if (sum<=0) { include(cache.unweighted,vertex.position); continue; }
        if (!std::isfinite(sum) || std::abs(sum-1.f)>1e-5f) cache.safe=false;
        for (std::size_t i=0;i<4;++i) {
            const float weight=vertex.boneWeights[i];
            if (weight==0) continue;
            const int bone=vertex.boneIds[i];
            if (weight<0 || bone<0 || std::size_t(bone)>=cache.bones.size()) {
                cache.safe=false; continue;
            }
            include(cache.bones[bone],vertex.position);
        }
    }
    return cache;
}
inline bool evaluate(const SubMesh& sub, const Mat4& node,
                     const std::vector<Mat4>* palette, Visibility::Bounds& result) {
    const auto& cache=prepare(sub);
    SkinBoundsBox merged;
    auto transform=[&](const SkinBoundsBox& box,const Mat4& matrix) {
        if (!box.valid) return;
        const auto bounds=Visibility::transformed({box.min,box.max},matrix);
        include(merged,bounds.min); include(merged,bounds.max);
    };
    if (palette && !sub.bones.empty() && palette->size()==sub.bones.size()) {
        if (!cache.safe) return false;
        // Nonnegative normalized skin weights form a convex combination.
        // Union of transformed influence boxes therefore contains every vertex.
        for (std::size_t i=0;i<cache.bones.size();++i) transform(cache.bones[i],(*palette)[i]);
        transform(cache.unweighted,node);
    } else transform(cache.all,node);
    if (!merged.valid) return false;
    // Cover normalized-weight rounding and CPU/GPU arithmetic differences.
    const float extent=std::max({std::abs(merged.min.x),std::abs(merged.min.y),
        std::abs(merged.min.z),std::abs(merged.max.x),std::abs(merged.max.y),std::abs(merged.max.z),1.f});
    const float pad=extent*2e-5f;
    result={{merged.min.x-pad,merged.min.y-pad,merged.min.z-pad},
            {merged.max.x+pad,merged.max.y+pad,merged.max.z+pad}};
    return true;
}
}
