#include "ecs/Components.h"
#include "ecs/RenderSystem.h"
#include "ecs/World.h"
#include "math/CameraMath.h"
#include "render/OpenGLRenderAdapter.h"
#include "resources/ResourceManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

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

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cout << "SKIP: GLFW display unavailable\n";
        return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    OpenGLRenderAdapter renderer;
    auto& resources = ResourceManager::getInstance();
    try {
        require(argc == 2, "Expected fixture output directory");
        require(renderer.init(120, 80, "Texture resource tests"), "OpenGL renderer failed to initialize");
        require(glfwGetWindowAttrib(renderer.getWindow(), GLFW_VISIBLE) == GLFW_FALSE, "Test window must be hidden");
        resources.init(&renderer);
        const std::filesystem::path directory(argv[1]);
        createFixtures(directory);
        testImages(resources, directory);
        testMaterialRendering(renderer, resources, directory);
        require(glGetError() == GL_NO_ERROR, "OpenGL error during texture tests");
        resources.clearCache();
        renderer.shutdown();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        resources.clearCache();
        renderer.shutdown();
        return 1;
    }
    return 0;
}
