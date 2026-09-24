#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in ivec4 aBoneIds;
layout (location = 4) in vec4 aBoneWeights;

layout (std140) uniform SkinPalette { mat4 bones[128]; };
uniform bool useSkinning;
uniform mat4 meshNodeTransform;

out vec3 Normal;
out vec2 TexCoord;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    mat4 deformation = meshNodeTransform;
    if (useSkinning && dot(aBoneWeights, vec4(1.0)) > 0.0) {
        deformation = bones[aBoneIds.x] * aBoneWeights.x
                    + bones[aBoneIds.y] * aBoneWeights.y
                    + bones[aBoneIds.z] * aBoneWeights.z
                    + bones[aBoneIds.w] * aBoneWeights.w;
    }
    mat4 worldTransform = model * deformation;
    mat3 normalTransform = mat3(worldTransform);
    // Blended transforms may be singular for pathological poses; avoid NaNs.
    Normal = abs(determinant(normalTransform)) > 0.000001
        ? transpose(inverse(normalTransform)) * aNormal : mat3(model) * aNormal;
    TexCoord = aTexCoord;
    vec4 worldPos = worldTransform * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
}
