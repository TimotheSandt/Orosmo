#version 450
// Maillage éclairé (RHI, GLSL 450). Groupe 0 : image (caméra, lumières) ; groupe 2 : objet (matrice du modèle).

#ifdef OROSMO_AOV_MOTION
#include "frame_data.glsl"
#endif

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
    mat4 prevModel;    // modèle de l'image précédente (vecteurs de mouvement ; égal à `model` pour un objet immobile)
    uvec4 info;        // x : identifiant d'objet (Mesh::setObjectId)
} model;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec3 color;
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
    normal = mat3(model.normalMatrix) * aNormal; // dans l'espace monde, comme la position (normalisée par mesh.frag)
    color = aColor;

    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
#ifdef OROSMO_AOV_MOTION
    currentClip = gl_Position;
    previousClip = frame.prevViewProj * (model.prevModel * vec4(aPos, 1.0f));
#endif
#ifdef OROSMO_AOV_OBJECT_ID
    objectId = model.info.x;
#endif
}
