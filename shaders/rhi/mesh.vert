#version 450
// Maillage éclairé (RHI, GLSL 450). Groupe 0 : image (caméra, lumières) ; groupe 2 : objet (matrice du modèle).

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(set = 2, binding = 0, std140) uniform ModelBlock
{
    mat4 model;
    mat4 normalMatrix; // inverse transposée de la partie 3x3 du modèle, calculée par objet (Mesh::uploadTransform)
} model;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec3 color;

void main()
{
    crntPos = vec3(model.model * vec4(aPos, 1.0f));
    normal = mat3(model.normalMatrix) * aNormal; // dans l'espace monde, comme la position (normalisée par mesh.frag)
    color = aColor;

    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
}
