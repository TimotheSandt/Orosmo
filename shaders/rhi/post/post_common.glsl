// REN-17, REN-18 : fonctions des post-traitements HDR (tonemapping, histogramme, vignettage). Mêmes formules que les références
// CPU de include/orosmo/renderer/tonemap.hpp (src/renderer/post/tonemap.cpp) : tests/renderer/post_effects_tests.cpp compare les
// deux. Fonctions pures, sans liaison.
#ifndef OROSMO_POST_COMMON_GLSL
#define OROSMO_POST_COMMON_GLSL

#include "../../include/orosmo/math.glsl"

// Valeurs de orosmo::renderer::Tonemapper.
const int kTonemapClamp = 0;
const int kTonemapAces = 1;
const int kTonemapAgx = 2;
const int kTonemapReinhardExtended = 3;

vec3 tonemapAces(vec3 color)
{
    const mat3 inputMat = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outputMat = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    vec3 v = inputMat * color;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return saturate(outputMat * (a / b));
}

vec3 tonemapAgx(vec3 color)
{
    const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336,
                            0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417,
                             -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 v = inset * max(color, vec3(1e-10));
    v = clamp(log2(v), vec3(minEv), vec3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    vec3 x2 = v * v;
    vec3 x4 = x2 * x2;
    v = 15.5 * x4 * x2 - 40.14 * x4 * v + 31.96 * x4 - 6.868 * x2 * v + 0.4298 * x2 + 0.1191 * v - 0.00232;
    v = outset * v;
    return saturate(pow(max(v, vec3(0.0)), vec3(2.2)));
}

vec3 tonemapReinhardExtended(vec3 color, float whitePoint)
{
    float l = luminance(color);
    if (l <= 1e-6)
    {
        return vec3(0.0);
    }
    float w = max(whitePoint, 1e-3);
    float mapped = l * (1.0 + l / (w * w)) / (1.0 + l);
    return saturate(color * (mapped / l));
}

vec3 tonemap(int op, vec3 color, float whitePoint)
{
    vec3 c = max(color, vec3(0.0));
    if (op == kTonemapAces)
        return tonemapAces(c);
    if (op == kTonemapAgx)
        return tonemapAgx(c);
    if (op == kTonemapReinhardExtended)
        return tonemapReinhardExtended(c, whitePoint);
    return saturate(c);
}

// Case d'histogramme (256 cases) : 0 sous exp2(minLog2), puis 1 à 255 sur [minLog2, maxLog2] en log2.
uint histogramBin(float lum, float minLog2, float maxLog2)
{
    if (!(lum >= exp2(minLog2)))
    {
        return 0u;
    }
    float t = saturate((log2(lum) - minLog2) / (maxLog2 - minLog2));
    return uint(t * 254.0 + 1.0);
}

// Vignettage : facteur multiplicatif ; settings = (intensité, rayon, douceur, rondeur).
float vignetteFactor(vec2 uv, float aspect, vec4 settings)
{
    float ax = 1.0 + (aspect - 1.0) * saturate(settings.w);
    vec2 v = (uv - 0.5) * 2.0;
    v.x *= ax;
    float d = length(v) / length(vec2(ax, 1.0));
    float t = smoothstep(settings.y, settings.y + max(settings.z, 1e-4), d);
    return 1.0 - saturate(settings.x) * t;
}

#endif
