#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform sampler2D baseColorTexture;

void main() {
    // Показываем UV координаты как цвета для отладки
    FragColor = vec4(TexCoord.x, TexCoord.y, 0.0, 1.0);
}
