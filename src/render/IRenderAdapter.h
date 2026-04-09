#pragma once
#include "math/MathTypes.h"
#include "render/RenderTypes.h"

#include <cstddef>
#include <string>

class IRenderAdapter {
	public:
	virtual ~IRenderAdapter() = default;

	// Create a window and initialize the renderer.
	virtual bool init(int width, int height, const std::string& title) = 0;

	// Check whether the window should keep running.
	virtual bool isRunning() const = 0;

	// Process platform events.
	virtual void pollEvents() = 0;

	// Clear the framebuffer and prepare for drawing.
	virtual void beginFrame(float r, float g, float b) = 0;

	// Draw a primitive using the supplied model transform and color.
	virtual void drawPrimitive(
		PrimitiveType primitive,
		const Mat4& modelMatrix,
		const Vec4& color,
		const Mat4& viewMatrix,
		const Mat4& projectionMatrix) = 0;
	virtual void drawDebugAABB(
		const Vec3& center,
		const Vec3& halfExtents,
		const Vec4& color,
		const Mat4& viewMatrix,
		const Mat4& projectionMatrix) = 0;

	// Upload a mesh to GPU buffers.
	virtual bool uploadMesh(
		const void* vertexData,
		std::size_t vertexStride,
		std::size_t vertexCount,
		const unsigned int* indexData,
		std::size_t indexCount,
		unsigned int& outVao,
		unsigned int& outVbo,
		unsigned int& outEbo) = 0;

	// Create a texture from CPU pixel data.
	virtual bool createTexture(
		int width,
		int height,
		int channels,
		const unsigned char* pixels,
		unsigned int& outTextureId) = 0;

	// Compile and link a shader program from source strings.
	virtual bool createShaderProgram(
		const std::string& vertexSource,
		const std::string& fragmentSource,
		unsigned int& outProgramId,
		std::string& outError) = 0;

	// Destroy GPU resources created through the adapter.
	virtual void destroyMesh(unsigned int& vao, unsigned int& vbo, unsigned int& ebo) = 0;
	virtual void destroyTexture(unsigned int& textureId) = 0;
	virtual void destroyShaderProgram(unsigned int& programId) = 0;

	// Bind mesh rendering state.
	virtual void useShaderProgram(unsigned int programId) = 0;
	virtual void setMatrix4(unsigned int programId, const char* name, const Mat4& value) = 0;
	virtual void setInt(unsigned int programId, const char* name, int value) = 0;
	virtual void setFloat(unsigned int programId, const char* name, float value) = 0;
	virtual void setVec3(unsigned int programId, const char* name, const Vec3& value) = 0;
	virtual void bindTexture2D(unsigned int textureId, unsigned int unit) = 0;
	virtual void drawIndexed(unsigned int vao, unsigned int indexCount) = 0;
	virtual void getFramebufferSize(int& width, int& height) const = 0;

	// Present the rendered frame.
	virtual void endFrame() = 0;

	// Release window and renderer resources.
	virtual void shutdown() = 0;
};
