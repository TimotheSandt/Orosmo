// REN-13 : lecture de l'IBL par les matériaux (groupe 0, liaisons 5 à 7 ajoutées par IblFeature). À inclure après math.glsl,
// pbr.glsl et frame_data.glsl, avec OROSMO_IBL défini (iblDefines()).
#ifndef OROSMO_IBL_GLSL
#define OROSMO_IBL_GLSL

#include "ibl_common.glsl"

#define OROSMO_MAX_LIGHT_PROBES 16

layout(set = 0, binding = 5) uniform samplerCube iblEnvironment;
layout(set = 0, binding = 6) uniform sampler2D iblBrdfLut;
layout(set = 0, binding = 7, std140) uniform IblBlock
{
    vec4 params; // x : intensité diffuse, y : intensité spéculaire, z : dernier niveau de mip, w : nombre de sondes
    vec4 environmentSh[9];
    vec4 probePositions[OROSMO_MAX_LIGHT_PROBES]; // xyz monde, w rayon
    vec4 probeSh[OROSMO_MAX_LIGHT_PROBES * 9];
} ibl;

// Irradiance / π au point `worldPos` (monde) pour la normale `n` : sondes proches mélangées, complétées par l'environnement
// (même formule que ProbeSet::sample).
vec3 iblIrradiance(vec3 worldPos, vec3 n)
{
    float y[9];
    iblShBasis(n, y);
    vec3 probes = vec3(0.0);
    float total = 0.0;
    int count = min(int(ibl.params.w), OROSMO_MAX_LIGHT_PROBES);
    for (int i = 0; i < count; i++)
    {
        vec4 p = ibl.probePositions[i];
        float w = saturate(1.0 - length(worldPos - p.xyz) / p.w);
        w *= w;
        if (w > 0.0)
        {
            vec3 e = vec3(0.0);
            for (int k = 0; k < 9; k++)
            {
                e += ibl.probeSh[i * 9 + k].rgb * y[k];
            }
            probes += w * e;
            total += w;
        }
    }
    vec3 result;
    if (total >= 1.0)
    {
        result = probes / total;
    }
    else
    {
        vec3 env = vec3(0.0);
        for (int k = 0; k < 9; k++)
        {
            env += ibl.environmentSh[k].rgb * y[k];
        }
        result = probes + (1.0 - total) * env;
    }
    return max(result, vec3(0.0));
}

// Lumière renvoyée par l'environnement (split-sum) : diffus SH + spéculaire préfiltré × table BRDF, multipliés par l'occlusion.
vec3 iblShade(vec3 worldPos, vec3 n, vec3 v, vec3 albedo, float metallic, float roughness, float occlusion)
{
    float nDotV = max(dot(n, v), 1e-4);
    vec3 f0 = baseReflectance(albedo, metallic);
    vec2 ab = textureLod(iblBrdfLut, vec2(nDotV, roughness), 0.0).rg;
    vec3 specularColor = f0 * ab.x + ab.y;
    vec3 r = reflect(-v, n);
    vec3 prefiltered = textureLod(iblEnvironment, r, roughness * ibl.params.z).rgb;
    vec3 diffuse = (vec3(1.0) - specularColor) * albedo * (1.0 - metallic) * iblIrradiance(worldPos, n);
    return (diffuse * ibl.params.x + specularColor * prefiltered * ibl.params.y) * occlusion;
}

#endif
