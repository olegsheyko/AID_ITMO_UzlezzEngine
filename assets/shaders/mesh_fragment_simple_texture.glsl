#version 330 core

in vec3 Normal;
in vec2 TexCoord;
in vec3 FragPos;

out vec4 FragColor;

uniform sampler2D baseColorTexture;

void main() {
    vec4 texColor = texture(baseColorTexture, TexCoord);
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), 0.0);

    vec3 ambient = 0.3 * texColor.rgb;
    vec3 diffuse = diff * texColor.rgb;

    FragColor = vec4(ambient + diffuse, texColor.a);
}
