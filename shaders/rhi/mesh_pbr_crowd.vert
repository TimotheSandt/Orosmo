#version 450
// ANIM-04 : foule instanciée (anim::CrowdRenderer). Skinning dans le vertex shader avec UNE palette d'os par instance : les palettes
// de toutes les instances sont dans un tampon de stockage (groupe 2), l'instance donne sa matrice monde et le début de sa palette.
// Sorties identiques à mesh_pbr.vert (sans AOV) : à associer à mesh_pbr.frag. Même calcul que anim::skinVertices (CPU).
//
// Sommets : liaison 0 = sommets PBR au repos (emplacements 0 à 4), liaison 1 = influences (5 : os, 6 : poids) ;
// instance (liaison 2, pas d'instance) : 8 à 11 = matrice monde, 12.x = premier os de la palette de l'instance.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aColor;
layout(location = 5) in uvec4 aJoints;
layout(location = 6) in vec4 aWeights;

layout(location = 8) in vec4 iModel0;
layout(location = 9) in vec4 iModel1;
layout(location = 10) in vec4 iModel2;
layout(location = 11) in vec4 iModel3;
layout(location = 12) in uvec4 iInfo;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(set = 2, binding = 0, std430) readonly buffer Palettes { mat4 palettes[]; };

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec2 uv;
layout(location = 7) out vec4 tangent;
layout(location = 8) out vec4 vertexColor;

void main()
{
    const uint base = iInfo.x;
    const mat4 skin = palettes[base + aJoints.x] * aWeights.x + palettes[base + aJoints.y] * aWeights.y +
                      palettes[base + aJoints.z] * aWeights.z + palettes[base + aJoints.w] * aWeights.w;
    const mat4 model = mat4(iModel0, iModel1, iModel2, iModel3) * skin;
    crntPos = vec3(model * vec4(aPos, 1.0f));
    // Matrice des normales : mat3 du modèle (exact sans échelle non uniforme, comme le skinning en compute) ; normalisée par le fragment.
    normal = mat3(model) * aNormal;
    tangent = vec4(mat3(model) * aTangent.xyz, aTangent.w);
    uv = aUv;
    vertexColor = aColor;
    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
}
