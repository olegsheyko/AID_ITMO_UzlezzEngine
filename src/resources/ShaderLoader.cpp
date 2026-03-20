#include "ShaderLoader.h"
#include "render/IRenderAdapter.h"
#include "core/Logger.h"

#include <fstream>
#include <sstream>
#include <glad/glad.h>

bool ShaderLoader::load(const std::string& vertexPath, const std::string& fragmentPath, 
                        ShaderData& shaderData, IRenderAdapter* renderer) {
    // Читаем исходники шейдеров
    if (!readFile(vertexPath, shaderData.vertexSource)) {
        LOG_ERROR("Failed to read vertex shader: " + vertexPath);
        return false;
    }
    LOG_INFO("Loaded vertex shader: " + vertexPath);

    if (!readFile(fragmentPath, shaderData.fragmentSource)) {
        LOG_ERROR("Failed to read fragment shader: " + fragmentPath);
        return false;
    }
    LOG_INFO("Loaded fragment shader: " + fragmentPath);

    bool result = compileAndLink(shaderData, renderer);
    if (result) {
        LOG_INFO("Shader program compiled successfully, ID=" + std::to_string(shaderData.programId));
    }
    return result;
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
    const char* vShaderCode = shaderData.vertexSource.c_str();
    const char* fShaderCode = shaderData.fragmentSource.c_str();

    // Компилируем вершинный шейдер
    uint32_t vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);
    if (!checkCompileErrors(vertex, "VERTEX")) {
        glDeleteShader(vertex);
        return false;
    }

    // Компилируем фрагментный шейдер
    uint32_t fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    if (!checkCompileErrors(fragment, "FRAGMENT")) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }

    // Линкуем программу
    shaderData.programId = glCreateProgram();
    glAttachShader(shaderData.programId, vertex);
    glAttachShader(shaderData.programId, fragment);
    glLinkProgram(shaderData.programId);
    
    if (!checkLinkErrors(shaderData.programId)) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        glDeleteProgram(shaderData.programId);
        return false;
    }

    // Удаляем шейдеры (они уже в программе)
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return true;
}

bool ShaderLoader::checkCompileErrors(uint32_t shader, const std::string& type) {
    int success;
    char infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        LOG_ERROR("Shader compilation error (" + type + "): " + infoLog);
        return false;
    }
    return true;
}

bool ShaderLoader::checkLinkErrors(uint32_t program) {
    int success;
    char infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        LOG_ERROR("Shader linking error: " + std::string(infoLog));
        return false;
    }
    return true;
}
