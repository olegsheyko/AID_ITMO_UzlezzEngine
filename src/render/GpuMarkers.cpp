#include "render/GpuMarkers.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// GPU-зоны Tracy для OpenGL. На Apple Tracy сам их отключает (метки времени GL там ненадёжны),
// поэтому там остаётся только разметка для отладчиков. ENGINE_TRACY_GPU_CHECK нужен лишь для
// проверки сборки этой ветки на Mac.
#if defined(TRACY_ENABLE) && (!defined(__APPLE__) || defined(ENGINE_TRACY_GPU_CHECK))
#define ENGINE_TRACY_GPU 1
#include <tracy/TracyOpenGL.hpp>
#endif

namespace {
// glad сгенерирован под OpenGL 3.3 без расширений, поэтому функции разметки загружаем сами.
using PushGroupFn = void (APIENTRYP)(GLenum source, GLuint id, GLsizei length, const GLchar* message);
using PopGroupFn = void (APIENTRYP)();
using ObjectLabelFn = void (APIENTRYP)(GLenum identifier, GLuint name, GLsizei length, const GLchar* label);
using PushMarkerExtFn = void (APIENTRYP)(GLsizei length, const GLchar* marker);
using PopMarkerExtFn = void (APIENTRYP)();
using LabelObjectExtFn = void (APIENTRYP)(GLenum type, GLuint object, GLsizei length, const GLchar* label);
constexpr GLenum kDebugSourceApplication = 0x824A; // GL_DEBUG_SOURCE_APPLICATION

bool hasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension != nullptr && std::strcmp(extension, name) == 0) {
            return true;
        }
    }
    return false;
}

// В core-профиле функции KHR_debug называются без суффикса; на части драйверов есть только вариант с KHR.
template <typename Fn>
Fn loadFunction(const char* name, const char* fallback = nullptr) {
    GLFWglproc proc = glfwGetProcAddress(name);
    if (proc == nullptr && fallback != nullptr) {
        proc = glfwGetProcAddress(fallback);
    }
    return reinterpret_cast<Fn>(proc);
}
} // namespace

struct GpuMarkers::Impl {
    PushGroupFn pushGroup = nullptr;
    PopGroupFn popGroup = nullptr;
    ObjectLabelFn objectLabel = nullptr;
    PushMarkerExtFn pushMarker = nullptr;
    PopMarkerExtFn popMarker = nullptr;
    LabelObjectExtFn labelObjectExt = nullptr;
    const char* backend = "none";
    bool tracyGpu = false;
    int depth = 0;
#ifdef ENGINE_TRACY_GPU
    // Каждая открытая группа держит свою GPU-зону Tracy; закрытие группы закрывает зону.
    std::vector<std::unique_ptr<tracy::GpuCtxScope>> zones;
#endif
};

GpuMarkers::GpuMarkers() : impl_(std::make_unique<Impl>()) {}
GpuMarkers::~GpuMarkers() = default;

void GpuMarkers::init() {
    auto& p = *impl_;
    // KHR_debug: есть у NVIDIA, AMD, Intel на Windows, и RenderDoc сам его предоставляет.
    if (hasExtension("GL_KHR_debug")) {
        p.pushGroup = loadFunction<PushGroupFn>("glPushDebugGroup", "glPushDebugGroupKHR");
        p.popGroup = loadFunction<PopGroupFn>("glPopDebugGroup", "glPopDebugGroupKHR");
        p.objectLabel = loadFunction<ObjectLabelFn>("glObjectLabel", "glObjectLabelKHR");
        if (p.pushGroup == nullptr || p.popGroup == nullptr) {
            p.pushGroup = nullptr;
            p.popGroup = nullptr;
        }
    }
    // Запасной путь: старые расширения маркеров и имён (их, например, даёт OpenGL на macOS).
    if (p.pushGroup == nullptr && hasExtension("GL_EXT_debug_marker")) {
        p.pushMarker = loadFunction<PushMarkerExtFn>("glPushGroupMarkerEXT");
        p.popMarker = loadFunction<PopMarkerExtFn>("glPopGroupMarkerEXT");
        if (p.pushMarker == nullptr || p.popMarker == nullptr) {
            p.pushMarker = nullptr;
            p.popMarker = nullptr;
        }
    }
    if (p.objectLabel == nullptr && hasExtension("GL_EXT_debug_label")) {
        p.labelObjectExt = loadFunction<LabelObjectExtFn>("glLabelObjectEXT");
    }
    p.backend = p.pushGroup != nullptr ? "KHR_debug" : (p.pushMarker != nullptr ? "EXT_debug_marker" : "none");
#ifdef ENGINE_TRACY_GPU
    // Контекст GPU-зон создаётся один раз на процесс: Tracy держит его в глобальном состоянии.
    static bool tracyContextCreated = false;
    if (!tracyContextCreated) {
        TracyGpuContext;
        TracyGpuContextName("OpenGL", 6);
        tracyContextCreated = true;
    }
    p.tracyGpu = true;
#endif
}

void GpuMarkers::push(const char* name) {
    auto& p = *impl_;
    if (p.pushGroup != nullptr) {
        p.pushGroup(kDebugSourceApplication, 0, -1, name);
    } else if (p.pushMarker != nullptr) {
        p.pushMarker(0, name); // длина 0: строка заканчивается нулём
    }
#ifdef ENGINE_TRACY_GPU
    if (p.tracyGpu) {
        // То же, что макрос TracyGpuZoneTransient, но зона живёт до pop(), а не до конца блока.
#ifdef TRACY_CALLSTACK
        p.zones.push_back(std::make_unique<tracy::GpuCtxScope>(TracyLine, TracyFile, std::strlen(TracyFile), TracyFunction,
            std::strlen(TracyFunction), name, std::strlen(name), TRACY_CALLSTACK, true));
#else
        p.zones.push_back(std::make_unique<tracy::GpuCtxScope>(TracyLine, TracyFile, std::strlen(TracyFile), TracyFunction,
            std::strlen(TracyFunction), name, std::strlen(name), true));
#endif
    }
#endif
    ++p.depth;
}

void GpuMarkers::pop() {
    auto& p = *impl_;
    if (p.depth == 0) {
        return;
    }
    --p.depth;
#ifdef ENGINE_TRACY_GPU
    if (!p.zones.empty()) {
        p.zones.pop_back();
    }
#endif
    if (p.popGroup != nullptr) {
        p.popGroup();
    } else if (p.popMarker != nullptr) {
        p.popMarker();
    }
}

void GpuMarkers::label(unsigned int khrIdentifier, unsigned int extType, unsigned int id, const char* name) {
    auto& p = *impl_;
    if (id == 0 || name == nullptr) {
        return;
    }
    if (p.objectLabel != nullptr) {
        p.objectLabel(khrIdentifier, id, -1, name);
    } else if (p.labelObjectExt != nullptr) {
        p.labelObjectExt(extType, id, 0, name);
    }
}

void GpuMarkers::collect() {
#ifdef ENGINE_TRACY_GPU
    if (impl_->tracyGpu) {
        TracyGpuCollect;
    }
#endif
}

bool GpuMarkers::active() const {
    return impl_->pushGroup != nullptr || impl_->pushMarker != nullptr || impl_->tracyGpu;
}

const char* GpuMarkers::backend() const {
    return impl_->backend;
}
