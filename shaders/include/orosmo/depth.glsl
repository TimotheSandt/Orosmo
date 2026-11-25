// Boîte à outils de shaders (REN-22) : profondeur et reconstruction de position.
// Convention du moteur (Camera) : profondeur [0, 1] (zéro-à-un), plans INVERSÉS (reverse-Z : proche = 1, loin = 0), plan
// lointain éventuellement infini. Les fonctions prennent l'inverse de la matrice de projection (glm::inverse(projection))
// et fonctionnent avec n'importe quelle projection zéro-à-un, inversée ou non.
#ifndef OROSMO_DEPTH_GLSL
#define OROSMO_DEPTH_GLSL

#include "coords.glsl"

// Position d'espace vue (RH, la caméra regarde vers -z) depuis l'uv du pixel et la profondeur d'écran du tampon.
vec3 viewPositionFromDepth(vec2 uv, float depth, mat4 inverseProjection)
{
    vec4 ndc = vec4(uvToNdc(uv), depth, 1.0);
    vec4 view = inverseProjection * ndc;
    return view.xyz / view.w;
}

// Position monde : inverseViewProjection = inverse(projection * vue).
vec3 worldPositionFromDepth(vec2 uv, float depth, mat4 inverseViewProjection)
{
    vec4 world = inverseViewProjection * vec4(uvToNdc(uv), depth, 1.0);
    return world.xyz / world.w;
}

// Distance (positive) le long de l'axe de vue depuis la profondeur d'écran, quelle que soit la projection.
float linearDepth(float depth, mat4 inverseProjection)
{
    vec4 view = inverseProjection * vec4(0.0, 0.0, depth, 1.0);
    return -view.z / view.w;
}

// Cas particulier du moteur : plan lointain infini et reverse-Z, profondeur = proche / distance.
float linearDepthInfiniteReverseZ(float depth, float nearPlane) { return depth > 0.0 ? nearPlane / depth : 1.0e30; }

// Cas reverse-Z à plan lointain fini (glm::perspectiveRH_ZO(fov, aspect, far, near)).
float linearDepthReverseZ(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / (nearPlane + depth * (farPlane - nearPlane));
}

#endif
