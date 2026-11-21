// Boîte à outils de shaders (REN-22) : BRDF physique (GGX / Smith / Schlick), flux de travail métallique-rugosité.
// Toutes les directions sont unitaires, dans le même espace ; l est vers la lumière, v vers l'œil.
#ifndef OROSMO_PBR_GLSL
#define OROSMO_PBR_GLSL

#include "math.glsl"

// Distribution normale GGX (Trowbridge-Reitz), alpha = rugosité^2.
float distributionGGX(float nDotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Visibilité de Smith-GGX corrélée à la hauteur : V = G / (4 nDotL nDotV).
float visibilitySmithGGXCorrelated(float nDotV, float nDotL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    float sum = gv + gl;
    return sum > 0.0 ? 0.5 / sum : 0.0;
}

// Fresnel de Schlick.
vec3 fresnelSchlick(float vDotH, vec3 f0) { return f0 + (1.0 - f0) * pow5(1.0 - vDotH); }
vec3 fresnelSchlick(float vDotH, vec3 f0, float f90) { return f0 + (vec3(f90) - f0) * pow5(1.0 - vDotH); }

// Réflectance à incidence normale : 0.04 pour un diélectrique, albedo pour un métal.
vec3 baseReflectance(vec3 albedo, float metallic) { return mix(vec3(0.04), albedo, metallic); }

// Diffus de Lambert, albedo atténué par le métal.
vec3 diffuseLambert(vec3 albedo, float metallic) { return albedo * (1.0 - metallic) * kInvPi; }

// BRDF complète (diffus Lambert + spéculaire Cook-Torrance GGX) multipliée par nDotL : à multiplier par la radiance incidente.
// roughness est la rugosité perceptuelle (alpha = roughness^2, bornée pour éviter les pics dégénérés).
vec3 brdfGGX(vec3 n, vec3 v, vec3 l, vec3 albedo, float metallic, float roughness)
{
    float nDotL = dot(n, l);
    if (nDotL <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 h = normalize(v + l);
    float nDotV = max(dot(n, v), 1e-4);
    float nDotH = saturate(dot(n, h));
    float vDotH = saturate(dot(v, h));
    float alpha = max(roughness * roughness, 0.002);
    vec3 f = fresnelSchlick(vDotH, baseReflectance(albedo, metallic));
    float d = distributionGGX(nDotH, alpha);
    float vis = visibilitySmithGGXCorrelated(nDotV, nDotL, alpha);
    vec3 specular = f * (d * vis);
    vec3 diffuse = (vec3(1.0) - f) * diffuseLambert(albedo, metallic);
    return (diffuse + specular) * nDotL;
}

#endif
