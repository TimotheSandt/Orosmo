#version 450
// Maillage éclairé et texturé (R2D-05 : 2D vers texture affichée sur un objet 3D). Attributs : position (0), normale (1), uv (2) ;
// à associer à mesh.frag compilé avec OROSMO_TEXTURED (texture de couleur au groupe 1, binding 0). Groupe 0 : image ; groupe 2 : objet.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(set = 2, binding = 0, std140) uniform ModelBlock
{
    mat4 model;
    mat4 normalMatrix;
} model;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec2 uv;

void main()
{
    crntPos = vec3(model.model * vec4(aPos, 1.0f));
    normal = mat3(model.normalMatrix) * aNormal;
    uv = aUv;
    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
}
