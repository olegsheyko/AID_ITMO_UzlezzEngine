#pragma once
#include "IRenderAdapter.h"
#include "render/ShaderProgram.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class OpenGLRenderAdapter : public IRenderAdapter {
	public:
	virtual ~OpenGLRenderAdapter() override;
	virtual bool init(int width, int height, const std::string& title) override;
	virtual bool isRunning() const override;
	virtual void pollEvents() override;
	virtual void beginFrame(float r, float g, float b) override;
	virtual void beginViewportFrame(int width, int height, float r, float g, float b) override;
	virtual void endViewportFrame() override;
	virtual unsigned int getViewportTextureId() const override;
	virtual void drawPrimitive(
		PrimitiveType primitive,
		const Mat4& modelMatrix,
		const Vec4& color,
		const Mat4& viewMatrix,
		const Mat4& projectionMatrix) override;
	virtual void drawDebugAABB(
		const Vec3& center,
		const Vec3& halfExtents,
		const Vec4& color,
		const Mat4& viewMatrix,
		const Mat4& projectionMatrix) override;
	virtual bool uploadMesh(
		const void* vertexData,
		std::size_t vertexStride,
		std::size_t vertexCount,
		const unsigned int* indexData,
		std::size_t indexCount,
		unsigned int& outVao,
		unsigned int& outVbo,
		unsigned int& outEbo) override;
	virtual bool createTexture(
		int width,
		int height,
		int channels,
		const unsigned char* pixels,
		unsigned int& outTextureId) override;
	virtual bool createShaderProgram(
		const std::string& vertexSource,
		const std::string& fragmentSource,
		unsigned int& outProgramId,
		std::string& outError) override;
	virtual void destroyMesh(unsigned int& vao, unsigned int& vbo, unsigned int& ebo) override;
	virtual void destroyTexture(unsigned int& textureId) override;
	virtual void destroyShaderProgram(unsigned int& programId) override;
	virtual void useShaderProgram(unsigned int programId) override;
	virtual void setMatrix4(unsigned int programId, const char* name, const Mat4& value) override;
	virtual void setInt(unsigned int programId, const char* name, int value) override;
	virtual void setFloat(unsigned int programId, const char* name, float value) override;
	virtual void setVec3(unsigned int programId, const char* name, const Vec3& value) override;
	virtual void bindTexture2D(unsigned int textureId, unsigned int unit) override;
	virtual void drawIndexed(unsigned int vao, unsigned int indexCount) override;
	virtual void getFramebufferSize(int& width, int& height) const override;
	virtual void endFrame() override;
	virtual void shutdown() override;

	GLFWwindow* getWindow() const { return window_; }

private:
	struct PrimitiveMesh {
		GLuint vao = 0;
		GLuint vbo = 0;
		GLsizei vertexCount = 0;
		GLenum drawMode = GL_TRIANGLES;
	};

	bool createRenderResources();
	void destroyRenderResources();
	bool resizeViewportFramebuffer(int width, int height);
	void destroyViewportFramebuffer();
	bool setupMesh(PrimitiveMesh& mesh, const float* vertices, GLsizei vertexCount, GLenum drawMode);
	const PrimitiveMesh* getMesh(PrimitiveType primitive) const;
	bool compileShader(GLenum type, const std::string& source, GLuint& shaderId, std::string& outError) const;

	GLFWwindow* window_ = nullptr;
	bool initialized_ = false;
	ShaderProgram shader_;
	PrimitiveMesh lineMesh_;
	PrimitiveMesh triangleMesh_;
	PrimitiveMesh quadMesh_;
	PrimitiveMesh cubeMesh_;
	GLuint viewportFbo_ = 0;
	GLuint viewportColorTexture_ = 0;
	GLuint viewportDepthRbo_ = 0;
	int viewportWidth_ = 0;
	int viewportHeight_ = 0;
	GLint previousFramebuffer_ = 0;
	bool renderingViewport_ = false;
	GLint modelLocation_ = -1;
	GLint viewLocation_ = -1;
	GLint projectionLocation_ = -1;
	GLint colorLocation_ = -1;
};
