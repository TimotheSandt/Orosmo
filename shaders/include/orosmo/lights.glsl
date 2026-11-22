// REN-05 : lumières ponctuelles et projecteurs, partagées par le forward et le forward+ (REN-04) : mêmes formules, donc même image
// dans les deux modes. Fonctions pures (aucune liaison) ; références CPU : `orosmo::renderer::lightRange` et
// `lightAttenuation` (include/orosmo/renderer/forward_plus.hpp), comparées au GPU par tests/renderer/forward_plus_tests.cpp.
// Inclusion : #include "../include/orosmo/lights.glsl" (relatif au shader) ou "orosmo/lights.glsl" avec la racine `shaders/include`.
#ifndef OROSMO_LIGHTS_GLSL
#define OROSMO_LIGHTS_GLSL

#include "math.glsl"

// Une lumière est négligée là où son intensité maximale atténuée (couleur × force × (diffus + spéculaire ≤ 1,5)) tombe sous
// 1/256, un pas d'une cible 8 bits. Sans atténuation (a = b = 0), la portée est kLightMaxRange.
const float kLightCutoff = 1.0 / 256.0;
const float kLightMaxRange = 1.0e6;
const float kLightPeakFactor = 1.5; // diffus (1) + spéculaire (0,5) au plus

// Distance d où intensité / (a d² + b d + 1) = kLightCutoff ; 0 si la lumière est déjà sous le seuil.
float lightRange(vec3 color, float strength, float a, float b)
{
    float peak = max(max(color.r, color.g), color.b) * strength * kLightPeakFactor;
    float k = peak / kLightCutoff - 1.0; // a d² + b d = k
    if (k <= 0.0)
    {
        return 0.0;
    }
    if (a > 0.0)
    {
        return min((-b + sqrt(b * b + 4.0 * a * k)) / (2.0 * a), kLightMaxRange);
    }
    if (b > 0.0)
    {
        return min(k / b, kLightMaxRange);
    }
    return kLightMaxRange;
}

// 1 / (a d² + b d + 1), ramenée à 0 à la portée par la fenêtre (1 - (d / portée)⁴)² : pas de coupure visible, et une lumière
// hors de sa portée n'apporte exactement rien (le forward+ peut l'écarter sans changer l'image).
float lightAttenuation(float d, float a, float b, float range)
{
    if (d >= range)
    {
        return 0.0;
    }
    float x = d / range;
    float x2 = x * x;
    float window = 1.0 - x2 * x2;
    return window * window / (a * d * d + b * d + 1.0);
}

// Facteur du cône d'un projecteur : 1 dans le cône intérieur, 0 hors du cône extérieur (cosinus), linéaire entre les deux.
// `beam` : sens du faisceau ; `toLight` : direction normalisée du point vers la lumière.
float spotCone(vec3 beam, vec3 toLight, float outerCone, float innerCone)
{
    float angle = dot(normalize(beam), -toLight);
    float width = innerCone - outerCone;
    return abs(width) > kEpsilon ? saturate((angle - outerCone) / width) : step(outerCone, angle);
}

// Diffus + spéculaire (Phong, 0,5 × cos^specularPower) d'une lumière ponctuelle ; `cone` vaut 1 pour une ponctuelle.
vec3 localLightRadiance(vec3 lightPosition, vec3 color, float strength, float a, float b, vec3 position, vec3 normal, vec3 eye,
                        float specularPower, vec3 beam, float outerCone, float innerCone, bool spot)
{
    vec3 toLight = lightPosition - position;
    float d = length(toLight);
    float attenuation = lightAttenuation(d, a, b, lightRange(color, strength, a, b));
    if (attenuation <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 l = d > kEpsilon ? toLight / d : vec3(0.0, 1.0, 0.0);
    if (spot)
    {
        attenuation *= spotCone(beam, l, outerCone, innerCone);
    }
    vec3 n = normalize(normal);
    float diffuse = saturate(dot(n, l));
    vec3 v = normalize(eye - position);
    float specular = pow(saturate(dot(v, reflect(-l, n))), specularPower) * 0.5;
    return color * strength * (diffuse + specular) * attenuation;
}

#endif
