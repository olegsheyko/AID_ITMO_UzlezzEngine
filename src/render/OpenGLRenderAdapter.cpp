#include "render/OpenGLRenderAdapter.h"
#include "core/Logger.h"

#include <algorithm>

namespace {
void framebufferSizeCallback(GLFWwindow*, int width, int height) {
	glViewport(0, 0, width, height);
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
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

	window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	if (!window_) {
		LOG_ERROR("OpenGLRenderAdapter: Failed to create GLFW window");
		shutdown();
		return false;
	}

	if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
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

void OpenGLRenderAdapter::pollEvents() {
	if (initialized_) {
		glfwPollEvents();
	}
}

void OpenGLRenderAdapter::beginFrame(float r, float g, float b) {
	if (window_) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		glViewport(0, 0, width, height);
	}

	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderAdapter::beginViewportFrame(int width, int height, float r, float g, float b) {
	width = std::max(1, width);
	height = std::max(1, height);

	if (!resizeViewportFramebuffer(width, height)) {
		return;
	}

	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer_);
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo_);
	glViewport(0, 0, viewportWidth_, viewportHeight_);
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

unsigned int OpenGLRenderAdapter::getViewportTextureId() const {
	return viewportColorTexture_;
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
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

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
		width = viewportWidth_;
		height = viewportHeight_;
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
	destroyViewportFramebuffer();

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

	return true;
}

void OpenGLRenderAdapter::destroyRenderResources() {
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
	modelLocation_ = -1;
	viewLocation_ = -1;
	projectionLocation_ = -1;
	colorLocation_ = -1;
	shader_.destroy();
}

bool OpenGLRenderAdapter::resizeViewportFramebuffer(int width, int height) {
	if (viewportFbo_ != 0 && viewportWidth_ == width && viewportHeight_ == height) {
		return true;
	}

	destroyViewportFramebuffer();

	viewportWidth_ = width;
	viewportHeight_ = height;

	glGenFramebuffers(1, &viewportFbo_);
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo_);

	glGenTextures(1, &viewportColorTexture_);
	glBindTexture(GL_TEXTURE_2D, viewportColorTexture_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportWidth_, viewportHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportColorTexture_, 0);

	glGenRenderbuffers(1, &viewportDepthRbo_);
	glBindRenderbuffer(GL_RENDERBUFFER, viewportDepthRbo_);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth_, viewportHeight_);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, viewportDepthRbo_);

	const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!complete) {
		LOG_ERROR("OpenGLRenderAdapter: Viewport framebuffer is incomplete");
		destroyViewportFramebuffer();
		return false;
	}

	return true;
}

void OpenGLRenderAdapter::destroyViewportFramebuffer() {
	if (viewportDepthRbo_ != 0) {
		glDeleteRenderbuffers(1, &viewportDepthRbo_);
		viewportDepthRbo_ = 0;
	}
	if (viewportColorTexture_ != 0) {
		glDeleteTextures(1, &viewportColorTexture_);
		viewportColorTexture_ = 0;
	}
	if (viewportFbo_ != 0) {
		glDeleteFramebuffers(1, &viewportFbo_);
		viewportFbo_ = 0;
	}
	viewportWidth_ = 0;
	viewportHeight_ = 0;
	renderingViewport_ = false;
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
