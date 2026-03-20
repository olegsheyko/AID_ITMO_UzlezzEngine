#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform vec4 color;
uniform sampler2D baseColorTexture;
uniform bool useBaseColorTexture;

void main() {
    // Сначала просто показываем базовую текстуру без освещения
    vec4 finalColor = color;
    
    if (useBaseColorTexture) {
        finalColor = texture(baseColorTexture, TexCoord);
    }
    
    // Простое освещение
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(Normal);
    float diff = max(dot(normal, lightDir), 0.3); // минимум 0.3 для ambient
    
    FragColor = vec4(finalColor.rgb * diff, finalColor.a);
}
