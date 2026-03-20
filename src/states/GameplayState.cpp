#include "states/GameplayState.h"
#include "ecs/Components.h"
#include "render/IRenderAdapter.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace {
constexpr float kMoveSpeed = 1.0f;
constexpr float kScaleStep = 0.1f;
constexpr float kRotationStep = 3.1415926f / 4.0f;
constexpr float kMinScale = 0.1f;
}

GameplayState::GameplayState(IRenderAdapter& renderer)
	: renderSystem_(renderer) {
}

void GameplayState::onEnter() {
	LOG_INFO("GameplayState: entered");
	world_.clear();
	world_.addUpdateSystem(spinSystem_);
	world_.addRenderSystem(renderSystem_);
	createScene();
	lmbWasPressed_ = false;
	rmbWasPressed_ = false;
	mmbWasPressed_ = false;
}

void GameplayState::onExit() {
	LOG_INFO("GameplayState: exited");
	world_.clear();
	controllableEntity_ = kInvalidEntity;
}

void GameplayState::update(float dt) {
	handleInput(dt);
	world_.updateSystems(dt);
}

void GameplayState::render() {
	world_.renderSystems();
}

void GameplayState::createScene() {
	// Cowboy Hat модель (управляемая, развернута лицом к пользователю)
	Entity cowboyHatEntity = world_.createEntity();
	world_.addComponent<Tag>(cowboyHatEntity, "CowboyHat");
	world_.addComponent<Transform>(cowboyHatEntity, Transform{
		{-0.75f, 0.0f, 0.0f},         // position (слева, расстояние уменьшено в 2 раза)
		{0.0f, 3.1415926f, 0.0f},     // rotation (x, y, z) - поворот по Y на 180°
		{1.0f, 1.0f, 1.0f}            // scale
	});
	
	MeshRenderer cowboyHatRenderer;
	cowboyHatRenderer.meshPath = "assets/models/Cowboy Hat.obj";
	cowboyHatRenderer.baseColorTexturePath = "assets/models/textures/Cowboy Hat_Hat_BaseColor.png";
	cowboyHatRenderer.vertexShaderPath = "assets/shaders/mesh_vertex.glsl";
	cowboyHatRenderer.fragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";
	cowboyHatRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
	world_.addComponent<MeshRenderer>(cowboyHatEntity, cowboyHatRenderer);
	world_.addComponent<Spin>(cowboyHatEntity, Spin{0.5f});
	
	controllableEntity_ = cowboyHatEntity;

	// Bookcase модель (только вращается, не управляется)
	Entity bookcaseEntity = world_.createEntity();
	world_.addComponent<Tag>(bookcaseEntity, "Bookcase");
	world_.addComponent<Transform>(bookcaseEntity, Transform{
		{0.75f, 0.0f, 0.0f},          // position (справа, расстояние уменьшено в 2 раза)
		{0.0f, 0.0f, 0.0f},           // rotation
		{0.25f, 0.25f, 0.25f}         // scale (в 2 раза меньше: 0.5 / 2 = 0.25)
	});
	
	MeshRenderer bookcaseRenderer;
	bookcaseRenderer.meshPath = "assets/models/bookcase_with_a_drawer.obj";
	bookcaseRenderer.baseColorTexturePath = "assets/models/Bakes/Bookcase&Handle baked diffuse.png";
	bookcaseRenderer.vertexShaderPath = "assets/shaders/mesh_vertex.glsl";
	bookcaseRenderer.fragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";
	bookcaseRenderer.color = {1.0f, 1.0f, 1.0f, 1.0f};
	world_.addComponent<MeshRenderer>(bookcaseEntity, bookcaseRenderer);
	world_.addComponent<Spin>(bookcaseEntity, Spin{0.3f}); // Медленнее вращается

	LOG_INFO("GameplayState: created scene with Cowboy Hat and Bookcase models");
}

void GameplayState::handleInput(float dt) {
	if (!world_.isAlive(controllableEntity_)) {
		return;
	}

	GLFWwindow* window = glfwGetCurrentContext();
	if (window == nullptr) {
		return;
	}

	Transform& transform = world_.getComponent<Transform>(controllableEntity_);
	const float moveStep = kMoveSpeed * dt;

	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
		transform.position.x -= moveStep;
	}
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
		transform.position.x += moveStep;
	}
	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
		transform.position.y += moveStep;
	}
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
		transform.position.y -= moveStep;
	}

	const bool lmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if (lmbNow && !lmbWasPressed_) {
		transform.scale.x += kScaleStep;
		transform.scale.y += kScaleStep;
		transform.scale.z += kScaleStep;
	}
	lmbWasPressed_ = lmbNow;

	const bool rmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	if (rmbNow && !rmbWasPressed_) {
		transform.rotation.y += kRotationStep;
	}
	rmbWasPressed_ = rmbNow;

	const bool mmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
	if (mmbNow && !mmbWasPressed_) {
		transform.scale.x = std::max(kMinScale, transform.scale.x - kScaleStep);
		transform.scale.y = std::max(kMinScale, transform.scale.y - kScaleStep);
		transform.scale.z = std::max(kMinScale, transform.scale.z - kScaleStep);
	}
	mmbWasPressed_ = mmbNow;
}

void GameplayState::attachChild(Entity parent, Entity child) {
	if (!world_.hasComponent<Hierarchy>(parent)) {
		world_.addComponent<Hierarchy>(parent, Hierarchy{});
	}

	Hierarchy childHierarchy{};
	childHierarchy.parent = parent;
	world_.addComponent<Hierarchy>(child, childHierarchy);

	world_.getComponent<Hierarchy>(parent).children.push_back(child);
}
