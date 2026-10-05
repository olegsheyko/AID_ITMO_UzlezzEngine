#pragma once
#include "IRenderAdapter.h"
#include "render/ShaderProgram.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <vector>

class OpenGLRenderAdapter : public IRenderAdapter {
	public:
	virtual ~OpenGLRenderAdapter() override;
	virtual bool init(int width, int height, const std::string& title) override;
	virtual bool isRunning() const override;
	virtual void pollEvents() override;
	virtual void setVSync(bool enabled) override;
	virtual void beginFrame(float r, float g, float b) override;
	using IRenderAdapter::beginViewportFrame;
	using IRenderAdapter::getViewportTextureId;
	virtual void beginViewportFrame(int target, int width, int height, float r, float g, float b) override;
	virtual void endViewportFrame() override;
	virtual unsigned int getViewportTextureId(int target) const override;
	virtual void drawGrid(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, float height) override;
	virtual void drawSky(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, const Vec3& sunDirection) override;
	virtual void beginSelectionMask() override;
	virtual void endSelectionMask(const Vec4& color, float thicknessPixels) override;
	virtual unsigned int selectionMaskProgram() const override { return maskProgram_; }
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
	virtual void drawDebugSphere(
		const Vec3& center,
		float radius,
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
	virtual std::size_t liveTextureCount() const override { return liveTextures_; }
	virtual void useShaderProgram(unsigned int programId) override;
	virtual void setMatrix4(unsigned int programId, const char* name, const Mat4& value) override;
	virtual void setSkinMatrices(unsigned int programId, const Mat4* matrices, std::size_t count) override;
	virtual void setInt(unsigned int programId, const char* name, int value) override;
	virtual void setFloat(unsigned int programId, const char* name, float value) override;
	virtual void setVec3(unsigned int programId, const char* name, const Vec3& value) override;
	virtual void bindTexture2D(unsigned int textureId, unsigned int unit) override;
	virtual void drawIndexed(unsigned int vao, unsigned int indexCount) override;
	virtual void getFramebufferSize(int& width, int& height) const override;
	virtual void endFrame() override;
	virtual void shutdown() override;

	GLFWwindow* getWindow() const { return window_; }

	// Для скриншотов редактора: окно не показывается, кадр целиком рисуется во внеэкранный буфер.
	void setHiddenWindow(bool hidden) { hiddenWindow_ = hidden; }
	void setOffscreenCapture(bool enabled) { captureEnabled_ = enabled; }
	bool readCapturedFrame(std::vector<unsigned char>& outRgba, int& outWidth, int& outHeight) const;

private:
	struct PrimitiveMesh {
		GLuint vao = 0;
		GLuint vbo = 0;
		GLsizei vertexCount = 0;
		GLenum drawMode = GL_TRIANGLES;
	};

	bool createRenderResources();
	void destroyRenderResources();
	struct RenderTarget {
		GLuint fbo = 0;
		GLuint color = 0;
		GLuint depth = 0;
		int width = 0;
		int height = 0;
	};

	bool resizeTarget(RenderTarget& target, int width, int height, bool withDepth, GLenum colorFormat);
	void destroyTarget(RenderTarget& target);
	bool createEditorPrograms();
	GLuint linkProgram(const std::string& vertexSource, const std::string& fragmentSource, const char* name);
	bool setupMesh(PrimitiveMesh& mesh, const float* vertices, GLsizei vertexCount, GLenum drawMode);
	const PrimitiveMesh* getMesh(PrimitiveType primitive) const;
	bool compileShader(GLenum type, const std::string& source, GLuint& shaderId, std::string& outError) const;

	GLFWwindow* window_ = nullptr;
	std::size_t liveTextures_ = 0;
	bool initialized_ = false;
	ShaderProgram shader_;
	PrimitiveMesh lineMesh_;
	PrimitiveMesh triangleMesh_;
	PrimitiveMesh quadMesh_;
	PrimitiveMesh cubeMesh_;
	PrimitiveMesh wireSphereMesh_;
	static constexpr int kMaxViewportTargets = 4;
	std::array<RenderTarget, kMaxViewportTargets> viewportTargets_{};
	RenderTarget maskTarget_;
	RenderTarget captureTarget_;
	int currentTarget_ = 0;
	GLuint skinBuffer_ = 0;
	GLint previousFramebuffer_ = 0;
	bool renderingViewport_ = false;
	bool hiddenWindow_ = false;
	bool captureEnabled_ = false;
	GLuint gridProgram_ = 0;
	GLuint gridVao_ = 0;
	GLuint gridVbo_ = 0;
	GLuint maskProgram_ = 0;
	GLuint outlineProgram_ = 0;
	GLuint skyProgram_ = 0;
	GLuint emptyVao_ = 0;
	GLint modelLocation_ = -1;
	GLint viewLocation_ = -1;
	GLint projectionLocation_ = -1;
	GLint colorLocation_ = -1;
};
