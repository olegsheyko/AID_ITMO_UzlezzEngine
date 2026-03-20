#include "states/GameplayState.h"

#include "core/Logger.h"
#include "ecs/Components.h"
#include "render/IRenderAdapter.h"
#include "resources/HotReload.h"
#include "resources/ResourceManager.h"
#include "resources/SceneManifest.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace {
constexpr float kMoveSpeed = 1.0f;
constexpr float kScaleStep = 0.1f;
constexpr float kRotationStep = 3.1415926f / 4.0f;
constexpr float kMinScale = 0.1f;
constexpr const char* kSceneManifestPath = "assets/scenes/demo_scene.json";
constexpr const char* kFallbackMeshPath = "assets/models/demo_cube.obj";
constexpr const char* kFallbackTexturePath = "assets/textures/WoodCrate02.dds";
constexpr const char* kFallbackVertexShaderPath = "assets/shaders/mesh_vertex.glsl";
constexpr const char* kFallbackFragmentShaderPath = "assets/shaders/mesh_fragment_simple_texture.glsl";
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
	if (createSceneFromManifest()) {
		return;
	}

	LOG_ERROR("Falling back to resource-based test scene");

	auto& resourceManager = ResourceManager::getInstance();
	MeshRenderer renderer;
	renderer.meshId = "fallback_cube";
	renderer.baseColorTextureId = "fallback_crate";
	renderer.shaderId = "fallback_textured";
	renderer.cachedMesh = resourceManager.load<MeshData>(kFallbackMeshPath);
	renderer.cachedBaseColorTexture = resourceManager.load<TextureData>(kFallbackTexturePath);
	renderer.cachedShader = resourceManager.loadShader(kFallbackVertexShaderPath, kFallbackFragmentShaderPath);

	if (!renderer.cachedMesh || !renderer.cachedShader || !renderer.cachedBaseColorTexture) {
		LOG_ERROR("Failed to build fallback scene resources");
		return;
	}

	HotReload::getInstance().watchFile(kFallbackVertexShaderPath);
	HotReload::getInstance().watchFile(kFallbackFragmentShaderPath);

	controllableEntity_ = world_.createEntity();
	world_.addComponent<Tag>(controllableEntity_, "FallbackCube");
	world_.addComponent<Transform>(controllableEntity_, Transform{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.6f, 0.6f, 0.6f}});
	world_.addComponent<MeshRenderer>(controllableEntity_, renderer);
}

bool GameplayState::createSceneFromManifest() {
	SceneManifest manifest;
	if (!manifest.loadFromFile(kSceneManifestPath)) {
		return false;
	}

	auto& resourceManager = ResourceManager::getInstance();

	for (const SceneEntityDescription& description : manifest.getEntities()) {
		const std::string* meshPath = manifest.findMeshPath(description.meshId);
		const ShaderAssetPaths* shaderPaths = manifest.findShader(description.shaderId);
		if (meshPath == nullptr || shaderPaths == nullptr) {
			LOG_ERROR("Scene entity has unresolved resources: " + description.tag);
			continue;
		}

		MeshRenderer renderer;
		renderer.meshId = description.meshId;
		renderer.baseColorTextureId = description.baseColorTextureId;
		renderer.shaderId = description.shaderId;
		renderer.cachedMesh = resourceManager.load<MeshData>(*meshPath);
		renderer.cachedShader = resourceManager.loadShader(shaderPaths->vertexPath, shaderPaths->fragmentPath);

		if (!description.baseColorTextureId.empty()) {
			if (const std::string* texturePath = manifest.findTexturePath(description.baseColorTextureId)) {
				renderer.cachedBaseColorTexture = resourceManager.load<TextureData>(*texturePath);
			}
		}

		if (!renderer.cachedMesh || !renderer.cachedShader) {
			LOG_ERROR("Failed to load scene resources for entity: " + description.tag);
			continue;
		}

		Entity entity = world_.createEntity();
		world_.addComponent<Tag>(entity, Tag{description.tag});
		world_.addComponent<Transform>(entity, Transform{description.position, description.rotation, description.scale});
		world_.addComponent<MeshRenderer>(entity, renderer);

		if (description.spinSpeed != 0.0f) {
			world_.addComponent<Spin>(entity, Spin{description.spinSpeed});
		}

		if (description.controllable) {
			controllableEntity_ = entity;
		}

		HotReload::getInstance().watchFile(shaderPaths->vertexPath);
		HotReload::getInstance().watchFile(shaderPaths->fragmentPath);
	}

	return world_.isAlive(controllableEntity_);
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
