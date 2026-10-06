#include "editor/AssetPreview.h"

#include "ecs/Components.h"
#include "ecs/RenderSystem.h"
#include "editor/EditorMath.h"
#include "editor/MeshBounds.h"
#include "math/CameraMath.h"
#include "render/IRenderAdapter.h"
#include "resources/ResourceManager.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace EditorMath;

namespace {
constexpr const char* kVertexShaderPath = "assets/shaders/mesh_vertex.glsl";
constexpr const char* kFragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";
constexpr const char* kSpherePath = "primitive:sphere";
constexpr float kFov = 30.0f;
constexpr int kThumbnailSize = 512;
// Вид 3/4 спереди-сверху, как у миниатюр UE5.
constexpr float kThumbnailYaw = -0.62f;
constexpr float kThumbnailPitch = -0.32f;

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool texturePending(const std::shared_ptr<Resource<TextureData>>& texture) {
    return texture && texture->isPending();
}
}

bool readMtl(const std::string& path, MtlInfo& outInfo) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    outInfo.materials.clear();
    const std::filesystem::path folder = std::filesystem::path(path).parent_path();
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;
        if (keyword == "newmtl") {
            MtlInfo::Entry entry;
            entry.name = trim(line.substr(6));
            outInfo.materials.push_back(entry);
        } else if (outInfo.materials.empty()) {
            continue;
        } else if (keyword == "Kd") {
            Vec3& color = outInfo.materials.back().color;
            stream >> color.x >> color.y >> color.z;
        } else if (keyword == "map_Kd") {
            // Имена файлов бывают с пробелами — берём остаток строки; опции вида "-s 1 1 1" не поддерживаем.
            std::string relative = trim(line.substr(6));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            if (!relative.empty() && relative[0] != '-') {
                const std::filesystem::path texture(relative);
                outInfo.materials.back().diffuseTexture =
                    (texture.is_absolute() ? texture : folder / texture).lexically_normal().generic_string();
            }
        }
    }
    return !outInfo.materials.empty();
}

AssetPreviewer::AssetPreviewer(IRenderAdapter& renderer)
    : renderer_(renderer) {
}

void AssetPreviewer::beginFrame() {
    thumbnailRenderedThisFrame_ = false;
}

void AssetPreviewer::startScene(const std::string& path, Scene& scene) {
    scene.path = path;
    ResourceManager& resources = ResourceManager::getInstance();
    const AssetType type = AssetDatabase::classify(path);
    if (type == AssetType::Model) {
        scene.mesh = resources.loadMeshAsync(path, JobPriority::Low);
    } else if (type == AssetType::Material) {
        MtlInfo info;
        if (readMtl(path, info)) {
            for (const MtlInfo::Entry& entry : info.materials) {
                if (!entry.diffuseTexture.empty()) {
                    scene.texture = resources.loadTextureAsync(entry.diffuseTexture, JobPriority::Low);
                    break;
                }
            }
        }
        scene.mesh = resources.loadMesh(kSpherePath);
    }
    scene.failed = scene.mesh == nullptr;
}

bool AssetPreviewer::buildScene(Scene& scene, bool animate) {
    if (scene.built || scene.failed) {
        return scene.built;
    }
    if (!scene.mesh || scene.mesh->isFailed()) {
        scene.failed = true;
        return false;
    }
    if (scene.mesh->isPending() || texturePending(scene.texture)) {
        return false;
    }
    const MeshData* data = scene.mesh->getData();
    // Ждём и текстуры материалов модели, иначе на превью останется заглушка.
    for (const SubMesh& subMesh : data->subMeshes) {
        if (texturePending(subMesh.material.cachedDiffuseTexture)) {
            return false;
        }
    }
    auto shader = ResourceManager::getInstance().loadShader(kVertexShaderPath, kFragmentShaderPath);
    Vec3 localMin{};
    Vec3 localMax{};
    if (!shader || !shader->isLoaded() || !MeshBounds::computeBindPose(*data, localMin, localMax)) {
        scene.failed = true;
        return false;
    }

    // Assimp отдаёт модели в Y-up, мир движка — Z-up: поворачивает сам меш.
    const bool yUpSource = MeshBounds::isImportedModel(scene.mesh->getPath());
    scene.bounds = transformBounds(MeshBounds::sourceBasis(yUpSource), localMin, localMax);

    scene.model = scene.world.createEntity();
    scene.world.addComponent<Transform>(scene.model, Transform{});
    MeshRenderer& meshRenderer = scene.world.addComponent<MeshRenderer>(scene.model);
    meshRenderer.yUpSource = yUpSource;
    meshRenderer.meshId = scene.mesh->getPath();
    meshRenderer.cachedMesh = scene.mesh;
    meshRenderer.cachedShader = shader;
    if (scene.texture && scene.texture->isLoaded()) {
        meshRenderer.baseColorTextureId = scene.texture->getPath();
        meshRenderer.cachedBaseColorTexture = scene.texture;
    }
    if (animate && !data->skeleton.clips.empty()) {
        scene.world.addComponent<Animator>(scene.model);
    }

    scene.camera = scene.world.createEntity();
    scene.world.addComponent<Transform>(scene.camera);
    Camera camera;
    camera.active = true;
    scene.world.addComponent<Camera>(scene.camera, camera);
    scene.built = true;
    return true;
}

void AssetPreviewer::renderScene(Scene& scene, int target, int width, int height, float yaw, float pitch, float zoom) {
    ZoneScopedN("Asset preview");
    // Кадрируем по наибольшей полуоси, а не по описанной сфере: модель занимает почти всю картинку.
    const AABB& bounds = scene.bounds;
    const float radius = std::max({bounds.halfSize.x, bounds.halfSize.y, bounds.halfSize.z, 0.0001f}) * 1.12f;
    const Vec3 forward = CameraMath::forward(pitch, yaw);
    const float distance = radius / std::tan(toRadians(kFov) * 0.5f) * std::max(zoom, 0.2f);
    const Vec3 eye = sub(bounds.center, mul(forward, distance));
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    Camera& camera = scene.world.getComponent<Camera>(scene.camera);
    camera.viewMatrix = CameraMath::view(eye, pitch, yaw);
    camera.projectionMatrix = Math::perspective(toRadians(kFov), aspect, distance * 0.02f, distance + radius * 4.0f);
    scene.world.getComponent<Transform>(scene.camera).position = eye;

    // Прозрачный фон цвета карточки: при уменьшении края смешиваются с ним, а не с чёрным.
    RenderSystem renderSystem(renderer_);
    renderer_.beginViewportFrame(target, width, height, 0.2f, 0.205f, 0.22f, 0.0f);
    renderSystem.render(scene.world);
    renderer_.endViewportFrame();
}

AssetThumbnail AssetPreviewer::thumbnail(const std::string& path) {
    std::unique_ptr<Thumbnail>& slot = thumbnails_[path];
    if (!slot) {
        slot = std::make_unique<Thumbnail>();
        startScene(path, slot->scene);
    }
    Thumbnail& entry = *slot;
    AssetThumbnail result;
    result.texture = entry.texture;
    result.failed = entry.failed;
    if (entry.texture != 0 || entry.failed) {
        return result;
    }
    if (!buildScene(entry.scene, false)) {
        entry.failed = entry.scene.failed;
        result.failed = entry.failed;
        result.loading = !entry.failed;
        return result;
    }
    if (thumbnailRenderedThisFrame_) {
        result.loading = true;
        return result;
    }
    thumbnailRenderedThisFrame_ = true;
    renderScene(entry.scene, kThumbnailTarget, kThumbnailSize, kThumbnailSize, kThumbnailYaw, kThumbnailPitch, 1.0f);
    entry.texture = renderer_.copyViewportTexture(kThumbnailTarget);
    entry.failed = entry.texture == 0;
    // Мир превью больше не нужен — миниатюра уже в текстуре.
    entry.scene.world.clear();
    entry.scene.mesh.reset();
    entry.scene.texture.reset();
    result.texture = entry.texture;
    result.failed = entry.failed;
    return result;
}

unsigned int AssetPreviewer::renderLive(const std::string& path, int width, int height, float yaw, float pitch, float zoom, float dt) {
    if (!live_ || live_->path != path) {
        live_ = std::make_unique<Scene>();
        startScene(path, *live_);
    }
    if (!buildScene(*live_, true)) {
        return 0;
    }
    liveAnimation_.update(live_->world, dt);
    renderScene(*live_, kAssetPreviewTarget, std::max(1, width), std::max(1, height), yaw, pitch, zoom);
    return renderer_.getViewportTextureId(kAssetPreviewTarget);
}

void AssetPreviewer::release() {
    for (auto& [path, entry] : thumbnails_) {
        (void)path;
        if (entry && entry->texture != 0) {
            renderer_.destroyTexture(entry->texture);
        }
    }
    thumbnails_.clear();
    live_.reset();
}
