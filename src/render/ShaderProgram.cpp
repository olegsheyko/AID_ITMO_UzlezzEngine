#include "render/ShaderProgram.h"
#include <fstream>
#include <sstream>

bool ShaderProgram::load(const std::string& vertexPath, const std::string& fragmentPath) {
    destroy();

    std::string vertSrc = readFile(vertexPath);
    std::string fragSrc = readFile(fragmentPath);

    if (vertSrc.empty() || fragSrc.empty()) {
        LOG_ERROR("ShaderProgram: failed to read shader files");
        return false;
    }

    // Compile both shader stages first.
    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    if (!vert || !frag) {
        if (vert) {
            glDeleteShader(vert);
        }
        if (frag) {
            glDeleteShader(frag);
        }
        return false;
    }

    // Link them into a single GPU program.
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    // Check the linker output before keeping the program.
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        LOG_ERROR("ShaderProgram: link error: " + std::string(log));
        glDeleteProgram(program);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    // Individual shader objects are no longer needed after linking.
    glDeleteShader(vert);
    glDeleteShader(frag);

    id_ = program;

    LOG_INFO("ShaderProgram: compiled OK");
    return true;
}

void ShaderProgram::use() const {
    if (id_ != 0) {
        glUseProgram(id_);
    }
}

void ShaderProgram::destroy() {
    if (id_ != 0) {
        glDeleteProgram(id_);
        id_ = 0;
    }
}

std::string ShaderProgram::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("ShaderProgram: cannot open file: " + path);
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint ShaderProgram::compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // Surface compiler diagnostics immediately.
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        LOG_ERROR("ShaderProgram: compile error: " + std::string(log));
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
