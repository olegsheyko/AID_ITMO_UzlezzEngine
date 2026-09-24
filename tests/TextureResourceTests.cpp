#include "ecs/Components.h"
#include "ecs/RenderSystem.h"
#include "ecs/World.h"
#include "math/CameraMath.h"
#include "render/OpenGLRenderAdapter.h"
#include "resources/ResourceManager.h"
#include "AnimationFixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <size_t N>
void writeBytes(const std::filesystem::path& path, const unsigned char (&data)[N]) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data), N);
    require(stream.good(), "Cannot write texture fixture");
}

void createFixtures(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    // RGB PNG: top row red/green/blue, bottom row yellow/cyan/magenta.
    const unsigned char rgb[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,3,0,0,0,2,8,2,0,0,0,18,22,241,77,0,0,0,21,73,68,65,84,120,156,99,248,207,192,192,0,193,255,129,212,127,32,241,31,0,74,201,8,248,238,104,64,182,0,0,0,0,73,69,78,68,174,66,96,130};
    const unsigned char grayAlpha[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,156,99,168,112,0,0,1,51,0,185,47,93,67,100,0,0,0,0,73,69,78,68,174,66,96,130};
    writeBytes(directory / "colors.png", rgb);
    writeBytes(directory / "alpha.png", grayAlpha);
    std::ofstream(directory / "invalid.png") << "not an image";
    std::ofstream(directory / "quad.mtl") << "newmtl Test\nKd 0 1 0\nmap_Kd ./colors.png\n";
    std::ofstream(directory / "quad.obj") <<
        "mtllib quad.mtl\nusemtl Test\n"
        "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 -1 0\nf 1/1/1 2/2/1 3/3/1 4/4/1\n";
}

std::vector<unsigned char> texturePixels(const TextureData& texture) {
    std::vector<unsigned char> pixels(static_cast<size_t>(texture.width) * texture.height * 4);
    glBindTexture(GL_TEXTURE_2D, texture.textureId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

std::array<unsigned char, 4> framePixel(int x, int y) {
    std::array<unsigned char, 4> pixel{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return pixel;
}

void testImages(ResourceManager& resources, const std::filesystem::path& directory) {
    const auto paths = resources.getAvailableTexturePaths();
    require(std::find(paths.begin(), paths.end(), "assets/textures/demo_checker.png") != paths.end(),
        "Inspector does not discover unloaded PNG files");
    require(resources.getTextureCount() == 0, "Discovery should not load images");
    auto rgb = resources.load<TextureData>((directory / "colors.png").generic_string());
    require(rgb && rgb->isLoaded(), "PNG failed to load");
    const auto& data = *rgb->getData();
    require(data.width == 3 && data.height == 2 && data.channels == 4, "Wrong PNG dimensions/channels (stub?)");
    const std::vector<unsigned char> expected = {
        255,255,0,255, 0,255,255,255, 255,0,255,255,
        255,0,0,255, 0,255,0,255, 0,0,255,255
    };
    require(texturePixels(data) == expected, "PNG pixels, row orientation or upload alignment incorrect");
    require(resources.load<TextureData>((directory / "colors.png").generic_string()) == rgb, "Texture cache misses same path");

    auto alpha = resources.load<TextureData>((directory / "alpha.png").generic_string());
    require(alpha && texturePixels(*alpha->getData()) == std::vector<unsigned char>{120,120,120,64},
        "Grayscale-alpha PNG was not expanded correctly");
    require(!resources.load<TextureData>((directory / "invalid.png").generic_string()), "Corrupt image accepted");
    require(!resources.load<TextureData>((directory / "missing.png").generic_string()), "Missing image accepted");

    int existingImages = 0;
    for (const auto& path : paths) {
        if (std::filesystem::path(path).extension() != ".png" && std::filesystem::path(path).extension() != ".dds") continue;
        auto texture = resources.load<TextureData>(path);
        require(texture && texture->getData()->width > 2 && texture->getData()->height > 2,
            "Project PNG/DDS failed to decode at real resolution");
        ++existingImages;
    }
    require(existingImages >= 17, "Expected project textures were not discovered");
    std::cout << "PASS: PNG pixels, alpha, orientation, errors, cache and " << existingImages << " project PNG/DDS files\n";
}

void testMaterialRendering(OpenGLRenderAdapter& renderer, ResourceManager& resources, const std::filesystem::path& directory) {
    auto mesh = resources.load<MeshData>((directory / "quad.obj").generic_string());
    require(mesh && mesh->getData()->subMeshes.size() == 1, "OBJ fixture failed to load");
    auto& material = mesh->getData()->subMeshes.front().material;
    require(material.diffuseTexturePath == (directory / "colors.png").lexically_normal().generic_string(),
        "MTL relative path was not resolved");
    require(material.cachedDiffuseTexture && material.cachedDiffuseTexture->isLoaded(), "Material texture was not loaded with mesh");
    glBindTexture(GL_TEXTURE_2D, material.cachedDiffuseTexture->getData()->textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    auto shader = resources.loadShader("assets/shaders/mesh_vertex.glsl", "assets/shaders/mesh_fragment_simple_texture.glsl");
    require(shader != nullptr, "Texture shader failed to compile");

    World world;
    const Entity camera = world.createEntity();
    world.addComponent<Transform>(camera);
    world.addComponent<Camera>(camera).viewMatrix = CameraMath::view(Vec3{}, 0.0f, 0.0f);
    const Entity entity = world.createEntity();
    world.addComponent<Transform>(entity);
    auto& meshRenderer = world.addComponent<MeshRenderer>(entity);
    meshRenderer.cachedMesh = mesh;
    meshRenderer.cachedShader = shader;
    RenderSystem renderSystem(renderer);
    auto draw = [&] {
        renderer.beginViewportFrame(120, 80, 0.0f, 0.0f, 0.0f);
        glDisable(GL_BLEND);
        renderSystem.render(world);
    };
    draw();
    const auto bottomLeft = framePixel(20, 20);
    const auto topRight = framePixel(100, 60);
    require(bottomLeft[0] > 100 && bottomLeft[1] > 100 && bottomLeft[2] < 10, "Material bottom-left should be yellow");
    require(topRight[0] < 10 && topRight[1] < 10 && topRight[2] > 100, "Material top-right should be blue");
    renderer.endViewportFrame();

    meshRenderer.cachedBaseColorTexture = resources.load<TextureData>((directory / "alpha.png").generic_string());
    draw();
    const auto overridePixel = framePixel(20, 20);
    // The warm directional light tints grayscale: approximately RGB (72, 69, 64).
    require(std::abs(static_cast<int>(overridePixel[0]) - 72) <= 2 &&
        std::abs(static_cast<int>(overridePixel[1]) - 69) <= 2 &&
        std::abs(static_cast<int>(overridePixel[2]) - 64) <= 2 && overridePixel[3] == 64,
        "Explicit texture override or alpha was ignored");
    renderer.endViewportFrame();

    meshRenderer.cachedBaseColorTexture.reset();
    const auto savedTexture = material.cachedDiffuseTexture;
    material.cachedDiffuseTexture.reset();
    draw();
    const auto fallback = framePixel(20, 20);
    require(fallback[0] < 10 && fallback[1] > 100 && fallback[2] < 10, "Missing texture did not fall back to material color");
    renderer.endViewportFrame();
    material.cachedDiffuseTexture = savedTexture;
    std::cout << "PASS: OBJ/MTL texture resolution, actual rendered colors, override and missing-texture fallback\n";
}
}

// Exercise the real vertex shader and uploaded integer bone IDs/weights on the GPU.
// Transform feedback compares shader output with an analytical weighted translation.
void testGpuSkinning(OpenGLRenderAdapter& renderer, ResourceManager& resources, const std::filesystem::path& directory) {
    auto mesh = resources.load<MeshData>(writeAnimationFixture(directory / "skin").generic_string());
    require(mesh && mesh->isLoaded(), "Skinned glTF GPU upload failed");
    auto shader = resources.loadShader("assets/shaders/mesh_vertex.glsl", "assets/shaders/mesh_fragment_simple_texture.glsl");
    require(shader && shader->isLoaded(), "Skin shader failed to compile");
    const GLuint program=shader->getData()->programId;
    const char* varyings[]={"FragPos","Normal"};
    glTransformFeedbackVaryings(program,2,varyings,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);
    GLint linked=0; glGetProgramiv(program,GL_LINK_STATUS,&linked);
    require(linked==GL_TRUE,"Skin shader transform feedback failed to link");
    renderer.useShaderProgram(program);
    for (const char* name : {"model","view","projection","meshNodeTransform"}) renderer.setMatrix4(program,name,Mat4::identity());
    AnimationPose pose; Animation::preparePose(*mesh->getData(),pose); Animation::evaluate(*mesh->getData(),0,1,pose);
    renderer.setSkinMatrices(program,pose.palettes[0].data(),pose.palettes[0].size());
    const auto& sub=mesh->getData()->subMeshes[0];
    require(sub.indices.size()==3,"Skin fixture topology changed");
    GLuint buffer=0; glGenBuffers(1,&buffer);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,18*sizeof(float),nullptr,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
    glEnable(GL_RASTERIZER_DISCARD);
    for (int skin : {1,0}) {
        renderer.setInt(program,"useSkinning",skin);
        glBeginTransformFeedback(GL_TRIANGLES);
        renderer.drawIndexed(sub.vao,sub.indexCount);
        glEndTransformFeedback();
        std::array<float,18> output{};
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(output),output.data());
        for (size_t i=0;i<3;++i) {
            const auto& vertex=sub.vertices[sub.indices[i]];
            require(std::abs(output[6*i]-vertex.position.x-(skin ? .75f : 0))<1e-5f,"GPU skinning weighted X position incorrect");
            require(std::abs(output[6*i+1]-vertex.position.y)<1e-5f && std::abs(output[6*i+2]-vertex.position.z)<1e-5f,"GPU skinning Y/Z position incorrect");
            require(std::abs(output[6*i+3])<1e-5f && std::abs(output[6*i+4]+1)<1e-5f && std::abs(output[6*i+5])<1e-5f,"GPU skinned normal incorrect");
        }
    }
    glDisable(GL_RASTERIZER_DISCARD);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);
    glDeleteBuffers(1,&buffer);
    renderer.useShaderProgram(0);
    std::cout << "PASS: actual GPU skinning, integer attributes, palette upload, normals and static fallback\n";
}

void testWalkingModel(OpenGLRenderAdapter& renderer, ResourceManager& resources,
    const std::filesystem::path& directory, const std::string& path) {
    auto mesh=resources.load<MeshData>(path);
    require(mesh && mesh->isLoaded(),"Walking FBX failed GPU upload");
    const auto& data=*mesh->getData();
    require(!data.skeleton.clips.empty(),"Walking clip missing");
    auto shader=resources.loadShader("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
    const GLuint program=shader->getData()->programId;
    World world;
    Entity camera=world.createEntity(); world.addComponent<Transform>(camera);
    auto& cameraData=world.addComponent<Camera>(camera);
    // Leave room for the forward travel authored in this walking clip.
    cameraData.viewMatrix=CameraMath::view({3.5f,-7,2.5f},-.24f,.610726f);
    cameraData.projectionMatrix=Math::perspective(.785398f,1,.1f,100);
    Entity character=world.createEntity();
    auto& transform=world.addComponent<Transform>(character);
    transform.rotation.x=1.57079632679f;
    transform.scale={2.0f/181,2.0f/181,2.0f/181};
    const Mat4 model=Math::composeTransform(transform.position,transform.rotation,transform.scale);
    auto& mr=world.addComponent<MeshRenderer>(character); mr.cachedMesh=mesh; mr.cachedShader=shader;
    auto& animator=world.addComponent<Animator>(character); animator.evaluatedMesh=&data;
    Animation::preparePose(data,animator.pose);
    RenderSystem render(renderer);
    GLuint buffer=0; glGenBuffers(1,&buffer);
    std::vector<unsigned char> firstImage;
    for (int phase=0;phase<4;++phase) {
        Animation::evaluate(data,0,data.skeleton.clips[0].duration*phase/4,animator.pose);
        // Compare every transformed FBX triangle vertex against the CPU skinning equation.
        renderer.useShaderProgram(program);
        renderer.setMatrix4(program,"model",model);
        renderer.setMatrix4(program,"view",cameraData.viewMatrix);
        renderer.setMatrix4(program,"projection",cameraData.projectionMatrix);
        glEnable(GL_RASTERIZER_DISCARD);
        for (size_t s=0;s<data.subMeshes.size();++s) {
            const auto& sub=data.subMeshes[s];
            const Mat4 node=Math::multiply(data.skeleton.rootInverse,animator.pose.globals[sub.skeletonNode]);
            renderer.setMatrix4(program,"meshNodeTransform",node);
            renderer.setInt(program,"useSkinning",sub.bones.empty() ? 0 : 1);
            renderer.setSkinMatrices(program,animator.pose.palettes[s].data(),animator.pose.palettes[s].size());
            std::vector<float> output(sub.indices.size()*6);
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
            glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,output.size()*sizeof(float),nullptr,GL_STREAM_READ);
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
            glBeginTransformFeedback(GL_TRIANGLES);
            renderer.drawIndexed(sub.vao,sub.indexCount);
            glEndTransformFeedback();
            glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,output.size()*sizeof(float),output.data());
            for (size_t i=0;i<sub.indices.size();++i) {
                const auto& vertex=sub.vertices[sub.indices[i]];
                Mat4 skin=node; float total=0; for (float w:vertex.boneWeights) total+=w;
                if (!sub.bones.empty() && total>0) {
                    skin={};
                    for (size_t b=0;b<4;++b) for (size_t k=0;k<16;++k)
                        skin.values[k]+=animator.pose.palettes[s][vertex.boneIds[b]].values[k]*vertex.boneWeights[b];
                }
                const Mat4 matrix=Math::multiply(model,skin);
                for (size_t k=0;k<3;++k) {
                    const float expected=matrix.values[k]*vertex.position.x+matrix.values[k+4]*vertex.position.y+matrix.values[k+8]*vertex.position.z+matrix.values[k+12];
                    require(std::isfinite(output[i*6+k]) && std::abs(output[i*6+k]-expected)<.0001f,"FBX GPU position differs from CPU skinning");
                    require(std::isfinite(output[i*6+k+3]),"Non-finite FBX GPU normal");
                }
            }
        }
        glDisable(GL_RASTERIZER_DISCARD);
        renderer.beginViewportFrame(640,640,.06f,.07f,.09f);
        render.render(world);
        std::vector<unsigned char> pixels(640*640*3);
        glReadPixels(0,0,640,640,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream ppm(directory/("Walking_phase_"+std::to_string(phase)+".ppm"),std::ios::binary);
        ppm << "P6\n640 640\n255\n";
        for (int row=639;row>=0;--row) ppm.write(reinterpret_cast<const char*>(pixels.data()+row*640*3),640*3);
        require(ppm.good(),"Cannot save walking preview");
        size_t visible=0; for (size_t i=0;i<pixels.size();i+=3) if (pixels[i]>45 || pixels[i+1]>45 || pixels[i+2]>45) ++visible;
        require(visible>1000,"Walking model is not visible in rendered preview");
        if (phase==0) firstImage=pixels;
        else require(pixels!=firstImage,"Rendered walking poses are identical");
        renderer.endViewportFrame();
    }
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0); glDeleteBuffers(1,&buffer);
    std::cout << "PASS: Walking.fbx full GPU/CPU vertex comparison and four rendered poses\n";
}

void testAsyncResources(OpenGLRenderAdapter& renderer, ResourceManager& resources, const std::filesystem::path& directory, const char* walking) {
    resources.clearCache();
    auto& jobs=JobSystem::getInstance();
    require(jobs.init({4}), "Cannot start resource workers");
    resources.setUploadBudget(4,1);
    auto mesh=resources.loadMeshAsync((directory/"quad.obj").generic_string());
    require(mesh==resources.loadMeshAsync((directory/"quad.obj").generic_string()),"Async mesh requests must share a handle");
    require(mesh->isPending(),"Mesh became ready before main-thread pump");
    auto scene=resources.loadSceneAsync("assets/scenes/demo_scene.json");
    std::ofstream(directory/"broken.json") << "{bad json";
    auto broken=resources.loadSceneAsync((directory/"broken.json").generic_string());
    auto missing=resources.loadMeshAsync((directory/"missing.fbx").generic_string());
    auto shader=resources.loadShader("assets/shaders/mesh_vertex.glsl","assets/shaders/mesh_fragment_simple_texture.glsl");
    World world;
    auto camera=world.createEntity(); world.addComponent<Transform>(camera);
    world.addComponent<Camera>(camera).viewMatrix=CameraMath::view(Vec3{},0,0);
    auto entity=world.createEntity(); world.addComponent<Transform>(entity);
    auto& mr=world.addComponent<MeshRenderer>(entity); mr.cachedMesh=mesh; mr.cachedShader=shader;
    RenderSystem render(renderer);
    renderer.beginViewportFrame(120,80,0,0,0);
    render.render(world);
    auto placeholder=framePixel(60,40);
    require(placeholder[0]>20 && placeholder[1]>20 && placeholder[2]>20,"Pending mesh placeholder is invisible");
    renderer.endViewportFrame();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while (resources.pendingLoadCount() && std::chrono::steady_clock::now()<deadline) {
        resources.pumpUploads(); jobs.collectCompleted(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(resources.pendingLoadCount()==0,"Async failures left pending loads");
    require(mesh->isLoaded() && scene->isLoaded(),"Async mesh/scene did not load");
    require(!scene->getData()->getEntities().empty(),"Async scene has no entities");
    require(broken->isFailed() && missing->isFailed(),"Broken resources must settle as Failed");
    auto texture=mesh->getData()->subMeshes[0].material.cachedDiffuseTexture;
    require(texture && texture->isLoaded(),"Async material texture was not loaded");
    // Explicit pending texture takes precedence over a ready material texture.
    mr.cachedBaseColorTexture=std::make_shared<Resource<TextureData>>("pending test override");
    renderer.beginViewportFrame(120,80,0,0,0); render.render(world);
    auto overridePixel=framePixel(20,20);
    require(overridePixel[2]>20,"Pending explicit texture fell back to material image");
    renderer.endViewportFrame();
    std::shared_ptr<Resource<MeshData>> animated;
    if (walking) {
        animated=resources.loadMeshAsync(walking);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while (animated->getState()!=ResourceState::ReadyForUpload && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(animated->getState()==ResourceState::ReadyForUpload,"FBX decode did not finish");
        require(animated->getData()->subMeshes.size()>1,"Expected multi-submesh FBX");
        resources.pumpUploads();
        require(animated->isPending(),"Upload count budget was ignored");
        require(animated->getData()->subMeshes[0].vao!=0 && animated->getData()->subMeshes[1].vao==0,"GPU upload is not incremental");
    }
    // Exit with both partially uploaded data and newly submitted CPU work.
    std::filesystem::copy_file(directory/"quad.obj",directory/"shutdown.obj",std::filesystem::copy_options::overwrite_existing);
    auto exiting=resources.loadMeshAsync((directory/"shutdown.obj").generic_string());
    GLuint partial=animated ? animated->getData()->subMeshes[0].vao : 0;
    resources.beginShutdown(); jobs.shutdown(); resources.clearCache();
    require(exiting->isFailed() && (!animated || animated->isFailed()),"Shutdown did not cancel pending mesh handles");
    require(resources.pendingLoadCount()==0,"Shutdown leaked pending resources");
    require(!partial || !glIsVertexArray(partial),"Shutdown leaked partially uploaded mesh");
    require(!resources.loadMeshAsync("primitive:cube"),"Shutdown accepted another mesh");
    resources.init(&renderer);
    resources.setUploadBudget(4,8);
    std::cout << "PASS: async mesh/scene/material, placeholders, failures, incremental GPU budget and shutdown\n";
}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cout << "SKIP: GLFW display unavailable\n";
        return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    OpenGLRenderAdapter renderer;
    auto& resources = ResourceManager::getInstance();
    try {
        require(argc == 2 || argc == 3, "Expected fixture output directory [animated model]");
        require(renderer.init(120, 80, "Texture resource tests"), "OpenGL renderer failed to initialize");
        require(glfwGetWindowAttrib(renderer.getWindow(), GLFW_VISIBLE) == GLFW_FALSE, "Test window must be hidden");
        resources.init(&renderer);
        const std::filesystem::path directory(argv[1]);
        createFixtures(directory);
        testImages(resources, directory);
        testMaterialRendering(renderer, resources, directory);
        testGpuSkinning(renderer, resources, directory);
        if (argc==3) testWalkingModel(renderer,resources,directory,argv[2]);
        testAsyncResources(renderer,resources,directory,argc==3 ? argv[2] : nullptr);
        require(glGetError() == GL_NO_ERROR, "OpenGL error during texture tests");
        resources.clearCache();
        renderer.shutdown();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        resources.beginShutdown();
        JobSystem::getInstance().shutdown();
        resources.clearCache();
        renderer.shutdown();
        return 1;
    }
    return 0;
}
