#include "states/GameplayState.h"
#include <GLFW/glfw3.h>
#include <cmath>

void GameplayState::onEnter() {
    LOG_INFO("GameplayState: entered");

    if (!shader_.load("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl")) {
        LOG_ERROR("GameplayState: failed to load shaders");
        return;
    }

    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  // Bottom-left vertex.
         0.5f, -0.5f, 0.0f,  // Bottom-right vertex.
         0.0f,  0.5f, 0.0f   // Top-center vertex.
    };

	centerX_ = (vertices[0] + vertices[3] + vertices[6]) / 3.0f; // Average X center.
	centerY_ = (vertices[1] + vertices[4] + vertices[7]) / 3.0f; // Average Y center.

	glGenVertexArrays(1, &VAO_);
	glGenBuffers(1, &VBO_);

	glBindVertexArray(VAO_);

	glBindBuffer(GL_ARRAY_BUFFER, VBO_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0); // Unbind VAO to avoid accidental later changes.
}

void GameplayState::onExit() {
    LOG_INFO("GameplayState: exited");
	glDeleteVertexArrays(1, &VAO_);
    glDeleteBuffers(1, &VBO_);
	shader_.destroy();
}

void GameplayState::update(float dt) {
    float speed = 1.0f * dt;

    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT) == GLFW_PRESS) {
        x_ -= speed;
        LOG_INFO("GameplayState: move left  x=" + std::to_string(x_));
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_RIGHT) == GLFW_PRESS) {
        x_ += speed;
        LOG_INFO("GameplayState: move right x=" + std::to_string(x_));
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_UP) == GLFW_PRESS) {
        y_ += speed;
        LOG_INFO("GameplayState: move up    y=" + std::to_string(y_));
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_DOWN) == GLFW_PRESS) {
        y_ -= speed;
        LOG_INFO("GameplayState: move down  y=" + std::to_string(y_));
    }

    // LMB input.
    bool lmbNow = glfwGetMouseButton(glfwGetCurrentContext(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (lmbNow && !lmbWasPressed_) {
        scale_ += 0.1f;
        LOG_INFO("GameplayState: scale up   scale=" + std::to_string(scale_));
    }
    lmbWasPressed_ = lmbNow;

    // RMB input.
    bool rmbNow = glfwGetMouseButton(glfwGetCurrentContext(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (rmbNow && !rmbWasPressed_) {
        rotation_ += 3.14159f / 4.0f;
        LOG_INFO("GameplayState: rotate     rotation=" + std::to_string(rotation_));
    }
    rmbWasPressed_ = rmbNow;

    // MMB input.
    bool mmbNow = glfwGetMouseButton(glfwGetCurrentContext(), GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    if (mmbNow && !mmbWasPressed_) {
        scale_ -= 0.1f;
        LOG_INFO("GameplayState: scale down scale=" + std::to_string(scale_));
    }
    mmbWasPressed_ = mmbNow;
}

void GameplayState::render() {
    if (shader_.getId() == 0 || VAO_ == 0) {
        return;
    }

    shader_.use();

	GLint posLoc = glGetUniformLocation(shader_.getId(), "uPosition");
	GLint scaleLoc = glGetUniformLocation(shader_.getId(), "uScale");
	GLint rotLoc = glGetUniformLocation(shader_.getId(), "uRotation");
	GLint centerLoc = glGetUniformLocation(shader_.getId(), "uCenter");

	glUniform2f(posLoc, x_, y_);
	glUniform1f(scaleLoc, scale_);
    glUniform1f(rotLoc, rotation_);
    glUniform2f(centerLoc, centerX_, centerY_);

    glBindVertexArray(VAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3); // Draw 3 vertices == 1 triangle.
}
