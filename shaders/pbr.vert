#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

// Per-instance attributes (mat4 occupies locations 3-6, material at 7)
layout (location = 3) in mat4 instanceModel;
layout (location = 7) in vec4 instanceMaterial;  // (metallic, roughness, 0, 0)

out vec2 TexCoords;
out vec3 WorldPos;
out vec3 Normal;
flat out vec2 InstanceMaterial;

uniform mat4 projection;
uniform mat4 view;

// Fallback for non-instanced rendering (single sphere)
uniform mat4 model;
uniform mat3 normalMatrix;
uniform int  useInstancing;

void main()
{
    mat4 M = (useInstancing == 1) ? instanceModel : model;

    TexCoords = aTexCoords;
    WorldPos = vec3(M * vec4(aPos, 1.0));

    if (useInstancing == 1) {
        // For translation-only transforms, normal matrix is identity
        Normal = aNormal;
        InstanceMaterial = instanceMaterial.xy;
    } else {
        Normal = normalMatrix * aNormal;
        InstanceMaterial = vec2(-1.0);
    }

    gl_Position = projection * view * vec4(WorldPos, 1.0);
}
