#include "states/GameplayState.h"

#include "core/ServiceLocator.h"
#include "events/CollisionEvent.h"
#include "core/Logger.h"
#include "ecs/Components.h"
#include "input/InputManager.h"
#include "input/KeyCode.h"
#include "render/IRenderAdapter.h"
#include "resources/HotReload.h"
#include "resources/ResourceManager.h"
#include "resources/SceneManifest.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMoveSpeed = 1.0f;
constexpr float kJumpSpeed = 3.5f;
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
	: cameraSystem_(renderer),
      physicsSystem_(),
	  renderSystem_(renderer),
      debugRenderSystem_(renderer) {
}

void GameplayState::onEnter() {
	LOG_INFO("GameplayState: entered");
	world_.clear();
    ServiceLocator::getEventDispatcher().clear();
    ServiceLocator::getEventDispatcher().addListener<CollisionEvent>([](const CollisionEvent& event) {
        LOG_INFO(
            "CollisionEvent: entities " +
            std::to_string(event.first) +
            " and " +
            std::to_string(event.second) +
            ", penetration=" +
            std::to_string(event.penetration));
    });
	world_.addUpdateSystem(physicsSystem_);
    world_.addUpdateSystem(cameraSystem_);
	world_.addUpdateSystem(spinSystem_);
	world_.addRenderSystem(renderSystem_);
    world_.addRenderSystem(debugRenderSystem_);
    InputManager& inputManager = InputManager::getInstance();
    inputManager.BindAction("ToggleDebug", KeyCode::KEY_F3);
    inputManager.BindAction("MoveLeft", KeyCode::KEY_LEFT);
    inputManager.BindAction("MoveLeft", KeyCode::KEY_A);
    inputManager.BindAction("MoveRight", KeyCode::KEY_RIGHT);
    inputManager.BindAction("MoveRight", KeyCode::KEY_D);
    inputManager.BindAction("MoveForward", KeyCode::KEY_UP);
    inputManager.BindAction("MoveForward", KeyCode::KEY_W);
    inputManager.BindAction("MoveBackward", KeyCode::KEY_DOWN);
    inputManager.BindAction("MoveBackward", KeyCode::KEY_S);
    inputManager.BindAction("Jump", KeyCode::KEY_SPACE);
    inputManager.BindAction("CameraForward", KeyCode::KEY_I);
    inputManager.BindAction("CameraBackward", KeyCode::KEY_K);
    inputManager.BindAction("CameraLeft", KeyCode::KEY_J);
    inputManager.BindAction("CameraRight", KeyCode::KEY_L);
    inputManager.BindAction("CameraUp", KeyCode::KEY_U);
    inputManager.BindAction("CameraDown", KeyCode::KEY_O);
	createScene();
    createCamera();
    debugRenderSystem_.setEnabled(false);
	lmbWasPressed_ = false;
	rmbWasPressed_ = false;
	mmbWasPressed_ = false;
}

void GameplayState::onExit() {
	LOG_INFO("GameplayState: exited");
    ServiceLocator::getEventDispatcher().clear();
	world_.clear();
    cameraEntity_ = kInvalidEntity;
	controllableEntity_ = kInvalidEntity;
}

void GameplayState::update(float dt) {
	handleInput(dt);
	world_.updateSystems(dt);
}

void GameplayState::render() {
	world_.renderSystems();
}

void GameplayState::createCamera() {
    cameraEntity_ = world_.createEntity();
    world_.addComponent<Tag>(cameraEntity_, Tag{"MainCamera"});
    world_.addComponent<Transform>(cameraEntity_, Transform{
        Vec3{0.0f, 4.0f, 10.0f},
        Vec3{-0.3f, 0.0f, 0.0f},
        Vec3{1.0f, 1.0f, 1.0f}
    });
    world_.addComponent<Camera>(cameraEntity_, Camera{45.0f, 0.1f, 100.0f, 800.0f / 600.0f, true, Mat4::identity(), Mat4::identity()});
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
    world_.addComponent<Rigidbody>(controllableEntity_, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
    world_.addComponent<Collider>(controllableEntity_, Collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f});
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
        if (description.hasRigidbody) {
            world_.addComponent<Rigidbody>(entity, description.rigidbody);
        }
        if (description.hasCollider) {
            world_.addComponent<Collider>(entity, description.collider);
        }

		if (description.spinSpeed != 0.0f) {
			world_.addComponent<Spin>(entity, Spin{description.spinSpeed});
		}

		if (description.controllable) {
			controllableEntity_ = entity;
			if (!world_.hasComponent<Rigidbody>(entity)) {
				world_.addComponent<Rigidbody>(entity, Rigidbody{Vec3{}, Vec3{}, 1.0f, true});
			}
            if (!world_.hasComponent<Collider>(entity)) {
                world_.addComponent<Collider>(entity, Collider{ColliderType::Box, Vec3{0.5f, 0.5f, 0.5f}, Vec3{}, 0.5f});
            }
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

	if (!world_.hasComponent<Rigidbody>(controllableEntity_)) {
        return;
    }

	Transform& transform = world_.getComponent<Transform>(controllableEntity_);
    Rigidbody& rigidbody = world_.getComponent<Rigidbody>(controllableEntity_);
    InputManager& inputManager = InputManager::getInstance();

    if (inputManager.isActionPressed("ToggleDebug")) {
        debugRenderSystem_.setEnabled(!debugRenderSystem_.isEnabled());
    }

    float horizontalVelocity = 0.0f;
    float depthVelocity = 0.0f;

	if (inputManager.IsActionActive("MoveLeft")) {
		horizontalVelocity -= kMoveSpeed;
	}
	if (inputManager.IsActionActive("MoveRight")) {
		horizontalVelocity += kMoveSpeed;
	}
	if (inputManager.IsActionActive("MoveForward")) {
		depthVelocity -= kMoveSpeed;
	}
	if (inputManager.IsActionActive("MoveBackward")) {
		depthVelocity += kMoveSpeed;
	}

    rigidbody.velocity.x = horizontalVelocity;
    rigidbody.velocity.z = depthVelocity;

    if (inputManager.isActionPressed("Jump") && !rigidbody.useGravity) {
        rigidbody.velocity.y = std::max(rigidbody.velocity.y, kJumpSpeed);
    } else if (inputManager.isActionPressed("Jump") && std::abs(transform.position.y) < 0.051f) {
        rigidbody.velocity.y = kJumpSpeed;
    }

	const bool lmbNow = inputManager.isMouseButtonDown(KeyCode::MouseLeft);
	if (lmbNow && !lmbWasPressed_) {
		transform.scale.x += kScaleStep;
		transform.scale.y += kScaleStep;
		transform.scale.z += kScaleStep;
	}
	lmbWasPressed_ = lmbNow;

	const bool rmbNow = inputManager.isMouseButtonDown(KeyCode::MouseRight);
	if (rmbNow && !rmbWasPressed_ && !world_.isAlive(cameraEntity_)) {
		transform.rotation.y += kRotationStep;
	}
	rmbWasPressed_ = rmbNow;

	const bool mmbNow = inputManager.isMouseButtonDown(KeyCode::MouseMiddle);
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
