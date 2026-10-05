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

	// Enable or disable waiting for the display refresh on present.
	virtual void setVSync(bool enabled) = 0;

	// Clear the framebuffer and prepare for drawing.
	virtual void beginFrame(float r, float g, float b) = 0;
	// Внеэкранные цели вьюпортов редактора (Scene View, Game View...). Цель 0 — по умолчанию.
	virtual void beginViewportFrame(int target, int width, int height, float r, float g, float b) = 0;
	void beginViewportFrame(int width, int height, float r, float g, float b) {
		beginViewportFrame(0, width, height, r, g, b);
	}
	virtual void endViewportFrame() = 0;
	virtual unsigned int getViewportTextureId(int target) const = 0;
	unsigned int getViewportTextureId() const { return getViewportTextureId(0); }

	// Бесконечная сетка редактора на плоскости z = height, рисуется в текущую цель с тестом глубины.
	virtual void drawGrid(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, float height) = 0;

	// Процедурное небо на весь кадр вьюпорта: градиент зенит — горизонт — земля и солнце по направлению света.
	// Рисуется первым, без записи глубины. Без поддержки — ничего не делает.
	virtual void drawSky(const Mat4& viewMatrix, const Mat4& projectionMatrix, const Vec3& cameraPosition, const Vec3& sunDirection) {
		(void)viewMatrix; (void)projectionMatrix; (void)cameraPosition; (void)sunDirection;
	}

	// Копия текущего содержимого цели вьюпорта в новую текстуру — для миниатюр. 0 — не поддерживается.
	// Текстуру потом освобождают через destroyTexture.
	virtual unsigned int copyViewportTexture(int target) { (void)target; return 0; }

	// Контур выделения: между begin/end выделенные меши рисуются программой selectionMaskProgram(),
	// end обводит получившуюся маску. Работает внутри кадра вьюпорта; без поддержки — ничего не делает.
	virtual void beginSelectionMask() {}
	virtual void endSelectionMask(const Vec4& color, float thicknessPixels) { (void)color; (void)thicknessPixels; }
	virtual unsigned int selectionMaskProgram() const { return 0; }

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
	virtual void drawDebugSphere(
		const Vec3& center,
		float radius,
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

	// Текстуры, созданные createTexture и ещё не удалённые, — датчик утечки GPU-памяти для стресс-теста.
	virtual std::size_t liveTextureCount() const = 0;

	// Bind mesh rendering state.
	virtual void useShaderProgram(unsigned int programId) = 0;
	virtual void setMatrix4(unsigned int programId, const char* name, const Mat4& value) = 0;
	virtual void setSkinMatrices(unsigned int programId, const Mat4* matrices, std::size_t count) = 0;
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
