#pragma once

#include "math/MathTypes.h"

#include <cstddef>
#include <string>

struct MeshData;

namespace MeshBounds {
std::size_t vertexCount(const MeshData& data);
// Габариты меша в позе привязки — так же, как его рисует RenderSystem без анимации.
bool computeBindPose(const MeshData& data, Vec3& outMin, Vec3& outMax);
// Модель из файла (через Assimp, Y-up), а не процедурный примитив движка.
bool isImportedModel(const std::string& path);
} // namespace MeshBounds
