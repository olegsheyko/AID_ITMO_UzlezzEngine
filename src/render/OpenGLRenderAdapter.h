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
	virtual void drawPrimitive(PrimitiveType primitive, const Mat4& modelMatrix, const Vec4& color) override;
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
	GLint modelLocation_ = -1;
	GLint colorLocation_ = -1;
};
