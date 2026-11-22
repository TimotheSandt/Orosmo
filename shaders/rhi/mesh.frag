#version 450
// Éclairage : ambiante + directionnelles (ponctuelles et projecteurs : types réservés, non rendus, comme avant).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer ».

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
#ifdef OROSMO_TEXTURED
// R2D-05 : couleur lue dans une texture (groupe 1, binding 0) au lieu de la couleur des sommets ; à associer à mesh_textured.vert.
layout(location = 2) in vec2 uv;
layout(set = 1, binding = 0) uniform sampler2D albedo;
#else
layout(location = 2) in vec3 color;
#endif
#ifdef OROSMO_INSTANCED
// REN-31 : seuil de tramage par instance (lod.hpp) ; les seuils f et -f de deux niveaux couvrent chaque pixel une fois.
layout(location = 3) flat in float dither;
#endif

layout(location = 0) out vec4 FragColor;

#ifdef OROSMO_TRANSPARENT
// REN-32 / REN-37 : matériau à mode de fusion. `mode` suit BlendMode (blend_mode.hpp) : 0 opaque, 1 mask, 2 mask_a2c, 3 dithered,
// 4 blend, 5 additive, 6 premultiplied, 7 transmission. `alpha` est l'opacité de l'objet.
layout(push_constant) uniform Push
{
    int wireframe;
    float alpha;
    float cutoff;
    int mode;
} pc;
#else
layout(push_constant) uniform Push
{
    int wireframe;
} pc;
#endif

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

#ifdef OROSMO_TEXTURED
    return vec4(texture(albedo, uv).rgb, 1.0f) * l;
#else
    return vec4(color, 1.0f) * l;
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
#ifdef OROSMO_TRANSPARENT
    float alpha = pc.alpha;
    if (pc.mode == 1 || pc.mode == 2)
    {
        // Seuil : pixel gardé ou rejeté (alpha-to-coverage, pas encore dans le RHI : mask_a2c se comporte comme mask).
        if (alpha < pc.cutoff)
        {
            discard;
        }
        alpha = 1.0f;
    }
    else if (pc.mode == 3)
    {
        // Tramage de Bayer 4x4 : la part de pixels gardés vaut alpha (lissé ensuite par le TAA).
        const float bayerMask[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        const ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        if ((bayerMask[q.y * 4 + q.x] + 0.5f) / 16.0f > alpha)
        {
            discard;
        }
        alpha = 1.0f;
    }
    else if (pc.mode == 0)
    {
        alpha = 1.0f;
    }
    else if (pc.mode == 6)
    {
        FragColor.rgb *= alpha; // prémultiplié
    }
    FragColor.a = alpha;
#endif
}
