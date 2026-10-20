#include "ecs/SceneVisibility.h"
#include "math/CameraMath.h"
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        Visibility::Service service;
        Visibility::Record record;
        auto h=service.registerObject(record);
        check(service.query(Mat4::identity()).size()==1,"inside");
        record.bounds={{2,2,2},{3,3,3}};
        check(service.update(h,record),"update registered");
        check(service.query(Mat4::identity()).empty(),"outside");
        check(service.query(Mat4::identity(),false).size()==1,"disabled");
        record.bounds={{1,-.2f,-.2f},{2,.2f,.2f}};
        service.update(h,record);
        check(service.query(Mat4::identity()).size()==1,"plane touching");
        record.bounds={{-10,-10,-10},{10,10,10}};
        service.update(h,record);
        check(service.query(Mat4::identity()).size()==1,"camera enclosed");
        check(service.unregisterObject(h) && !service.update(h,record),"stale handle");
        auto next=service.registerObject(record);
        service.clear();
        check(service.registerObject(record)!=next,"clear invalidates handles");

        World world;
        auto mesh=std::make_shared<Resource<MeshData>>();
        mesh->getData()->vertices={Vertex{{-.5f,-.5f,-.5f}},Vertex{{.5f,.5f,.5f}}};
        mesh->setLoaded(true);
        auto shader=std::make_shared<Resource<ShaderData>>();
        shader->setLoaded(true);
        auto add=[&](Vec3 position) {
            Entity e=world.createEntity();
            world.addComponent<Transform>(e).position=position;
            auto& mr=world.addComponent<MeshRenderer>(e);
            mr.cachedMesh=mesh; mr.cachedShader=shader;
            return e;
        };
        Entity e=add({});
        SceneVisibility::synchronize(world);
        auto original=world.visibilityHandles.at(e);
        check(SceneVisibility::synchronize(world)==0,"static scene must not rescan");
        check(world.visibility.query(Mat4::identity()).size()==1,"registered entity");
        world.getComponent<Transform>(e).position={5,0,0};
        SceneVisibility::synchronize(world);
        check(world.visibilityHandles.at(e)==original,"update preserves handle");
        check(world.visibility.query(Mat4::identity()).empty(),"move");
        // Independent second camera, without rebuilding/consuming the service.
        check(world.visibility.query(Math::translation({-5,0,0})).size()==1,"second camera");
        world.getComponent<Transform>(e).scale={-12,2,2};
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).size()==1,"negative scale bounds");
        world.getComponent<MeshRenderer>(e).visible=false;
        SceneVisibility::synchronize(world);
        check(world.visibility.size()==0,"visibility removal");
        world.getComponent<MeshRenderer>(e).visible=true;
        SceneVisibility::synchronize(world);
        check(world.visibilityHandles.at(e)!=original,"reregister handle");
        world.removeComponent<Transform>(e);
        SceneVisibility::synchronize(world);
        check(world.visibility.size()==0,"component removal");
        world.destroyEntity(e);
        Entity parent=world.createEntity();
        world.addComponent<Transform>(parent).position={5,0,0};
        Entity child=add({});
        world.addComponent<Hierarchy>(parent).children.push_back(child);
        world.addComponent<Hierarchy>(child).parent=parent;
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).empty(),"parent transform");
        world.getComponent<Transform>(parent).position={};
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).size()==1,"parent dirty propagation");
        world.destroyEntity(parent);
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).size()==1,"parent destruction");
        world.destroyEntity(child);
        check(world.visibility.size()==0,"destroy unregisters immediately");
        world.clear();
        for (int i=0;i<10000;++i) add({float(i*3),0,0});
        SceneVisibility::synchronize(world);
        check(world.visibility.size()==10000,"10000 registered");
        check(SceneVisibility::synchronize(world)==0,"10000 static no rescan");
        check(world.visibility.query(Mat4::identity()).size()==1,"10000 culling");
        world.clear();
        check(world.visibility.size()==0,"world clear");
        // Placeholder becomes an offscreen loaded mesh without an ECS edit.
        mesh->setState(ResourceState::Queued);
        add({});
        SceneVisibility::synchronize(world);
        check(world.visibilityPending.size()==1,"pending resource tracked");
        mesh->getData()->vertices={Vertex{{10,10,10}},Vertex{{11,11,11}}};
        mesh->setLoaded(true);
        SceneVisibility::synchronize(world);
        check(world.visibilityPending.empty() && world.visibility.query(Mat4::identity()).empty(),"resource ready bounds");
        mesh->getData()->skeleton.nodes.resize(1);
        world.markSpatialDirty(world.getEntities()[0]);
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).size()==1,"skeletal conservative");
        const Entity animated=world.getEntities()[0];
        auto* data=mesh->getData();
        data->subMeshes.resize(1);
        auto& sub=data->subMeshes[0];
        sub.bones.resize(1);
        Vertex weighted{{0,0,0}};
        weighted.boneWeights[0]=1;
        sub.vertices={weighted};
        auto& animator=world.addComponent<Animator>(animated);
        animator.evaluatedMesh=data;
        animator.pose.globals={Mat4::identity()};
        animator.pose.palettes={{Math::translation({5,0,0})}};
        world.markSpatialDirty(animated);
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).empty(),"animated pose outside");
        animator.pose.palettes[0][0]=Mat4::identity();
        world.markSpatialDirty(animated);
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).size()==1,"animated pose enters frustum");
        sub.vertices[0].boneWeights[0]=0;
        sub.visibilityBounds={};
        animator.pose.globals[0]=Math::translation({5,0,0});
        world.markSpatialDirty(animated);
        SceneVisibility::synchronize(world);
        check(world.visibility.query(Mat4::identity()).empty(),"zero weight node transform");
        SubMesh blended;
        blended.bones.resize(2);
        Vertex v{{2,-3,4}};
        v.boneIds={0,1,0,0}; v.boneWeights={.25f,.75f,0,0};
        blended.vertices={v};
        std::vector<Mat4> palette={
            Math::composeTransform({5,2,-8},{.2f,.6f,.8f},{-2,3,.5f}),
            Math::composeTransform({-7,9,3},{.8f,.1f,-.7f},{.5f,-2,4})};
        Visibility::Bounds envelope;
        check(SkinBounds::evaluate(blended,Mat4::identity(),&palette,envelope),"blended envelope");
        const auto p0=Visibility::point(palette[0],v.position),p1=Visibility::point(palette[1],v.position);
        const Vec3 p{p0.x*.25f+p1.x*.75f,p0.y*.25f+p1.y*.75f,p0.z*.25f+p1.z*.75f};
        check(p.x>=envelope.min.x && p.x<=envelope.max.x &&
              p.y>=envelope.min.y && p.y<=envelope.max.y &&
              p.z>=envelope.min.z && p.z<=envelope.max.z,"negative scale blended vertex contained");
        blended.vertices[0].boneWeights[0]=-.25f; blended.visibilityBounds={};
        check(!SkinBounds::evaluate(blended,Mat4::identity(),&palette,envelope),"invalid weights require conservative fallback");
        std::cout<<"VisibilityTests: passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; return 1;
    }
}
