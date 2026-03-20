#pragma once

#include "ResourceTypes.h"
#include <string>

class IRenderAdapter;

// Загрузчик шейдеров
class ShaderLoader {
public:
    static bool load(const std::string& vertexPath, const std::string& fragmentPath, 
                     ShaderData& shaderData, IRenderAdapter* renderer);

private:
    static bool readFile(const std::string& path, std::string& content);
    static bool compileAndLink(ShaderData& shaderData, IRenderAdapter* renderer);
    static bool checkCompileErrors(uint32_t shader, const std::string& type);
    static bool checkLinkErrors(uint32_t program);
};
