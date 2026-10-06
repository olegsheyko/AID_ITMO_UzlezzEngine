#pragma once

#include "ecs/AnimationSystem.h"
#include "ecs/CollisionUtils.h"
#include "ecs/Entity.h"
#include "ecs/World.h"
#include "editor/AssetDatabase.h"
#include "resources/Resource.h"
#include "resources/ResourceTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IRenderAdapter;

// Цели рендера превью у IRenderAdapter (0 и 1 заняты Scene и Game View).
constexpr int kThumbnailTarget = 2;
constexpr int kAssetPreviewTarget = 3;

struct AssetThumbnail {
    unsigned int texture = 0;
    bool loading = false;
    bool failed = false;
};

// Материал из .mtl: имена и первая диффузная текстура — для превью-шара и перетаскивания на объект.
struct MtlInfo {
    struct Entry {
        std::string name;
        Vec3 color{1.0f, 1.0f, 1.0f};
        std::string diffuseTexture;  // путь от рабочей папки, пусто — без текстуры
    };
    std::vector<Entry> materials;
};
bool readMtl(const std::string& path, MtlInfo& outInfo);

// Рендеры ассетов, как в Content Browser UE5: модели — в позе 3/4, материалы — шаром.
// Миниатюры рисуются один раз в текстуру; живой вид для инспектора — каждый кадр с орбитой и анимацией.
class AssetPreviewer {
public:
    explicit AssetPreviewer(IRenderAdapter& renderer);

    static bool supports(AssetType type) { return type == AssetType::Model || type == AssetType::Material; }

    // Не больше одной новой миниатюры за кадр, чтобы листание папки не дёргало FPS.
    void beginFrame();
    AssetThumbnail thumbnail(const std::string& path);
    // Живой вид в цель kAssetPreviewTarget; 0 — ещё грузится. yaw/pitch — орбита, zoom — множитель дистанции.
    unsigned int renderLive(const std::string& path, int width, int height, float yaw, float pitch, float zoom, float dt);
    // Освобождает GPU-текстуры миниатюр. Вызывать, пока жив контекст OpenGL.
    void release();

private:
    struct Scene {
        std::string path;
        World world;
        Entity model = kInvalidEntity;
        Entity camera = kInvalidEntity;
        AABB bounds{};
        std::shared_ptr<Resource<MeshData>> mesh;
        std::shared_ptr<Resource<TextureData>> texture;
        bool built = false;
        bool failed = false;
    };

    struct Thumbnail {
        Scene scene;
        unsigned int texture = 0;
        bool failed = false;
    };

    void startScene(const std::string& path, Scene& scene);
    // true — сцена собрана и её можно рисовать; пока ресурсы грузятся — false.
    bool buildScene(Scene& scene, bool animate);
    void renderScene(Scene& scene, int target, int width, int height, float yaw, float pitch, float zoom);

    IRenderAdapter& renderer_;
    std::unordered_map<std::string, std::unique_ptr<Thumbnail>> thumbnails_;
    std::unique_ptr<Scene> live_;
    AnimationSystem liveAnimation_;
    bool thumbnailRenderedThisFrame_ = false;
};
