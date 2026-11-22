// Boîte à outils de shaders (REN-22) : hachage et bruits. Déterministes : mêmes entrées, mêmes sorties sur tous les
// backends (hachage entier PCG, aucune horloge, aucune texture). Sorties dans [0, 1] sauf mention contraire.
#ifndef OROSMO_NOISE_GLSL
#define OROSMO_NOISE_GLSL

#include "math.glsl"

// Hachage entier PCG (Jarzynski et Olano). Référence CPU identique dans tests/renderer/shader_toolbox_tests.cpp.
uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint pcgHash(uvec2 v) { return pcgHash(v.x ^ pcgHash(v.y)); }
uint pcgHash(uvec3 v) { return pcgHash(v.x ^ pcgHash(v.y ^ pcgHash(v.z))); }

// uint -> float dans [0, 1) : 24 bits de mantisse, exact.
float hashToUnit(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }

float hash11(uint x) { return hashToUnit(pcgHash(x)); }
float hash21(ivec2 p) { return hashToUnit(pcgHash(uvec2(p))); }
float hash31(ivec3 p) { return hashToUnit(pcgHash(uvec3(p))); }

// Bruit de valeur 2D, interpolation quintique : vaut hash21 aux coins entiers.
float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    ivec2 c = ivec2(i);
    float a = hash21(c);
    float b = hash21(c + ivec2(1, 0));
    float d = hash21(c + ivec2(0, 1));
    float e = hash21(c + ivec2(1, 1));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

// Bruit de gradient (Perlin) 2D, sortie dans [-1, 1] ; vaut 0 aux coins entiers.
vec2 gradientAt(ivec2 c)
{
    float angle = hash21(c) * kTwoPi;
    return vec2(cos(angle), sin(angle));
}

float gradientNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    ivec2 c = ivec2(i);
    float a = dot(gradientAt(c), f);
    float b = dot(gradientAt(c + ivec2(1, 0)), f - vec2(1.0, 0.0));
    float d = dot(gradientAt(c + ivec2(0, 1)), f - vec2(0.0, 1.0));
    float e = dot(gradientAt(c + ivec2(1, 1)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y) * 1.4142135; // normalisation approximative vers [-1, 1]
}

// Mouvement brownien fractionnaire de bruit de valeur : somme normalisée, sortie dans [0, 1].
float fbm(vec2 p, int octaves, float lacunarity, float gain)
{
    float sum = 0.0;
    float amp = 1.0;
    float norm = 0.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * valueNoise(p);
        norm += amp;
        p *= lacunarity;
        amp *= gain;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

// Bruit de gradient entrelacé (Jimenez) : tramage d'écran peu coûteux, dans [0, 1).
float interleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif
