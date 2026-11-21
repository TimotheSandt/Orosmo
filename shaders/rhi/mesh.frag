#version 450
// Éclairage : ambiante + directionnelles (ponctuelles et projecteurs : types réservés, non rendus, comme avant).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer ».

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec3 color;

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
            l += directLight(lght.direction, lght.color, lght.strength);
        }
    }

    return vec4(color, 1.0f) * l;
}

void main()
{
    if (pc.wireframe == 1)
    {
        FragColor = wireframeRender();
    }
    else
    {
        FragColor = fillRender();
    }
}
