#version 450
// Maillage éclairé et texturé (R2D-05 : 2D vers texture affichée sur un objet 3D). Attributs : position (0), normale (1), uv (2) ;
// à associer à mesh.frag compilé avec OROSMO_TEXTURED (texture de couleur au groupe 1, binding 0). Groupe 0 : image ; groupe 2 : objet.
// Avec les macros d'AOV (SceneRenderer::aovShaderDefines), mêmes sorties que mesh.vert (mouvement, identifiant d'objet).

#ifdef OROSMO_AOV_MOTION
#include "frame_data.glsl"
#endif

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

// Même bloc que mesh.vert (ModelBlock de mesh.cpp, 208 octets).
layout(set = 2, binding = 0, std140) uniform ModelBlock
{
    mat4 model;
    mat4 normalMatrix;
    mat4 prevModel;
    uvec4 info; // x : identifiant d'objet
} model;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec2 uv;
#ifdef OROSMO_AOV_MOTION
layout(location = 4) out vec4 currentClip;
layout(location = 5) out vec4 previousClip;
#endif
#ifdef OROSMO_AOV_OBJECT_ID
layout(location = 6) flat out uint objectId;
#endif

void main()
{
    crntPos = vec3(model.model * vec4(aPos, 1.0f));
    normal = mat3(model.normalMatrix) * aNormal;
    uv = aUv;
    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
#ifdef OROSMO_AOV_MOTION
    currentClip = gl_Position;
    previousClip = frame.prevViewProj * (model.prevModel * vec4(aPos, 1.0f));
#endif
#ifdef OROSMO_AOV_OBJECT_ID
    objectId = model.info.x;
#endif
}
