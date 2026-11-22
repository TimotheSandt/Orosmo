// REN-06 à REN-08 : lecture des ombres par un matériau (récepteur). À inclure par un fragment shader qui définit
// OROSMO_SHADOWS (mesh.frag le fait). Le groupe 1 du matériau porte l'atlas (binding 6, échantillonneur de comparaison) et le bloc
// des ombres (binding 7) : ShadowFeature::receiverGroup(), ou ShadowFeature::appendReceiverBindings dans le groupe du matériau.
// Disposition std140 identique à orosmo::renderer::ShadowGpuData (shadow_feature.hpp, vérifiée par static_assert).
// Positions dans le repère des shaders (monde, ou relatif à la caméra en rendu relatif : les matrices en tiennent compte).
#ifndef OROSMO_SHADOW_RECEIVER_GLSL
#define OROSMO_SHADOW_RECEIVER_GLSL

#include "../include/orosmo/shadow.glsl"

#define OROSMO_SHADOW_CASCADES 4
#define OROSMO_SHADOW_SPOTS 8

layout(set = 1, binding = 6) uniform sampler2DShadow orosmoShadowAtlas;

layout(set = 1, binding = 7, std140) uniform OrosmoShadowBlock
{
    mat4 cascadeMatrix[OROSMO_SHADOW_CASCADES]; // repère des shaders -> (u, v, profondeur) dans l'atlas
    vec4 cascadeRect[OROSMO_SHADOW_CASCADES];   // tuile : u0, v0, u1, v1
    vec4 cascadeParams[OROSMO_SHADOW_CASCADES]; // x : texel (m), y : 1 / profondeur couverte (m), z : 1 si dessinée
    vec4 directional;                           // x : index de la lumière (-1 : aucune), y : cascades, z, w : fondu (m)
    vec4 filtering;                                // x : rayon PCF, y : biais constant, z : biais de pente, w : décalage normal (texels)
    vec4 atlas;                                 // x : 1 / taille, y : taille, z : projecteurs
    mat4 spotMatrix[OROSMO_SHADOW_SPOTS];       // repère des shaders -> clip (u, v après division par w)
    vec4 spotRect[OROSMO_SHADOW_SPOTS];
    vec4 spotParams[OROSMO_SHADOW_SPOTS];       // x : index de la lumière, y : proche, z : lointain, w : texel à 1 m
    vec4 spotPosition[OROSMO_SHADOW_SPOTS];     // xyz : position (repère des shaders), w : début du fondu (distance à la caméra)
    vec4 spotDirection[OROSMO_SHADOW_SPOTS];    // xyz : direction, w : fin du fondu
} orosmoShadows;

bool orosmoInsideTile(vec2 uv, vec4 rect, float margin)
{
    return all(greaterThanEqual(uv, rect.xy + vec2(margin))) && all(lessThanEqual(uv, rect.zw - vec2(margin)));
}

float orosmoFade(float distanceToEye, float fadeStart, float fadeEnd)
{
    return saturate((fadeEnd - distanceToEye) / max(fadeEnd - fadeStart, 1e-4));
}

// Visibilité (1 = éclairé) de la lumière `lightIndex` (index dans le tampon des lumières) au point `position`, de normale `n`
// (normalisée) ; `toLight` : vers la lumière (directionnelle) ; `eye` : position de la caméra (repère des shaders).
float orosmoLightShadow(int lightIndex, vec3 position, vec3 n, vec3 toLight, vec3 eye)
{
    const int radius = int(orosmoShadows.filtering.x);
    const float margin = (float(radius) + 1.0) * orosmoShadows.atlas.x;
    const vec2 texel = vec2(orosmoShadows.atlas.x);
    const float distanceToEye = length(position - eye);
    if (lightIndex == int(orosmoShadows.directional.x))
    {
        const float fade = orosmoFade(distanceToEye, orosmoShadows.directional.z, orosmoShadows.directional.w);
        if (fade <= 0.0)
        {
            return 1.0;
        }
        const float nDotL = saturate(dot(n, toLight));
        const int count = int(orosmoShadows.directional.y);
        // Première cascade (la plus fine) qui contient le point, PCF compris ; une cascade pas encore dessinée est sautée.
        for (int c = 0; c < count; ++c)
        {
            const vec4 params = orosmoShadows.cascadeParams[c];
            if (params.z == 0.0)
            {
                continue;
            }
            const vec3 p = position + n * (orosmoShadows.filtering.w * params.x);
            vec3 coord = (orosmoShadows.cascadeMatrix[c] * vec4(p, 1.0)).xyz;
            if (!orosmoInsideTile(coord.xy, orosmoShadows.cascadeRect[c], margin) || coord.z < 0.0 || coord.z > 1.0)
            {
                continue;
            }
            coord.z -= shadowSlopeBias(nDotL, orosmoShadows.filtering.y, orosmoShadows.filtering.z) * params.x * params.y;
            return mix(1.0, shadowPcf(orosmoShadowAtlas, coord, texel, radius), fade);
        }
        return 1.0;
    }
    const int spots = int(orosmoShadows.atlas.z);
    for (int s = 0; s < spots; ++s)
    {
        if (int(orosmoShadows.spotParams[s].x) != lightIndex)
        {
            continue;
        }
        const vec4 params = orosmoShadows.spotParams[s];
        const vec3 origin = orosmoShadows.spotPosition[s].xyz;
        const vec3 axis = orosmoShadows.spotDirection[s].xyz;
        const float fade = orosmoFade(length(origin - eye), orosmoShadows.spotPosition[s].w, orosmoShadows.spotDirection[s].w);
        const float depth = dot(position - origin, axis);
        if (fade <= 0.0 || depth <= params.y)
        {
            return 1.0;
        }
        const float texelWorld = params.w * depth;
        const float nDotL = saturate(dot(n, normalize(origin - position)));
        const vec3 p = position + n * (orosmoShadows.filtering.w * texelWorld);
        const vec4 clip = orosmoShadows.spotMatrix[s] * vec4(p, 1.0);
        if (clip.w <= 0.0)
        {
            return 1.0;
        }
        const vec2 uv = clip.xy / clip.w;
        if (!orosmoInsideTile(uv, orosmoShadows.spotRect[s], margin))
        {
            return 1.0;
        }
        // Profondeur du récepteur recalculée depuis la distance (linéaire), biais en mètres, puis remise en profondeur [0, 1].
        const float d = max(dot(p - origin, axis) - shadowSlopeBias(nDotL, orosmoShadows.filtering.y, orosmoShadows.filtering.z) * texelWorld,
                            params.y);
        const float ref = params.z * (d - params.y) / (d * (params.z - params.y));
        return mix(1.0, shadowPcf(orosmoShadowAtlas, vec3(uv, ref), texel, radius), fade);
    }
    return 1.0;
}

#endif
