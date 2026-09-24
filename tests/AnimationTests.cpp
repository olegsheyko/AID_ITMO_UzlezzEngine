#include "AnimationFixture.h"
#include "animation/Animation.h"
#include "ecs/AnimationSystem.h"
#include "ecs/World.h"
#include "resources/MeshLoader.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(float a, float b, const char* message) { require(std::isfinite(a) && std::abs(a-b)<1e-5f,message); }
void compare(const AnimationPose& a,const AnimationPose& b) {
    require(a.globals.size()==b.globals.size() && a.palettes.size()==b.palettes.size(),"Pose dimensions differ");
    for (size_t i=0;i<a.globals.size();++i) for (size_t k=0;k<16;++k) close(a.globals[i].values[k],b.globals[i].values[k],"Global poses differ");
    for (size_t i=0;i<a.palettes.size();++i) {
        require(a.palettes[i].size()==b.palettes[i].size(),"Palette dimensions differ");
        for (size_t j=0;j<a.palettes[i].size();++j) for (size_t k=0;k<16;++k)
            close(a.palettes[i][j].values[k],b.palettes[i][j].values[k],"Skin palettes differ");
    }
}
void mathTests() {
    const float h=std::sqrt(.5f);
    auto q=Animation::slerp({},Quaternion{0,0,1,0},.5f);
    close(q.z,h,"Quaternion interpolation"); close(q.w,h,"Quaternion interpolation");
    q=Animation::slerp(Quaternion{0,0,h,h},Quaternion{0,0,-h,-h},.5f);
    close(q.z,h,"Quaternion shortest path");
    const Mat4 transform=Animation::compose({1,2,3},q,{2,3,4});
    close(transform.values[0],0,"Rotation matrix"); close(transform.values[1],2,"Rotation and scale");
    close(transform.values[4],-3,"Rotation and scale"); close(transform.values[14],3,"Translation");
    close(float(Animation::wrapTime(-.25,2)),1.75f,"Reverse looping");
    close(float(Animation::wrapTime(2,2)),0,"Loop boundary");
    close(float(Animation::wrapTime(5,0)),0,"Zero duration");
}
void hierarchyTests() {
    MeshData mesh;
    SkeletonNode root; root.bindLocal=Math::translation({10,0,0}); root.translation={10,0,0};
    SkeletonNode child; child.parent=0; child.translation={0,2,0}; child.bindLocal=Math::translation(child.translation);
    mesh.skeleton.nodes={root,child}; mesh.skeleton.rootInverse=Math::translation({-10,0,0});
    mesh.subMeshes.resize(1); mesh.subMeshes[0].bones.push_back({1,Math::translation({0,-2,0})});
    AnimationClip clip; clip.duration=2; clip.tracks.resize(2);
    clip.tracks[1].positions={{0,{0,2,0}},{1,{0,4,0}},{2,{0,2,0}}}; mesh.skeleton.clips.push_back(clip);
    AnimationPose pose; Animation::preparePose(mesh,pose); Animation::evaluate(mesh,0,.5,pose);
    close(pose.globals[1].values[12],10,"Parent transform");
    close(pose.palettes[0][0].values[12],0,"Root inverse");
    close(pose.palettes[0][0].values[13],1,"Inverse bind and interpolated position");
    Animation::evaluate(mesh,99,0,pose);
    close(pose.palettes[0][0].values[13],0,"Invalid clip uses bind pose");
}
void importAndJobs(const std::filesystem::path& directory) {
    auto resource=std::make_shared<Resource<MeshData>>("test skin");
    require(MeshLoader::decode(writeAnimationFixture(directory).string(),*resource->getData()),"glTF import failed");
    resource->setLoaded(true);
    const auto& mesh=*resource->getData();
    require(mesh.skeleton.clips.size()==1 && mesh.subMeshes[0].bones.size()==2,"Missing imported skin/clip");
    close(float(mesh.skeleton.clips[0].duration),2,"Animation seconds conversion");
    AnimationPose pose; Animation::preparePose(mesh,pose); Animation::evaluate(mesh,0,1,pose);
    const auto& vertex=mesh.subMeshes[0].vertices[0]; float displacement=0, total=0;
    for (int i=0;i<4;++i) { total+=vertex.boneWeights[i]; displacement+=vertex.boneWeights[i]*pose.palettes[0][vertex.boneIds[i]].values[12]; }
    close(total,1,"Imported weights normalized"); close(displacement,.75f,"Imported inverse bind/channel mapping");

    World world; std::vector<Entity> entities;
    for (int i=0;i<257;++i) {
        auto entity=world.createEntity(); entities.push_back(entity);
        world.addComponent<MeshRenderer>(entity).cachedMesh=resource;
        auto& animator=world.addComponent<Animator>(entity);
        animator.time=i/137.0; animator.speed=(i%2 ? -1.0f : 1.5f); animator.paused=i%7==0;
    }
    AnimationSystem system; system.parallel=false; system.update(world,1.0f/60);
    std::vector<Animator> baseline;
    for (Entity entity:entities) baseline.push_back(world.getComponent<Animator>(entity));
    auto& jobs=JobSystem::getInstance(); jobs.init({4});
    for (unsigned int batch : {1u,32u,1000u}) {
        system.batchSize=batch; system.parallel=true; system.update(world,0);
        for (size_t i=0;i<entities.size();++i) compare(baseline[i].pose,world.getComponent<Animator>(entities[i]).pose);
        jobs.collectCompleted();
    }
    system.paused=true; system.update(world,1);
    for (size_t i=0;i<entities.size();++i) close(float(world.getComponent<Animator>(entities[i]).time),float(baseline[i].time),"Global pause advanced time");
    // Removal/rebuild and stopped-scheduler fallback cannot leave tasks referring to ECS storage.
    for (Entity entity:entities) world.destroyEntity(entity);
    system.update(world,.1f); require(system.characterCount==0,"Deleted characters retained");
    jobs.shutdown();
    auto entity=world.createEntity(); world.addComponent<MeshRenderer>(entity).cachedMesh=resource;
    system.update(world,0); require(world.hasComponent<Animator>(entity),"Automatic Animator attachment");
    MeshData cube; require(MeshLoader::decode("primitive:cube",cube) && cube.skeleton.nodes.empty(),"Static mesh regression");
}
void inspectModel(const std::string& path, const std::filesystem::path& output, bool benchmark) {
    auto resource=std::make_shared<Resource<MeshData>>(path);
    require(MeshLoader::decode(path,*resource->getData()),"User model import failed");
    resource->setLoaded(true);
    const auto& mesh=*resource->getData();
    std::cout << "MODEL " << path << " nodes=" << mesh.skeleton.nodes.size() << " submeshes=" << mesh.subMeshes.size() << '\n';
    for (const auto& sub:mesh.subMeshes) {
        Vec3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
        for (const auto& v:sub.vertices) {
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        std::cout << "MESH vertices=" << sub.vertices.size() << " triangles=" << sub.indices.size()/3 << " bones=" << sub.bones.size()
            << " material=" << sub.material.name << " texture=" << sub.material.diffuseTexturePath << '\n';
        std::cout << "bounds " << lo.x << ' ' << lo.y << ' ' << lo.z << " to " << hi.x << ' ' << hi.y << ' ' << hi.z << '\n';
    }
    for (const auto& clip:mesh.skeleton.clips) {
        size_t tracks=0,keys=0;
        for (const auto& t:clip.tracks) { if (!t.positions.empty() || !t.rotations.empty() || !t.scales.empty()) ++tracks; keys+=t.positions.size()+t.rotations.size()+t.scales.size(); }
        std::cout << "CLIP " << clip.name << " seconds=" << clip.duration << " tracks=" << tracks << " keys=" << keys << '\n';
    }
    require(!mesh.skeleton.clips.empty(),"User model has no clips");
    AnimationPose first, later;
    Animation::preparePose(mesh,first); Animation::preparePose(mesh,later);
    Animation::evaluate(mesh,0,0,first); Animation::evaluate(mesh,0,mesh.skeleton.clips[0].duration*.25,later);
    for (int phase=-1;phase<2;++phase) {
        AnimationPose debug; Animation::preparePose(mesh,debug);
        Animation::evaluate(mesh,phase<0 ? ~0u : 0u,phase*.25,debug);
        Vec3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
        for (size_t s=0;s<mesh.subMeshes.size();++s) for (const auto& v:mesh.subMeshes[s].vertices) {
            Mat4 m{};
            for (size_t b=0;b<4;++b) for (size_t k=0;k<16;++k) m.values[k]+=debug.palettes[s][v.boneIds[b]].values[k]*v.boneWeights[b];
            Vec3 p{m.values[0]*v.position.x+m.values[4]*v.position.y+m.values[8]*v.position.z+m.values[12],
                m.values[1]*v.position.x+m.values[5]*v.position.y+m.values[9]*v.position.z+m.values[13],
                m.values[2]*v.position.x+m.values[6]*v.position.y+m.values[10]*v.position.z+m.values[14]};
            lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
        }
        // Walking.fbx stands on Y=0 and is ~181 source units tall. Previously
        // FBX pivot helpers doubled the animated translations (feet at Y~188).
        require(lo.y>-5 && lo.y<10 && hi.y>150 && hi.y<195,"FBX pivot transforms doubled or standing pose bounds corrupted");
    }
    double difference=0;
    for (size_t s=0;s<first.palettes.size();++s) for (size_t b=0;b<first.palettes[s].size();++b)
        for (size_t k=0;k<16;++k) {
            require(std::isfinite(later.palettes[s][b].values[k]),"FBX contains non-finite pose");
            difference+=std::abs(first.palettes[s][b].values[k]-later.palettes[s][b].values[k]);
        }
    require(difference>1,"FBX clip does not animate the skin");
    Animation::evaluate(mesh,0,mesh.skeleton.clips[0].duration,later); compare(first,later);
    World world; std::vector<Entity> entities;
    for (int i=0;i<512;++i) {
        Entity e=world.createEntity(); entities.push_back(e);
        world.addComponent<MeshRenderer>(e).cachedMesh=resource;
        world.addComponent<Animator>(e).time=mesh.skeleton.clips[0].duration*i/512;
    }
    AnimationSystem system; system.parallel=false; system.update(world,0);
    std::vector<AnimationPose> poses;
    for (Entity e:entities) poses.push_back(world.getComponent<Animator>(e).pose);
    auto& jobs=JobSystem::getInstance(); jobs.init({4});
    system.parallel=true; system.update(world,0);
    for (size_t i=0;i<entities.size();++i) compare(poses[i],world.getComponent<Animator>(entities[i]).pose);
    std::cout << "PASS: actual FBX poses change, loop and match sequential/parallel for 512 characters\n";
    if (benchmark) {
        std::ofstream csv(output/"walking-cpu.csv");
        csv << "mode,run,frame,characters,workers,batch,cpu_update_ms\n";
        // Both modes evaluate identical phases. Alternate mode order between runs.
        for (int run=0;run<3;++run) for (int mode=0;mode<2;++mode) {
            system.parallel=((mode+run)%2)!=0;
            for (int frame=-30;frame<180;++frame) {
                for (size_t i=0;i<entities.size();++i)
                    world.getComponent<Animator>(entities[i]).time=Animation::wrapTime((frame+30)/60.0+mesh.skeleton.clips[0].duration*i/512,mesh.skeleton.clips[0].duration);
                system.update(world,0);
                jobs.collectCompleted();
                if (frame>=0) csv << (system.parallel ? "parallel" : "sequential") << ',' << run << ',' << frame << ",512,4,32," << system.lastUpdateMs << '\n';
            }
        }
        require(csv.good(),"Failed to write animation benchmark CSV");
    }
    jobs.shutdown();
}
}
int main(int argc,char** argv) {
    try {
        require(argc>=2 && argc<=4,"Expected fixture output directory [model [--benchmark]]");
        mathTests(); hierarchyTests(); importAndJobs(argv[1]);
        if (argc>=3) inspectModel(argv[2],argv[1],argc==4 && std::string(argv[3])=="--benchmark");
        std::cout << "PASS: interpolation, looping, hierarchy, glTF import, 257 sequential/parallel poses, pause and lifecycle\n";
    } catch (const std::exception& e) {
        JobSystem::getInstance().shutdown(); std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
    return 0;
}
