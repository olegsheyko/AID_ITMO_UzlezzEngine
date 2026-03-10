#pragma once
#include "states/IGameState.h"
#include "core/Logger.h"
#include "render/ShaderProgram.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// Main gameplay state.
class GameplayState : public IGameState {
public:
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render() override;

private:
	ShaderProgram shader_; // Shader used to draw the triangle.

	GLuint VAO_ = 0; // Vertex Array Object for the triangle.
	GLuint VBO_ = 0; // Vertex Buffer Object for the triangle.

    float x_ = 0.0f; // Horizontal triangle offset.
    float y_ = 0.0f;

	float centerX_ = 0.0f; // Triangle center on the X axis.
	float centerY_ = 0.0f; // Triangle center on the Y axis.

	float scale_ = 1.0f; // Current object scale.
	float rotation_ = 0.0f; // Current object rotation in radians.

	bool rmbWasPressed_ = false; // Edge detection for the right mouse button.
	bool lmbWasPressed_ = false; // Edge detection for the left mouse button.
	bool mmbWasPressed_ = false; // Edge detection for the middle mouse button.
};
