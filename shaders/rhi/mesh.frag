#version 450
// Éclairage : ambiante + directionnelles (ponctuelles et projecteurs : types réservés, non rendus, comme avant).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer ».

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"

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

struct LightStruct
{
    int type;

    vec3 position;
    vec3 direction;
    vec3 color;

    float strength;

    float outerCone;
    float innerCone;
    float a;
    float b;
    float _pad[3];
};

layout(set = 0, binding = 1, std430) buffer LightBlock
{
    int size;

    vec4 AmbientLightColor;
    float AmbientLightStrength;

    LightStruct lights[];
} lights;

// REN-06 à REN-08 : ombres (variante OROSMO_SHADOWS : groupe 1, bindings 6 et 7, voir shadow_receiver.glsl). Sans elle : 1.
#ifdef OROSMO_SHADOWS
#include "shadow_receiver.glsl"
#else
float orosmoLightShadow(int lightIndex, vec3 position, vec3 n, vec3 toLight, vec3 eye)
{
    return 1.0;
}
#endif

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

vec4 fillRender()
{
    vec4 l = vec4(lights.AmbientLightColor.rgb * lights.AmbientLightStrength, 1.0f);
    for (int i = 0; i < lights.size; i++)
    {
        LightStruct lght = lights.lights[i];
        if (lght.type == 1)
        {
            l += vec4(lght.color * lght.strength, 1.0f);
        }
        else if (lght.type == 2)
        {
            l += directLight(lght.direction, lght.color, lght.strength) *
                 orosmoLightShadow(i, crntPos, normalize(normal), normalize(lght.direction), camera.position);
        }
    }

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
    if (pc.wireframe == 1)
    {
        FragColor = wireframeRender();
    }
    else
    {
        FragColor = fillRender();
    }
}
