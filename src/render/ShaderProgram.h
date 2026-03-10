#pragma once
#include <glad/glad.h>
#include <string>
#include "core/Logger.h"

// Loads shader sources from disk and compiles a GPU program.
class ShaderProgram {
public:
    // Load and compile the shader pair.
    bool load(const std::string& vertexPath, const std::string& fragmentPath);

    // Bind the program for drawing.
    void use() const;

    // Delete the GPU program.
    void destroy();

	GLuint getId() const { return id_; } // Expose the OpenGL program id.

private:
    GLuint id_ = 0; // OpenGL program id.

    // Read the full file into a string.
    std::string readFile(const std::string& path);

    // Compile a single shader stage.
    GLuint compileShader(GLenum type, const std::string& source);
};
