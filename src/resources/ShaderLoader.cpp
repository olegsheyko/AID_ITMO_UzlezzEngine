#include "ShaderLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <fstream>
#include <sstream>

bool ShaderLoader::load(const std::string& vertexPath, const std::string& fragmentPath, ShaderData& shaderData, IRenderAdapter* renderer) {
    shaderData.vertexPath = vertexPath;
    shaderData.fragmentPath = fragmentPath;

    if (!readFile(vertexPath, shaderData.vertexSource)) {
        LOG_ERROR("Failed to read vertex shader: " + vertexPath);
        return false;
    }

    if (!readFile(fragmentPath, shaderData.fragmentSource)) {
        LOG_ERROR("Failed to read fragment shader: " + fragmentPath);
        return false;
    }

    return compileAndLink(shaderData, renderer);
}

bool ShaderLoader::readFile(const std::string& path, std::string& content) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

bool ShaderLoader::compileAndLink(ShaderData& shaderData, IRenderAdapter* renderer) {
    if (renderer == nullptr) {
        return false;
    }

    std::string error;
    if (!renderer->createShaderProgram(
            shaderData.vertexSource,
            shaderData.fragmentSource,
            shaderData.programId,
            error)) {
        if (!error.empty()) {
            LOG_ERROR("Shader compilation/linking error: " + error);
        }
        return false;
    }

    LOG_INFO("Shader program compiled successfully, ID=" + std::to_string(shaderData.programId));
    return true;
}
