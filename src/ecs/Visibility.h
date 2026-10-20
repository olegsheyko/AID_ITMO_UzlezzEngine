#pragma once
#include "ecs/Entity.h"
#include "math/MathTypes.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Visibility {
struct Bounds { Vec3 min{-.5f,-.5f,-.5f}, max{.5f,.5f,.5f}; };
inline Vec3 point(const Mat4& m, Vec3 p) {
    return {m.values[0]*p.x+m.values[4]*p.y+m.values[8]*p.z+m.values[12],
            m.values[1]*p.x+m.values[5]*p.y+m.values[9]*p.z+m.values[13],
            m.values[2]*p.x+m.values[6]*p.y+m.values[10]*p.z+m.values[14]};
}
inline Bounds transformed(Bounds b, const Mat4& m) {
    Bounds out;
    out.min=out.max=point(m,b.min);
    for (int i=1;i<8;++i) {
        const Vec3 p=point(m,{i&1 ? b.max.x:b.min.x,i&2 ? b.max.y:b.min.y,i&4 ? b.max.z:b.min.z});
        out.min={std::min(out.min.x,p.x),std::min(out.min.y,p.y),std::min(out.min.z,p.z)};
        out.max={std::max(out.max.x,p.x),std::max(out.max.y,p.y),std::max(out.max.z,p.z)};
    }
    return out;
}
struct Frustum {
    Vec4 planes[6];
    explicit Frustum(const Mat4& clip) {
        for (int axis=0;axis<3;++axis) for (int side=0;side<2;++side) {
            const float sign=side ? -1.f:1.f;
            planes[axis*2+side]={clip.values[3]+sign*clip.values[axis],
                clip.values[7]+sign*clip.values[4+axis],
                clip.values[11]+sign*clip.values[8+axis],
                clip.values[15]+sign*clip.values[12+axis]};
        }
    }
    bool intersects(Bounds b) const {
        for (const auto& p:planes) {
            const float d=p.x*(p.x>=0 ? b.max.x:b.min.x)+p.y*(p.y>=0 ? b.max.y:b.min.y)
                +p.z*(p.z>=0 ? b.max.z:b.min.z)+p.w;
            // Conservative at the boundary, including invalid/unknown data.
            if (d < -1e-4f) return false;
        }
        return true;
    }
};
using Handle=std::uint64_t;
struct Record {
    Entity entity=kInvalidEntity;
    Bounds bounds;
    Mat4 model=Mat4::identity();
    bool alwaysVisible=false;
};
// Owns spatial data. Queries never access World, graphics, or a scene snapshot.
// Handles are monotonic, including across clear(), so stale handles stay invalid.
class Service {
public:
    Handle registerObject(Record record) {
        const Handle h=++next_;
        records_.emplace(h,record);
        return h;
    }
    bool update(Handle h, Record record) {
        auto it=records_.find(h);
        if (it==records_.end()) return false;
        it->second=record;
        return true;
    }
    bool unregisterObject(Handle h) { return records_.erase(h)!=0; }
    const Record* get(Handle h) const {
        auto it=records_.find(h);
        return it==records_.end() ? nullptr:&it->second;
    }
    std::vector<Handle> query(const Mat4& viewProjection, bool culling=true) const {
        const Frustum frustum(viewProjection);
        std::vector<Handle> result;
        result.reserve(records_.size());
        for (const auto& [h,r]:records_)
            if (!culling || r.alwaysVisible || frustum.intersects(r.bounds)) result.push_back(h);
        return result;
    }
    std::size_t size() const { return records_.size(); }
    void clear() { records_.clear(); }
private:
    Handle next_=0;
    std::unordered_map<Handle,Record> records_;
};
}
