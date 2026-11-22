#version 450
// Éclairage : ambiante + directionnelles (ponctuelles et projecteurs : types réservés, non rendus, comme avant).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer ».

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"
// REN-21 : sorties auxiliaires (macros OROSMO_AOV_* : aovShaderDefines) ; sans elles, rien de plus que la couleur.
#include "aov.glsl"
#ifdef OROSMO_AOV_NORMALS
#include "../include/orosmo/coords.glsl"
#endif
#ifdef OROSMO_AOV_MOTION
#include "frame_data.glsl"
#endif

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec3 color;
#ifdef OROSMO_INSTANCED
// REN-31 : seuil de tramage par instance (lod.hpp) ; les seuils f et -f de deux niveaux couvrent chaque pixel une fois.
layout(location = 3) flat in float dither;
#endif
#ifdef OROSMO_AOV_MOTION
layout(location = 4) in vec4 currentClip;
layout(location = 5) in vec4 previousClip;
#endif
#ifdef OROSMO_AOV_OBJECT_ID
layout(location = 6) flat in uint objectId;
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

void writeAovs()
{
#ifdef OROSMO_AOV_MOTION
    aovMotion = aovMotionVector(currentClip, previousClip, frame.jitter);
#endif
#ifdef OROSMO_AOV_NORMALS
    aovNormals = octEncode(normalize(normal));
#endif
#ifdef OROSMO_AOV_MATERIAL
    aovMaterial = vec2(0.5, 0.0); // rugosité, métal : constantes du matériau par défaut (pas de PBR avant REN-09)
#endif
#ifdef OROSMO_AOV_OBJECT_ID
    aovObjectId = objectId;
#endif
#ifdef OROSMO_AOV_LINEAR_DEPTH
    aovLinearDepth = 1.0 / gl_FragCoord.w; // w de l'espace de découpe = distance le long de l'axe de la vue
#endif
#ifdef OROSMO_AOV_EMISSION
    aovEmission = vec4(0.0); // aucune émission avant REN-09
#endif
#ifdef OROSMO_AOV_ALBEDO
    aovAlbedo = vec4(color, 1.0);
#endif
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
    writeAovs();
}
