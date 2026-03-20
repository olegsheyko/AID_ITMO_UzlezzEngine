#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform vec4 color;
uniform sampler2D baseColorTexture;
uniform bool useBaseColorTexture;

void main() {
    // DEBUG: Просто показываем текстуру без освещения
    if (useBaseColorTexture) {
        FragColor = texture(baseColorTexture, TexCoord);
    } else {
        // Если текстуры нет, показываем красный цвет (не нормали!)
        FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    }
}
