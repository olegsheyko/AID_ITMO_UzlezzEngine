#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform vec4 color;
uniform sampler2D baseColorTexture;
uniform sampler2D normalTexture;
uniform sampler2D metallicTexture;
uniform sampler2D roughnessTexture;
uniform sampler2D aoTexture;
uniform sampler2D heightTexture;

uniform bool useBaseColorTexture;
uniform bool useNormalTexture;
uniform bool useMetallicTexture;
uniform bool useRoughnessTexture;
uniform bool useAOTexture;
uniform bool useHeightTexture;

const float PI = 3.14159265359;

// Простое освещение (направленный свет)
vec3 calculateLighting(vec3 normal, vec3 albedo, float metallic, float roughness, float ao) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0));
    
    // Ambient
    vec3 ambient = vec3(0.3) * albedo * ao;
    
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * albedo;
    
    // Specular (упрощенный Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), (1.0 - roughness) * 128.0);
    vec3 specular = vec3(spec) * mix(vec3(0.04), albedo, metallic);
    
    return ambient + diffuse + specular;
}

void main() {
    // Base Color
    vec4 baseColor = color;
    if (useBaseColorTexture) {
        vec4 texColor = texture(baseColorTexture, TexCoord);
        // Если текстура загрузилась, используем её
        if (texColor.a > 0.01) {
            baseColor = texColor;
        }
    }
    
    // Normal - используем вершинные нормали (без normal map пока)
    vec3 normal = normalize(Normal);
    
    // Metallic
    float metallic = 0.0;
    if (useMetallicTexture) {
        metallic = texture(metallicTexture, TexCoord).r;
    }
    
    // Roughness
    float roughness = 0.5;
    if (useRoughnessTexture) {
        roughness = texture(roughnessTexture, TexCoord).r;
    }
    
    // Ambient Occlusion
    float ao = 1.0;
    if (useAOTexture) {
        ao = texture(aoTexture, TexCoord).r;
    }
    
    // Вычисляем освещение
    vec3 lighting = calculateLighting(normal, baseColor.rgb, metallic, roughness, ao);
    
    FragColor = vec4(lighting, baseColor.a);
}
