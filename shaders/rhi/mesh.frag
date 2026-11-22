#version 450
// Éclairage : ambiante, directionnelles, ponctuelles et projecteurs (REN-05, lights.glsl).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer » (1), ou 2 dans la
// pré-passe de profondeur de SceneRenderer (couleur masquée : le programme peut sauter son éclairage).
// Variantes : OROSMO_FORWARD_PLUS (REN-04) lit les lumières du cluster du fragment (clusters.glsl, ForwardPlusLighting) au lieu de
// toutes les lumières ; OROSMO_CLUSTER_DEBUG (avec OROSMO_FORWARD_PLUS) affiche le nombre de lumières par cluster.

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"
#include "../include/orosmo/lights.glsl"
#include "frame_data.glsl"
#include "light_block.glsl"
#ifdef OROSMO_FORWARD_PLUS
#include "clusters.glsl"
#endif

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec3 color;
#ifdef OROSMO_INSTANCED
// REN-31 : seuil de tramage par instance (lod.hpp) ; les seuils f et -f de deux niveaux couvrent chaque pixel une fois.
layout(location = 3) flat in float dither;
#endif

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Push
{
    int wireframe;
} pc;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

vec4 directLight(vec3 lightDirection, vec3 lightColor, float lightIntensity)
{
    // lumière diffuse
    vec3 norm = normalize(normal);
    lightDirection = normalize(lightDirection);
    float diffuse = saturate(dot(norm, lightDirection));

    // lumière spéculaire
    vec3 viexDirection = normalize(camera.position - crntPos);
    vec3 reflectionDirection = reflect(-lightDirection, norm);
    float specAmount = pow(saturate(dot(viexDirection, reflectionDirection)), 16);
    float specularLight = 0.5f;
    float specular = specAmount * specularLight;

    return vec4(lightColor, 1.0f) * (diffuse + specular) * lightIntensity;
}

vec4 wireframeRender()
{
    return vec4(0.0f);
}

// Apport de la lumière i (même fonction en forward et en forward+ : même image).
vec4 lightContribution(int i)
{
    LightStruct lght = lights.lights[i];
    if (lght.type == 1)
    {
        return vec4(lght.color * lght.strength, 1.0f);
    }
    if (lght.type == 2)
    {
        return directLight(lght.direction, lght.color, lght.strength);
    }
    if (lght.type == 3 || lght.type == 4)
    {
        // Positions des lumières en coordonnées monde, ramenées dans l'espace des shaders (rendu relatif à la caméra, REN-02).
        vec3 offset = (frame.cameraWorldHigh.xyz + frame.cameraWorldLow.xyz) - frame.cameraRelativeOrigin.xyz;
        const bool spot = lght.type == 4;
        return vec4(localLightRadiance(lght.position - offset, lght.color, lght.strength, lght.a, lght.b, crntPos, normal, camera.position,
                                       spot ? 64.0f : 16.0f, lght.direction, lght.outerCone, lght.innerCone, spot),
                    0.0f);
    }
    return vec4(0.0f);
}

vec4 fillRender()
{
    vec4 l = vec4(lights.AmbientLightColor.rgb * lights.AmbientLightStrength, 1.0f);
#ifdef OROSMO_FORWARD_PLUS
    const uint cluster = clusterIndexAt(crntPos);
#ifdef OROSMO_CLUSTER_DEBUG
    return vec4(clusterHeatColor(cluster), 1.0f);
#endif
    const uint count = clusterLightCount(cluster);
    for (uint k = 0u; k < count; k++)
    {
        l += lightContribution(int(clusterLight(cluster, k)));
    }
#else
    for (int i = 0; i < lights.size; i++)
    {
        l += lightContribution(i);
    }
#endif

    return vec4(color, 1.0f) * l;
}

void main()
{
#ifdef OROSMO_INSTANCED
    if (dither != 0.0f)
    {
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        const ivec2 p = ivec2(gl_FragCoord.xy) & 3;
        const float n = bayer[p.y * 4 + p.x] / 16.0f;
        if ((dither > 0.0f && n < dither) || (dither < 0.0f && n >= -dither))
        {
            discard;
        }
    }
#endif
    if (pc.wireframe == 2)
    {
        FragColor = vec4(0.0f); // pré-passe de profondeur (REN-04) : couleur masquée, aucun éclairage à calculer
    }
    else if (pc.wireframe == 1)
    {
        FragColor = wireframeRender();
    }
    else
    {
        FragColor = fillRender();
    }
}
