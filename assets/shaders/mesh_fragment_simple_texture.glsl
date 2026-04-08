#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform sampler2D baseColorTexture;

struct DirectionalLight {
    vec3 direction;
    vec3 color;
    float ambientStrength;
    float diffuseStrength;
};

uniform DirectionalLight light;

void main() {
    vec4 texColor = texture(baseColorTexture, TexCoord);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(norm, lightDir), 0.0);

    vec3 ambient = light.ambientStrength * texColor.rgb * light.color;
    vec3 diffuse = light.diffuseStrength * diff * texColor.rgb * light.color;
    vec3 finalColor = ambient + diffuse;

    FragColor = vec4(finalColor, texColor.a);
}
