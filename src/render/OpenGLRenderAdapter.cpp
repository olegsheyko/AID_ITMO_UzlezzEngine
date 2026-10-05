#include "render/OpenGLRenderAdapter.h"
#include "core/Logger.h"
#include "resources/ResourceTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {
void framebufferSizeCallback(GLFWwindow*, int width, int height) {
	glViewport(0, 0, width, height);
}

// Сетка редактора: квадрат на плоскости z = uHeight вокруг камеры, линии — через fwidth.
const char* kGridVertexShader = R"(#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCamera;
uniform float uHeight;
uniform float uExtent;
out vec3 vWorld;
void main() {
	vWorld = vec3(uCamera.xy + aPos.xy * uExtent, uHeight);
	gl_Position = uProjection * uView * vec4(vWorld, 1.0);
}
)";

const char* kGridFragmentShader = R"(#version 330 core
in vec3 vWorld;
uniform vec3 uCamera;
uniform float uExtent;
out vec4 FragColor;
float gridLine(vec2 coord, float spacing, float width) {
	vec2 c = coord / spacing;
	vec2 d = fwidth(c);
	vec2 g = abs(fract(c - 0.5) - 0.5) / (d * width);
	return 1.0 - min(min(g.x, g.y), 1.0);
}
void main() {
	float distance = length(vWorld - uCamera);
	float height = max(abs(uCamera.z - vWorld.z), 1.0);
	// Мелкая сетка гаснет раньше крупной, иначе вдали она рябит.
	float minorFade = 1.0 - smoothstep(height * 4.0, height * 14.0, distance);
	float majorFade = 1.0 - smoothstep(uExtent * 0.25, uExtent * 0.9, distance);
	float minor = gridLine(vWorld.xy, 1.0, 1.0) * 0.22 * minorFade;
	float major = gridLine(vWorld.xy, 10.0, 1.3) * 0.42 * majorFade;
	vec3 color = vec3(0.62);
	float alpha = max(minor, major);
	vec2 axisWidth = fwidth(vWorld.xy) * 1.6;
	float xAxis = (1.0 - min(abs(vWorld.y) / axisWidth.y, 1.0)) * majorFade;
	float yAxis = (1.0 - min(abs(vWorld.x) / axisWidth.x, 1.0)) * majorFade;
	if (xAxis > 0.0) { color = mix(color, vec3(0.90, 0.33, 0.36), xAxis); alpha = max(alpha, xAxis * 0.75); }
	if (yAxis > 0.0) { color = mix(color, vec3(0.50, 0.80, 0.34), yAxis); alpha = max(alpha, yAxis * 0.75); }
	if (alpha < 0.003) discard;
	FragColor = vec4(color, alpha);
}
)";

// Маска выделения: вершинный шейдер мешей с белым фрагментом.
const char* kMaskFragmentShader = R"(#version 330 core
out vec4 FragColor;
void main() {
	FragColor = vec4(1.0);
}
)";

const char* kFullscreenVertexShader = R"(#version 330 core
out vec2 vUv;
void main() {
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	vUv = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kOutlineFragmentShader = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uMask;
uniform vec4 uColor;
uniform vec2 uTexel;
uniform float uRadius;
out vec4 FragColor;
void main() {
	float center = texture(uMask, vUv).r;
	float neighbour = 0.0;
	int r = int(ceil(uRadius));
	for (int x = -r; x <= r; ++x) {
		for (int y = -r; y <= r; ++y) {
			float d = length(vec2(x, y));
			if (d > uRadius + 0.5) continue;
			float weight = clamp(uRadius + 0.5 - d, 0.0, 1.0);
			neighbour = max(neighbour, texture(uMask, vUv + vec2(x, y) * uTexel).r * weight);
		}
	}
	float edge = neighbour * (1.0 - center);
	float fill = center * 0.06;
	float alpha = max(edge, fill);
	if (alpha <= 0.001) discard;
	FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
)";

const char* kSkyFragmentShader = R"(#version 330 core
in vec2 vUv;
uniform mat4 uInverseViewProjection;
uniform vec3 uCamera;
uniform vec3 uSun;
out vec4 FragColor;
void main() {
	vec4 far = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
	vec3 direction = normalize(far.xyz / far.w - uCamera);
	float t = direction.z;
	vec3 zenith = vec3(0.24, 0.42, 0.72);
	vec3 horizon = vec3(0.70, 0.78, 0.86);
	vec3 ground = vec3(0.19, 0.195, 0.205);
	vec3 color = t > 0.0 ? mix(horizon, zenith, pow(t, 0.55)) : mix(horizon * 0.75, ground, pow(-t, 0.3));
	float sun = max(dot(direction, normalize(uSun)), 0.0);
	color += vec3(1.0, 0.93, 0.78) * (pow(sun, 900.0) * 1.6 + pow(sun, 48.0) * 0.22);
	FragColor = vec4(color, 1.0);
}
)";

std::string readTextFile(const char* path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return {};
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}
}

OpenGLRenderAdapter::~OpenGLRenderAdapter() {
	shutdown();
}

bool OpenGLRenderAdapter::init(int width, int height, const std::string& title) {
	shutdown();

	if (!glfwInit()) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to initialize GLFW");
		return false;
	}
	initialized_ = true;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	// macOS выдаёт core-контекст 3.2+ только с forward-compat, иначе окно не создаётся.
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
	glfwWindowHint(GLFW_MAXIMIZED, hiddenWindow_ ? GLFW_FALSE : GLFW_TRUE);
	if (hiddenWindow_) {
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
	}

	window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!window_) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to create GLFW window");
		shutdown();
		return false;
	}

	if (GLFWmonitor* monitor = hiddenWindow_ ? nullptr : glfwGetPrimaryMonitor()) {
		int workX = 0;
		int workY = 0;
		int workWidth = width;
		int workHeight = height;
		glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
		glfwSetWindowPos(window_, workX, workY);
		glfwSetWindowSize(window_, workWidth, workHeight);
		glfwMaximizeWindow(window_);
	}

	glfwMakeContextCurrent(window_);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to initialize GLAD");
		shutdown();
		return false;
	}

	glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
	glViewport(0, 0, width, height);
	glfwSwapInterval(1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (!createRenderResources()) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to create render resources");
		shutdown();
		return false;
	}

	LOG_INFO("OpenGLRenderAdapter: Successfully initialized OpenGL context");
	return true;
}

bool OpenGLRenderAdapter::isRunning() const {
	return window_ != nullptr && !glfwWindowShouldClose(window_);
}

void OpenGLRenderAdapter::setVSync(bool enabled) {
	if (window_) {
		glfwSwapInterval(enabled ? 1 : 0);
	}
}

void OpenGLRenderAdapter::pollEvents() {
	if (initialized_) {
		glfwPollEvents();
	}
}

void OpenGLRenderAdapter::beginFrame(float r, float g, float b) {
	if (window_) {
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		GLuint target = 0;
		if (captureEnabled_ && resizeTarget(captureTarget_, std::max(1, width), std::max(1, height), false, GL_RGBA8)) {
			target = captureTarget_.fbo;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, target);
		glViewport(0, 0, width, height);
	}

	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderAdapter::beginViewportFrame(int target, int width, int height, float r, float g, float b) {
	width = std::max(1, width);
	height = std::max(1, height);
	target = std::clamp(target, 0, kMaxViewportTargets - 1);

	RenderTarget& renderTarget = viewportTargets_[target];
	if (!resizeTarget(renderTarget, width, height, true, GL_RGBA8)) {
		return;
	}

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer_);
	glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.fbo);
	glViewport(0, 0, renderTarget.width, renderTarget.height);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	currentTarget_ = target;
	renderingViewport_ = true;
}

void OpenGLRenderAdapter::endViewportFrame() {
	if (!renderingViewport_) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer_));
	if (window_) {
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		glViewport(0, 0, width, height);
	}
	renderingViewport_ = false;
}

unsigned int OpenGLRenderAdapter::getViewportTextureId(int target) const {
	if (target < 0 || target >= kMaxViewportTargets) {
		return 0;
	}
	return viewportTargets_[target].color;
}

void OpenGLRenderAdapter::drawGrid(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, float height) {
	if (gridProgram_ == 0 || gridVao_ == 0) {
		return;
	}
	// Дальность сетки растёт с высотой камеры, чтобы сверху она не обрывалась.
	const float extent = std::clamp(std::abs(cameraPosition.z - height) * 30.0f, 60.0f, 600.0f);
	GLboolean depthWrite = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(gridProgram_);
	glUniformMatrix4fv(glGetUniformLocation(gridProgram_, "uView"), 1, GL_FALSE, viewMatrix.data());
	glUniformMatrix4fv(glGetUniformLocation(gridProgram_, "uProjection"), 1, GL_FALSE, projectionMatrix.data());
	glUniform3f(glGetUniformLocation(gridProgram_, "uCamera"), cameraPosition.x, cameraPosition.y, cameraPosition.z);
	glUniform1f(glGetUniformLocation(gridProgram_, "uHeight"), height);
	glUniform1f(glGetUniformLocation(gridProgram_, "uExtent"), extent);
	// Сетка часто лежит ровно на полу: смещаем её глубину к камере, чтобы не мерцала.
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-2.0f, -4.0f);
	glBindVertexArray(gridVao_);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glUseProgram(0);
	glDepthMask(depthWrite);
}

unsigned int OpenGLRenderAdapter::copyViewportTexture(int target) {
	if (target < 0 || target >= kMaxViewportTargets || viewportTargets_[target].fbo == 0) {
		return 0;
	}
	const RenderTarget& source = viewportTargets_[target];
	GLuint texture = 0;
	glGenTextures(1, &texture);
	if (texture == 0) {
		return 0;
	}
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source.width, source.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	GLint previousDraw = 0;
	GLint previousRead = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
	GLuint copyFbo = 0;
	glGenFramebuffers(1, &copyFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFbo);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fbo);
	glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, source.width, source.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
	glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
	glDeleteFramebuffers(1, &copyFbo);
	++liveTextures_;
	return texture;
}

void OpenGLRenderAdapter::drawSky(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, const Vec3& sunDirection) {
	if (skyProgram_ == 0 || emptyVao_ == 0) {
		return;
	}
	// Небо считается без переноса камеры: так точность не теряется на больших координатах.
	Mat4 rotationOnly = viewMatrix;
	rotationOnly.values[12] = rotationOnly.values[13] = rotationOnly.values[14] = 0.0f;
	const Mat4 inverseViewProjection = Math::inverse(Math::multiply(projectionMatrix, rotationOnly));
	GLboolean depthWrite = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
	const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glUseProgram(skyProgram_);
	glUniformMatrix4fv(glGetUniformLocation(skyProgram_, "uInverseViewProjection"), 1, GL_FALSE, inverseViewProjection.data());
	glUniform3f(glGetUniformLocation(skyProgram_, "uCamera"), 0.0f, 0.0f, 0.0f);
	glUniform3f(glGetUniformLocation(skyProgram_, "uSun"), sunDirection.x, sunDirection.y, sunDirection.z);
	glBindVertexArray(emptyVao_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glUseProgram(0);
	glDepthMask(depthWrite);
	if (depthTest) {
		glEnable(GL_DEPTH_TEST);
	}
	(void)cameraPosition;
}

void OpenGLRenderAdapter::beginSelectionMask() {
	if (!renderingViewport_ || maskProgram_ == 0) {
		return;
	}
	const RenderTarget& target = viewportTargets_[currentTarget_];
	if (!resizeTarget(maskTarget_, target.width, target.height, false, GL_R8)) {
		return;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, maskTarget_.fbo);
	glViewport(0, 0, maskTarget_.width, maskTarget_.height);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

void OpenGLRenderAdapter::endSelectionMask(const Vec4& color, float thicknessPixels) {
	if (!renderingViewport_ || outlineProgram_ == 0 || maskTarget_.fbo == 0) {
		return;
	}
	const RenderTarget& target = viewportTargets_[currentTarget_];
	glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
	glViewport(0, 0, target.width, target.height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(outlineProgram_);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, maskTarget_.color);
	glUniform1i(glGetUniformLocation(outlineProgram_, "uMask"), 0);
	glUniform4f(glGetUniformLocation(outlineProgram_, "uColor"), color.x, color.y, color.z, color.w);
	glUniform2f(glGetUniformLocation(outlineProgram_, "uTexel"), 1.0f / maskTarget_.width, 1.0f / maskTarget_.height);
	glUniform1f(glGetUniformLocation(outlineProgram_, "uRadius"), std::clamp(thicknessPixels, 1.0f, 6.0f));
	glBindVertexArray(emptyVao_);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glEnable(GL_DEPTH_TEST);
}

bool OpenGLRenderAdapter::readCapturedFrame(std::vector<unsigned char>& outRgba, int& outWidth, int& outHeight) const {
	if (captureTarget_.fbo == 0) {
		return false;
	}
	outWidth = captureTarget_.width;
	outHeight = captureTarget_.height;
	outRgba.assign(static_cast<std::size_t>(outWidth) * outHeight * 4, 0);
	GLint previous = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
	glBindFramebuffer(GL_FRAMEBUFFER, captureTarget_.fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, outWidth, outHeight, GL_RGBA, GL_UNSIGNED_BYTE, outRgba.data());
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
	// OpenGL отдаёт строки снизу вверх.
	const std::size_t stride = static_cast<std::size_t>(outWidth) * 4;
	std::vector<unsigned char> row(stride);
	for (int y = 0; y < outHeight / 2; ++y) {
		unsigned char* top = outRgba.data() + y * stride;
		unsigned char* bottom = outRgba.data() + (outHeight - 1 - y) * stride;
		std::copy(top, top + stride, row.begin());
		std::copy(bottom, bottom + stride, top);
		std::copy(row.begin(), row.end(), bottom);
	}
	for (std::size_t i = 3; i < outRgba.size(); i += 4) {
		outRgba[i] = 255;
	}
	return true;
}

void OpenGLRenderAdapter::drawPrimitive(
	PrimitiveType primitive,
	const Mat4& modelMatrix,
	const Vec4& color,
	const Mat4& viewMatrix,
	const Mat4& projectionMatrix) {
	const PrimitiveMesh* mesh = getMesh(primitive);
	if (mesh == nullptr || shader_.getId() == 0) {
		return;
	}

	shader_.use();
	glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, modelMatrix.data());
	if (viewLocation_ >= 0) {
		glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, viewMatrix.data());
	}
	if (projectionLocation_ >= 0) {
		glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projectionMatrix.data());
	}
	glUniform4f(colorLocation_, color.x, color.y, color.z, color.w);

	glBindVertexArray(mesh->vao);
	glDrawArrays(mesh->drawMode, 0, mesh->vertexCount);
	glBindVertexArray(0);
}

void OpenGLRenderAdapter::drawDebugAABB(
	const Vec3& center,
	const Vec3& halfExtents,
	const Vec4& color,
	const Mat4& viewMatrix,
	const Mat4& projectionMatrix) {
	constexpr float kHalfPi = 1.5707963f;

	auto drawEdge = [&](const Vec3& edgeCenter, const Vec3& rotation, float length) {
		const Mat4 modelMatrix = Math::composeTransform(
			edgeCenter,
			rotation,
			Vec3{length, 1.0f, 1.0f});
		drawPrimitive(PrimitiveType::Line, modelMatrix, color, viewMatrix, projectionMatrix);
	};

	const float minX = center.x - halfExtents.x;
	const float maxX = center.x + halfExtents.x;
	const float minY = center.y - halfExtents.y;
	const float maxY = center.y + halfExtents.y;
	const float minZ = center.z - halfExtents.z;
	const float maxZ = center.z + halfExtents.z;

	const float sizeX = halfExtents.x * 2.0f;
	const float sizeY = halfExtents.y * 2.0f;
	const float sizeZ = halfExtents.z * 2.0f;

	for (float y : {minY, maxY}) {
		for (float z : {minZ, maxZ}) {
			drawEdge(Vec3{center.x, y, z}, Vec3{}, sizeX);
		}
	}

	for (float x : {minX, maxX}) {
		for (float z : {minZ, maxZ}) {
			drawEdge(Vec3{x, center.y, z}, Vec3{0.0f, 0.0f, kHalfPi}, sizeY);
		}
	}

	for (float x : {minX, maxX}) {
		for (float y : {minY, maxY}) {
			drawEdge(Vec3{x, y, center.z}, Vec3{0.0f, -kHalfPi, 0.0f}, sizeZ);
		}
	}
}

void OpenGLRenderAdapter::drawDebugSphere(
	const Vec3& center,
	float radius,
	const Vec4& color,
	const Mat4& viewMatrix,
	const Mat4& projectionMatrix) {
	if (radius <= 0.0f) {
		return;
	}

	// A sphere can be completely inside its mesh; show its wireframe through it.
	const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
	GLboolean depthWriteEnabled = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteEnabled);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	const Mat4 modelMatrix = Math::composeTransform(center, Vec3{}, Vec3{radius, radius, radius});
	drawPrimitive(PrimitiveType::WireSphere, modelMatrix, color, viewMatrix, projectionMatrix);

	glDepthMask(depthWriteEnabled);
	if (depthTestEnabled) {
		glEnable(GL_DEPTH_TEST);
	}
}

bool OpenGLRenderAdapter::uploadMesh(
	const void* vertexData,
	std::size_t vertexStride,
	std::size_t vertexCount,
	const unsigned int* indexData,
	std::size_t indexCount,
	unsigned int& outVao,
	unsigned int& outVbo,
	unsigned int& outEbo) {
	outVao = 0;
	outVbo = 0;
	outEbo = 0;

	if (vertexData == nullptr || indexData == nullptr || vertexStride == 0 || vertexCount == 0 || indexCount == 0) {
		return false;
	}

	glGenVertexArrays(1, &outVao);
	glGenBuffers(1, &outVbo);
	glGenBuffers(1, &outEbo);

	if (outVao == 0 || outVbo == 0 || outEbo == 0) {
		destroyMesh(outVao, outVbo, outEbo);
		return false;
	}

	glBindVertexArray(outVao);

	glBindBuffer(GL_ARRAY_BUFFER, outVbo);
	glBufferData(
		GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(vertexStride * vertexCount),
		vertexData,
		GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, outEbo);
	glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(sizeof(unsigned int) * indexCount),
		indexData,
		GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vertexStride), (void*)0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vertexStride), (void*)(3 * sizeof(float)));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vertexStride), (void*)(6 * sizeof(float)));
	if (vertexStride == sizeof(Vertex)) {
		glEnableVertexAttribArray(3);
		glVertexAttribIPointer(3, 4, GL_INT, static_cast<GLsizei>(vertexStride), reinterpret_cast<void*>(offsetof(Vertex, boneIds)));
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vertexStride), reinterpret_cast<void*>(offsetof(Vertex, boneWeights)));
	}

	glBindVertexArray(0);
	return true;
}

bool OpenGLRenderAdapter::createTexture(
	int width,
	int height,
	int channels,
	const unsigned char* pixels,
	unsigned int& outTextureId) {
	outTextureId = 0;

	if (width <= 0 || height <= 0 || pixels == nullptr) {
		return false;
	}

	glGenTextures(1, &outTextureId);
	if (outTextureId == 0) {
		return false;
	}

	GLenum internalFormat = GL_RGB;
	GLenum dataFormat = GL_RGB;

	if (channels == 1) {
		internalFormat = GL_RED;
		dataFormat = GL_RED;
	} else if (channels == 4) {
		internalFormat = GL_RGBA;
		dataFormat = GL_RGBA;
	}

	glBindTexture(GL_TEXTURE_2D, outTextureId);
	GLint unpackAlignment = 4;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, pixels);
	glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	++liveTextures_;
	return true;
}

bool OpenGLRenderAdapter::createShaderProgram(
	const std::string& vertexSource,
	const std::string& fragmentSource,
	unsigned int& outProgramId,
	std::string& outError) {
	outProgramId = 0;
	outError.clear();

	GLuint vertexShader = 0;
	GLuint fragmentShader = 0;
	if (!compileShader(GL_VERTEX_SHADER, vertexSource, vertexShader, outError)) {
		return false;
	}

	if (!compileShader(GL_FRAGMENT_SHADER, fragmentSource, fragmentShader, outError)) {
		glDeleteShader(vertexShader);
		return false;
	}

	outProgramId = glCreateProgram();
	glAttachShader(outProgramId, vertexShader);
	glAttachShader(outProgramId, fragmentShader);
	glLinkProgram(outProgramId);

	GLint success = GL_FALSE;
	glGetProgramiv(outProgramId, GL_LINK_STATUS, &success);
	if (success != GL_TRUE) {
		char infoLog[1024] = {};
		glGetProgramInfoLog(outProgramId, sizeof(infoLog), nullptr, infoLog);
		outError = infoLog;
		glDeleteProgram(outProgramId);
		outProgramId = 0;
	}

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return outProgramId != 0;
}

void OpenGLRenderAdapter::destroyMesh(unsigned int& vao, unsigned int& vbo, unsigned int& ebo) {
	if (ebo != 0) {
		glDeleteBuffers(1, &ebo);
		ebo = 0;
	}
	if (vbo != 0) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (vao != 0) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
}

void OpenGLRenderAdapter::destroyTexture(unsigned int& textureId) {
	if (textureId != 0) {
		glDeleteTextures(1, &textureId);
		textureId = 0;
		if (liveTextures_ > 0) {
			--liveTextures_;
		}
	}
}

void OpenGLRenderAdapter::destroyShaderProgram(unsigned int& programId) {
	if (programId != 0) {
		glDeleteProgram(programId);
		programId = 0;
	}
}

void OpenGLRenderAdapter::useShaderProgram(unsigned int programId) {
	glUseProgram(programId);
}

void OpenGLRenderAdapter::setMatrix4(unsigned int programId, const char* name, const Mat4& value) {
	const GLint location = glGetUniformLocation(programId, name);
	if (location >= 0) {
		glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
	}
}

void OpenGLRenderAdapter::setSkinMatrices(unsigned int programId, const Mat4* matrices, std::size_t count) {
	if (!matrices || count == 0 || count > kMaxSkinBones) return;
	const GLuint block = glGetUniformBlockIndex(programId, "SkinPalette");
	if (block == GL_INVALID_INDEX) return;
	if (!skinBuffer_) {
		glGenBuffers(1, &skinBuffer_);
		glBindBuffer(GL_UNIFORM_BUFFER, skinBuffer_);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(Mat4)*kMaxSkinBones, nullptr, GL_STREAM_DRAW);
	}
	glBindBuffer(GL_UNIFORM_BUFFER, skinBuffer_);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Mat4)*count, matrices);
	glUniformBlockBinding(programId, block, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, skinBuffer_);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void OpenGLRenderAdapter::setInt(unsigned int programId, const char* name, int value) {
	const GLint location = glGetUniformLocation(programId, name);
	if (location >= 0) {
		glUniform1i(location, value);
	}
}

void OpenGLRenderAdapter::setFloat(unsigned int programId, const char* name, float value) {
	const GLint location = glGetUniformLocation(programId, name);
	if (location >= 0) {
		glUniform1f(location, value);
	}
}

void OpenGLRenderAdapter::setVec3(unsigned int programId, const char* name, const Vec3& value) {
	const GLint location = glGetUniformLocation(programId, name);
	if (location >= 0) {
		glUniform3f(location, value.x, value.y, value.z);
	}
}

void OpenGLRenderAdapter::bindTexture2D(unsigned int textureId, unsigned int unit) {
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, textureId);
}

void OpenGLRenderAdapter::drawIndexed(unsigned int vao, unsigned int indexCount) {
	if (vao == 0 || indexCount == 0) {
		return;
	}

	glBindVertexArray(vao);
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}

void OpenGLRenderAdapter::getFramebufferSize(int& width, int& height) const {
	width = 0;
	height = 0;
	if (renderingViewport_) {
		width = viewportTargets_[currentTarget_].width;
		height = viewportTargets_[currentTarget_].height;
		return;
	}
	if (window_ != nullptr) {
		glfwGetFramebufferSize(window_, &width, &height);
	}
}

void OpenGLRenderAdapter::endFrame() {
	if (window_) {
		glfwSwapBuffers(window_);
	}
}

void OpenGLRenderAdapter::shutdown() {
	bool released = false;

	destroyRenderResources();
	for (RenderTarget& target : viewportTargets_) {
		destroyTarget(target);
	}
	destroyTarget(maskTarget_);
	destroyTarget(captureTarget_);
	renderingViewport_ = false;

	if (window_) {
		glfwDestroyWindow(window_);
		window_ = nullptr;
		released = true;
	}
	if (initialized_) {
		glfwTerminate();
		initialized_ = false;
		released = true;
	}

	if (released) {
		LOG_INFO("OpenGLRenderAdapter: Shutdown complete");
	}
}

bool OpenGLRenderAdapter::createRenderResources() {
	if (!shader_.load("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl")) {
		return false;
	}

	modelLocation_ = glGetUniformLocation(shader_.getId(), "uModel");
	viewLocation_ = glGetUniformLocation(shader_.getId(), "uView");
	projectionLocation_ = glGetUniformLocation(shader_.getId(), "uProjection");
	colorLocation_ = glGetUniformLocation(shader_.getId(), "uColor");

	if (modelLocation_ < 0 || viewLocation_ < 0 || projectionLocation_ < 0 || colorLocation_ < 0) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to find shader uniforms");
		return false;
	}

	static const float lineVertices[] = {
		-0.5f, 0.0f, 0.0f,
		 0.5f, 0.0f, 0.0f
	};

	static const float triangleVertices[] = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.0f,  0.5f, 0.0f
	};

	static const float quadVertices[] = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,
		-0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f
	};

	static const float cubeVertices[] = {
		-0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
		-0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,

		-0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
		-0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,

		-0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
		-0.5f, -0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f,

		 0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
		 0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,

		-0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
		-0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,

		-0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
		-0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f
	};

	if (!setupMesh(lineMesh_, lineVertices, 2, GL_LINES)) {
		return false;
	}

	if (!setupMesh(triangleMesh_, triangleVertices, 3, GL_TRIANGLES)) {
		return false;
	}

	if (!setupMesh(quadMesh_, quadVertices, 6, GL_TRIANGLES)) {
		return false;
	}

	if (!setupMesh(cubeMesh_, cubeVertices, 36, GL_TRIANGLES)) {
		return false;
	}

	// Three perpendicular great circles, uploaded once and reused by all colliders.
	constexpr int kSegments = 64;
	constexpr int kVertexCount = 3 * kSegments * 2;
	constexpr float kTwoPi = 6.28318530718f;
	std::array<float, kVertexCount * 3> sphereVertices{};
	std::size_t next = 0;
	for (int plane = 0; plane < 3; ++plane) {
		for (int segment = 0; segment < kSegments; ++segment) {
			for (int endpoint = 0; endpoint < 2; ++endpoint) {
				const float angle = kTwoPi * static_cast<float>(segment + endpoint) / kSegments;
				const float c = std::cos(angle);
				const float s = std::sin(angle);
				const Vec3 point = plane == 0 ? Vec3{c, s, 0.0f}
					: plane == 1 ? Vec3{c, 0.0f, s} : Vec3{0.0f, c, s};
				sphereVertices[next++] = point.x;
				sphereVertices[next++] = point.y;
				sphereVertices[next++] = point.z;
			}
		}
	}
	if (!setupMesh(wireSphereMesh_, sphereVertices.data(), kVertexCount, GL_LINES)) {
		return false;
	}

	// Без сетки и контура редактор работает, просто беднее — не валим запуск.
	createEditorPrograms();
	return true;
}

GLuint OpenGLRenderAdapter::linkProgram(const std::string& vertexSource, const std::string& fragmentSource, const char* name) {
	unsigned int program = 0;
	std::string error;
	if (!createShaderProgram(vertexSource, fragmentSource, program, error)) {
		LOG_ERROR(std::string("OpenGLRenderAdapter: failed to build ") + name + " shader: " + error);
		return 0;
	}
	return program;
}

bool OpenGLRenderAdapter::createEditorPrograms() {
	gridProgram_ = linkProgram(kGridVertexShader, kGridFragmentShader, "grid");
	static const float gridQuad[] = {
		-1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
		-1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 0.0f,  -1.0f, 1.0f, 0.0f
	};
	glGenVertexArrays(1, &gridVao_);
	glGenBuffers(1, &gridVbo_);
	glBindVertexArray(gridVao_);
	glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(gridQuad), gridQuad, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	glGenVertexArrays(1, &emptyVao_);
	const std::string meshVertex = readTextFile("assets/shaders/mesh_vertex.glsl");
	if (!meshVertex.empty()) {
		maskProgram_ = linkProgram(meshVertex, kMaskFragmentShader, "selection mask");
	}
	outlineProgram_ = linkProgram(kFullscreenVertexShader, kOutlineFragmentShader, "selection outline");
	skyProgram_ = linkProgram(kFullscreenVertexShader, kSkyFragmentShader, "sky");
	return gridProgram_ != 0 && maskProgram_ != 0 && outlineProgram_ != 0;
}

void OpenGLRenderAdapter::destroyRenderResources() {
	if (skinBuffer_) { glDeleteBuffers(1, &skinBuffer_); skinBuffer_ = 0; }
	auto destroyMesh = [](PrimitiveMesh& mesh) {
		if (mesh.vbo != 0) {
			glDeleteBuffers(1, &mesh.vbo);
			mesh.vbo = 0;
		}
		if (mesh.vao != 0) {
			glDeleteVertexArrays(1, &mesh.vao);
			mesh.vao = 0;
		}
		mesh.vertexCount = 0;
		mesh.drawMode = GL_TRIANGLES;
	};

	destroyMesh(lineMesh_);
	destroyMesh(triangleMesh_);
	destroyMesh(quadMesh_);
	destroyMesh(cubeMesh_);
	destroyMesh(wireSphereMesh_);
	for (GLuint* program : {&gridProgram_, &maskProgram_, &outlineProgram_, &skyProgram_}) {
		if (*program != 0) {
			glDeleteProgram(*program);
			*program = 0;
		}
	}
	if (gridVbo_ != 0) {
		glDeleteBuffers(1, &gridVbo_);
		gridVbo_ = 0;
	}
	for (GLuint* vao : {&gridVao_, &emptyVao_}) {
		if (*vao != 0) {
			glDeleteVertexArrays(1, vao);
			*vao = 0;
		}
	}
	modelLocation_ = -1;
	viewLocation_ = -1;
	projectionLocation_ = -1;
	colorLocation_ = -1;
	shader_.destroy();
}

bool OpenGLRenderAdapter::resizeTarget(RenderTarget& target, int width, int height, bool withDepth, GLenum colorFormat) {
	if (target.fbo != 0 && target.width == width && target.height == height) {
		return true;
	}

	destroyTarget(target);
	target.width = width;
	target.height = height;

	GLint previous = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
	glGenFramebuffers(1, &target.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

	const GLenum dataFormat = colorFormat == GL_R8 ? GL_RED : GL_RGBA;
	glGenTextures(1, &target.color);
	glBindTexture(GL_TEXTURE_2D, target.color);
	glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(colorFormat), width, height, 0, dataFormat, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.color, 0);

	if (withDepth) {
		glGenRenderbuffers(1, &target.depth);
		glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
	}

	const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));

	if (!complete) {
		LOG_ERROR("OpenGLRenderAdapter: framebuffer is incomplete");
		destroyTarget(target);
		return false;
	}
	return true;
}

void OpenGLRenderAdapter::destroyTarget(RenderTarget& target) {
	if (target.depth != 0) {
		glDeleteRenderbuffers(1, &target.depth);
		target.depth = 0;
	}
	if (target.color != 0) {
		glDeleteTextures(1, &target.color);
		target.color = 0;
	}
	if (target.fbo != 0) {
		glDeleteFramebuffers(1, &target.fbo);
		target.fbo = 0;
	}
	target.width = 0;
	target.height = 0;
}

bool OpenGLRenderAdapter::setupMesh(PrimitiveMesh& mesh, const float* vertices, GLsizei vertexCount, GLenum drawMode) {
	glGenVertexArrays(1, &mesh.vao);
	glGenBuffers(1, &mesh.vbo);

	if (mesh.vao == 0 || mesh.vbo == 0) {
		return false;
	}

	glBindVertexArray(mesh.vao);
	glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexCount * 3 * sizeof(float)), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);

	mesh.vertexCount = vertexCount;
	mesh.drawMode = drawMode;
	return true;
}

const OpenGLRenderAdapter::PrimitiveMesh* OpenGLRenderAdapter::getMesh(PrimitiveType primitive) const {
	switch (primitive) {
	case PrimitiveType::Line:
		return &lineMesh_;
	case PrimitiveType::Triangle:
		return &triangleMesh_;
	case PrimitiveType::Quad:
		return &quadMesh_;
	case PrimitiveType::Cube:
		return &cubeMesh_;
	case PrimitiveType::WireSphere:
		return &wireSphereMesh_;
	default:
		return nullptr;
	}
}

bool OpenGLRenderAdapter::compileShader(GLenum type, const std::string& source, GLuint& shaderId, std::string& outError) const {
	const char* sourcePtr = source.c_str();
	shaderId = glCreateShader(type);
	glShaderSource(shaderId, 1, &sourcePtr, nullptr);
	glCompileShader(shaderId);

	GLint success = GL_FALSE;
	glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
	if (success != GL_TRUE) {
		char infoLog[1024] = {};
		glGetShaderInfoLog(shaderId, sizeof(infoLog), nullptr, infoLog);
		outError = infoLog;
		glDeleteShader(shaderId);
		shaderId = 0;
		return false;
	}

	return true;
}
