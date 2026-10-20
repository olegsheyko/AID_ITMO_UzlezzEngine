#include "editor/EditorContext.h"
#include "editor/EditorWindows.h"
#include "resources/ResourceManager.h"
#include "core/Logger.h"
#include "editor/MeshBounds.h"

void EditorContext::loadCullingScene() {
    resetSceneState();
    scripts.stop();
    animationDemoEntities.clear();
    animationLoad.reset();
    world.clear();
    selected=controllableEntity=gameCameraEntity=editorCameraEntity=prefabPreview=kInvalidEntity;
    scenePath_="generated:culling-10000";
    cullingScene=true;
    cullingSerial=0; cullingTime=0; cullingMove=false; cullingFly=false;
    cullingCpuTotal={}; cullingFrameTotal={}; cullingSamples={};
    cullingObjects.clear(); cullingGroups.clear();
    // Collapsed groups keep the hierarchy usable with 10000 editable entities.
    for (int i=0;i<100;++i) {
        Entity group=createEmpty("Culling group "+std::to_string(i+1),{});
        world.addComponent<Hierarchy>(group);
        cullingGroups.push_back(group);
    }
    clearSelection();
    addCullingObjects(10000);
    auto character=ResourceManager::getInstance().loadMesh("assets/models/animation/Walking.fbx");
    if (character && character->isLoaded()) {
        Vec3 minimum{},maximum{};
        if (MeshBounds::computeBindPose(*character->getData(),minimum,maximum)) {
            const float scale=2.f/std::max(.001f,maximum.y-minimum.y);
            for (int i=0;i<100;++i) {
                Entity e=cullingObjects[i];
                world.getComponent<Tag>(e).name="Animated culling character "+std::to_string(i+1);
                auto& t=world.getComponent<Transform>(e);
                t.scale={scale,scale,scale};
                t.position={float(i%10-5)*3.f,float(i/10)*3.f,-minimum.y*scale};
                auto& mr=world.getComponent<MeshRenderer>(e);
                mr.meshId="assets/models/animation/Walking.fbx";
                mr.cachedMesh=character;
                mr.yUpSource=true;
                mr.baseColorTextureId.clear(); mr.cachedBaseColorTexture.reset();
                world.addComponent<Animator>(e).time=double(i)*.023;
            }
            animationSystem.update(world,0);
        }
    } else LOG_ERROR("Culling scene: Walking.fbx failed to load");
    createGameCamera();
    auto& game=world.getComponent<Transform>(gameCameraEntity);
    game.position={0,-35,14}; game.rotation={-.12f,0,0};
    createEditorCameraEntity();
    camera.setPose({0,-35,14},-.12f,0);
    camera.moveSpeed=30.f;
    setCameraMode();
    renderSystem.cullingEnabled=true; renderSystem.sortingEnabled=true;
    sceneView.showStats=true;
    sceneView.showGrid=false;
    sceneView.showCameraIcons=false;
    focusWindowRequest=EditorWindow::kScene;
    LOG_INFO("Editor: culling acceptance scene, 10000 editable objects");
}

void EditorContext::addCullingObjects(int count) {
    if (!cullingScene || cullingGroups.empty()) return;
    cullingCpuTotal={}; cullingFrameTotal={}; cullingSamples={};
    auto& resources=ResourceManager::getInstance();
    auto mesh=resources.loadMesh("primitive:cube");
    auto shader=resources.loadShader("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
    const char* paths[]={"assets/textures/demo_checker.png","assets/textures/demo_panel.png",
        "assets/textures/stone.dds","assets/textures/WoodCrate02.dds"};
    std::shared_ptr<Resource<TextureData>> textures[4];
    for (int i=0;i<4;++i) textures[i]=resources.loadTexture(paths[i]);
    for (int i=0;i<count;++i) {
        const unsigned int index=cullingSerial++;
        const unsigned int cell=(index*7919u)%10000u;
        Entity e=world.createEntity();
        world.addComponent<Tag>(e).name="Cull cube "+std::to_string(index+1);
        auto& t=world.addComponent<Transform>(e);
        t.position={float(int(cell%100)-50)*4.f,float(int(cell/100)-50)*4.f,float(index%5)*1.8f+float(index/10000)*15.f};
        t.scale={.8f,.8f,.8f};
        auto& mr=world.addComponent<MeshRenderer>(e);
        mr.meshId="primitive:cube";
        mr.shaderId=ResourceManager::makeShaderKey("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
        mr.cachedMesh=mesh; mr.cachedShader=shader;
        mr.baseColorTextureId=paths[index%4]; mr.cachedBaseColorTexture=textures[index%4];
        Entity parent=cullingGroups[(index/100)%cullingGroups.size()];
        if (world.isAlive(parent)) {
            world.addComponent<Hierarchy>(e).parent=parent;
            world.getComponent<Hierarchy>(parent).children.push_back(e);
        }
        cullingObjects.push_back(e);
    }
}

void EditorContext::removeCullingObjects(int count) {
    cullingCpuTotal={}; cullingFrameTotal={}; cullingSamples={};
    for (int i=0;i<count && !cullingObjects.empty();++i) {
        Entity e=cullingObjects.back(); cullingObjects.pop_back();
        if (selected==e) clearSelection();
        world.destroyEntity(e);
    }
}
