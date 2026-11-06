#include "bench/CullingBenchmark.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"
#include "math/CameraMath.h"
#include "core/Logger.h"
#include "editor/MeshBounds.h"
#include <imgui.h>
#include <glad/glad.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>

CullingBenchmark::CullingBenchmark(IRenderAdapter& renderer,Config config)
    :renderer_(renderer),config_(std::move(config)),render_(renderer) {}
void CullingBenchmark::onEnter() {
    auto& rm=ResourceManager::getInstance();
    mesh_=rm.loadMesh("primitive:cube");
    shader_=rm.loadShader("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
    if (!mesh_ || !mesh_->isLoaded() || !shader_ || !shader_->isLoaded()) {
        failed_=finished_=true; return;
    }
    for (int i=0;i<8;++i) {
        auto texture=std::make_shared<Resource<TextureData>>();
        unsigned char pixels[3]={static_cast<unsigned char>(60+(i*71)%190),
            static_cast<unsigned char>(60+(i*113)%190),static_cast<unsigned char>(60+(i*43)%190)};
        if (!renderer_.createTexture(1,1,3,pixels,texture->getData()->textureId)) {failed_=finished_=true;return;}
        renderer_.setDebugLabel(IRenderAdapter::DebugObject::Texture,texture->getData()->textureId,"Bench texture "+std::to_string(i+1));
        texture->setLoaded(true);
        textures_.push_back(texture);
    }
    for (unsigned int i=0;i<10000;++i) objects_.push_back(addObject(i));
    if (config_.animated) {
        auto character=rm.loadMesh("assets/models/animation/Walking.fbx");
        Vec3 minimum{},maximum{};
        if (!character || !character->isLoaded() || !MeshBounds::computeBindPose(*character->getData(),minimum,maximum)) {
            failed_=finished_=true; return;
        }
        const float scale=2.f/std::max(.001f,maximum.y-minimum.y);
        for (int i=0;i<100;++i) {
            const Entity e=objects_[i];
            auto& t=world_.getComponent<Transform>(e);
            t.position={float(i%10-5)*3.f,float(i/10)*3.f,-minimum.y*scale};
            t.scale={scale,scale,scale};
            auto& mr=world_.getComponent<MeshRenderer>(e);
            mr.cachedMesh=character; mr.yUpSource=true;
            world_.addComponent<Animator>(e).time=double(i)*.023;
        }
        animation_.update(world_,0);
    }
    camera_=world_.createEntity();
    world_.addComponent<Camera>(camera_);
    render_.cullingEnabled=config_.culling;
    render_.sortingEnabled=config_.sorting;
    render_.depthSortingEnabled=config_.depthSort;
    setCamera(0);
    LOG_INFO("Culling scene: 10000 objects, 8 texture groups");
}
void CullingBenchmark::onExit() {
    for (auto& texture:textures_) renderer_.destroyTexture(texture->getData()->textureId);
}
Entity CullingBenchmark::addObject(unsigned int index) {
    const Entity e=world_.createEntity();
    auto& t=world_.addComponent<Transform>(e);
    // Coprime permutation distributes depth independently of creation order.
    const unsigned int cell=(index*7919u)%10000u;
    t.position={float(int(cell%100)-50)*4.f,float(int(cell/100)-50)*4.f,float(index%5)*1.8f};
    t.scale={.8f,.8f,.8f};
    auto& mr=world_.addComponent<MeshRenderer>(e);
    mr.cachedMesh=mesh_; mr.cachedShader=shader_; mr.cachedBaseColorTexture=textures_[index%textures_.size()];
    return e;
}
void CullingBenchmark::setCamera(unsigned int frame) {
    const float t=frame*.008f;
    eye_={35.f*std::sin(t),-35.f*std::cos(t),14.f+4.f*std::sin(t*.6f)};
    yaw_=t*.7f; pitch_=-.12f;
    int w=0,h=0; renderer_.getFramebufferSize(w,h);
    auto& c=world_.getComponent<Camera>(camera_);
    c.viewMatrix=CameraMath::view(eye_,pitch_,yaw_);
    c.projectionMatrix=Math::perspective(.9f,h>0 ? float(w)/h:1.333333f,.1f,180.f);
}
void CullingBenchmark::beginFrame(double frameMs) {
    lastFrameMs_=frameMs;
    if (previousMeasured_) samples_.push_back({frameMs,lastStats_});
    previousMeasured_=false;
    if (!config_.live && samples_.size()>=config_.frames) finish();
}
void CullingBenchmark::update(float dt) {
    if (finished_) return;
    if (fly_) setCamera(frame_);
    else {
        auto& io=ImGui::GetIO();
        if (!io.WantCaptureKeyboard) {
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) yaw_+=dt;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) yaw_-=dt;
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) pitch_=std::min(1.5f,pitch_+dt);
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) pitch_=std::max(-1.5f,pitch_-dt);
            const auto f=CameraMath::forward(pitch_,yaw_),r=CameraMath::right(yaw_);
            float forward=float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S));
            float right=float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A));
            eye_.x+=dt*30*(f.x*forward+r.x*right); eye_.y+=dt*30*(f.y*forward+r.y*right); eye_.z+=dt*30*f.z*forward;
        }
        world_.getComponent<Camera>(camera_).viewMatrix=CameraMath::view(eye_,pitch_,yaw_);
    }
    if (move_) for (std::size_t i=0;i<std::min<std::size_t>(100,objects_.size());++i)
        world_.getComponent<Transform>(objects_[i]).position.z=4.f+3.f*std::sin(frame_*.04f+i);
    if (config_.animated) animation_.update(world_,1.f/60.f); // identical poses across modes
    previousMeasured_=!config_.live && frame_>=config_.warmup;
    ++frame_;
}
void CullingBenchmark::render() {
    if (finished_) return;
    if (config_.verify && frame_==1) {
        verifyImages();
        if (failed_) { finish(); return; }
        renderer_.beginFrame(.075f,.075f,.08f);
        setCamera(0);
    }
    {
        RenderDebugScope pass(renderer_,"Culling benchmark frame");
        render_.render(world_);
    }
    lastStats_=render_.stats();
    if (!config_.live) return; // benchmark excludes UI
    const int mode=(render_.cullingEnabled ? 2:0)+(render_.sortingEnabled ? 1:0);
    modeTotal_[mode]+=lastStats_.prepareMs+lastStats_.submitMs;
    ++modeSamples_[mode];
    ImGui::SetNextWindowSize({470,440},ImGuiCond_FirstUseEver);
    ImGui::Begin("Culling + submission acceptance");
    ImGui::Text("Scene: %zu    Visible: %zu    Sent to GPU: %zu",world_.visibility.size(),lastStats_.visible,lastStats_.submitted);
    ImGui::Text("Frame %.3f ms    CPU render %.3f ms",lastFrameMs_,lastStats_.prepareMs+lastStats_.submitMs);
    ImGui::Text("Prepare %.3f ms    Submit %.3f ms",lastStats_.prepareMs,lastStats_.submitMs);
    ImGui::Text("Shader changes %zu    Texture changes %zu",lastStats_.shaderChanges,lastStats_.textureChanges);
    ImGui::Text("Depth inversions %zu    Spatial updates %zu",lastStats_.depthInversions,lastStats_.updates);
    ImGui::Checkbox("CPU frustum culling",&render_.cullingEnabled);
    ImGui::Checkbox("Resource grouping + front-to-back",&render_.sortingEnabled);
    ImGui::Checkbox("Front-to-back within resource group",&render_.depthSortingEnabled);
    ImGui::Checkbox("Fly camera (deterministic route)",&fly_);
    ImGui::Checkbox("Move 100 registered objects",&move_);
    ImGui::TextUnformatted("Manual camera: disable Fly; WASD + arrow keys");
    if (ImGui::Button("Add 100")) for (int i=0;i<100;++i) objects_.push_back(addObject(serial_++));
    ImGui::SameLine();
    if (ImGui::Button("Delete 100")) for (int i=0;i<100 && !objects_.empty();++i) {
        world_.destroyEntity(objects_.back()); objects_.pop_back();
    }
    if (ImGui::Button("Reset comparison averages")) for (int i=0;i<4;++i) {modeTotal_[i]=0;modeSamples_[i]=0;}
    for (int i=0;i<4;++i) if (modeSamples_[i])
        ImGui::Text("Culling %s / Sort %s: %.3f ms CPU (%u frames)",i&2 ? "ON":"OFF",i&1 ? "ON":"OFF",
            modeTotal_[i]/modeSamples_[i],modeSamples_[i]);
    ImGui::TextWrapped("Live averages are illustrative. Reproducible median/p95/p99: run the offline matrix.");
    ImGui::End();
}
void CullingBenchmark::finish() {
    finished_=true;
    const std::filesystem::path path(config_.output);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {failed_=true;return;}
    int width=0,height=0;
    renderer_.getFramebufferSize(width,height);
    out<<"# objects=10000\n# gpu_sync=glFinish_before_present\n# warmup="<<config_.warmup<<"\n# vsync=0\n# culling="<<config_.culling
       <<"\n# animated="<<(config_.animated ? 100:0)<<"\n# sorting="<<config_.sorting<<"\n# depth_sort="<<config_.depthSort<<"\n# complete="<<(!failed_ && samples_.size()==config_.frames)
       <<"\n# gpu="<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))
       <<"\n# gl="<<reinterpret_cast<const char*>(glGetString(GL_VERSION))
       <<"\n# framebuffer="<<width<<'x'<<height
       <<"\nframe,frame_ms,prepare_ms,submit_ms,registered,visible,submitted,updates,shader_changes,texture_changes,depth_inversions\n";
    for (std::size_t i=0;i<samples_.size();++i) {
        const auto& s=samples_[i]; const auto& r=s.stats;
        out<<i<<','<<s.frame<<','<<r.prepareMs<<','<<r.submitMs<<','<<r.registered<<','<<r.visible<<','<<r.submitted
           <<','<<r.updates<<','<<r.shaderChanges<<','<<r.textureChanges<<','<<r.depthInversions<<'\n';
    }
}
void CullingBenchmark::verifyImages() {
    // Real framebuffer comparison, outside the measured interval. GL calls are
    // confined to this test harness; game objects and the spatial service have none.
    int w=0,h=0; renderer_.getFramebufferSize(w,h);
    std::vector<unsigned char> baseline(std::size_t(w)*h*3),candidate(baseline.size());
    auto capture=[&](bool cull,bool sort,std::vector<unsigned char>& pixels) {
        renderer_.beginFrame(.075f,.075f,.08f);
        render_.cullingEnabled=cull;render_.sortingEnabled=sort;
        render_.render(world_);
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    };
    const auto folder=std::filesystem::path(config_.output).parent_path();
    std::filesystem::create_directories(folder.empty() ? ".":folder);
    std::ofstream report(folder/"visual-check.csv");
    report<<"pose,culling,sorting,different_channels,max_error\n";
    for (unsigned int pose=0;pose<12;++pose) {
        // Exercise lifetime and transform updates between queries on the SAME service.
        // These extra objects are removed again before timed frames.
        std::vector<Entity> temporary;
        if (pose>=6) {
            for (unsigned int i=0;i<100;++i) temporary.push_back(addObject(10000+i+pose*100));
            for (Entity e:temporary) {
                auto& t=world_.getComponent<Transform>(e);
                t.position={float(int(e%10)-5)*4.f,float(int((e/10)%10)-5)*4.f,25.f};
                t.rotation={.3f,.2f,float(e)*.1f};
                t.scale={-1.3f,2.f,.6f};
            }
        }
        setCamera(pose*83);
        if (config_.animated) animation_.update(world_,.137f);
        capture(false,false,baseline);
        for (int mode=1;mode<4;++mode) {
            capture((mode&2)!=0,(mode&1)!=0,candidate);
            std::size_t different=0; int maxError=0;
            for (std::size_t i=0;i<baseline.size();++i) {
                int error=std::abs(int(baseline[i])-int(candidate[i]));
                if (error) ++different;
                maxError=std::max(maxError,error);
            }
            report<<pose<<','<<bool(mode&2)<<','<<bool(mode&1)<<','<<different<<','<<maxError<<'\n';
            if (maxError>2) failed_=true;
        }
        if (pose==0) {
            auto save=[&](const char* name,const auto& bytes) {
                std::ofstream image(folder/name,std::ios::binary);
                image<<"P6\n"<<w<<' '<<h<<"\n255\n";
                for (int y=h-1;y>=0;--y) image.write(reinterpret_cast<const char*>(bytes.data()+std::size_t(y)*w*3),w*3);
            };
            save("before.ppm",baseline); save("after.ppm",candidate);
        }
        for (Entity e:temporary) world_.destroyEntity(e);
    }
    render_.cullingEnabled=config_.culling;render_.sortingEnabled=config_.sorting;
    LOG_INFO(std::string("Culling image equivalence: ")+(failed_ ? "FAILED":"PASSED"));
}
