// Boîte à outils de shaders (REN-22) : échantillonnage d'ombres, PCF (percentage-closer filtering).
// `coord` = (uv dans la carte, profondeur du récepteur) en coordonnées de la carte d'ombres. La comparaison est faite par
// l'échantillonneur de comparaison de la carte (SamplerDesc::compare) : à lui de la régler selon le sens de la profondeur
// (CompareOp::kGreaterEqual en reverse-Z, kLessEqual sinon). Retour : visibilité dans [0, 1] (1 = éclairé).
#ifndef OROSMO_SHADOW_GLSL
#define OROSMO_SHADOW_GLSL

#include "math.glsl"

// Biais de pente : décale la profondeur du récepteur selon l'angle (nDotL), pour éviter l'acné d'ombre. Retourne un biais
// positif à retrancher en forward-Z (ajouter en reverse-Z).
float shadowSlopeBias(float nDotL, float constantBias, float slopeBias)
{
    float s = saturate(nDotL);
    float tanTheta = sqrt(max(1.0 - s * s, 0.0)) / max(s, 1e-3);
    return constantBias + slopeBias * min(tanTheta, 10.0);
}

// Hors de la carte (uv hors [0,1] ou profondeur hors [0,1]) : pas d'ombre.
bool shadowInsideMap(vec3 coord)
{
    return all(greaterThanEqual(coord, vec3(0.0))) && all(lessThanEqual(coord, vec3(1.0)));
}

// PCF en grille (2 * radius + 1)^2 échantillons de comparaison matérielle ; radius 0 : un seul échantillon.
float shadowPcf(sampler2DShadow shadowMap, vec3 coord, vec2 texelSize, int radius)
{
    if (!shadowInsideMap(coord))
    {
        return 1.0;
    }
    float sum = 0.0;
    int count = 0;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            vec2 uv = coord.xy + vec2(float(x), float(y)) * texelSize;
            sum += texture(shadowMap, vec3(uv, coord.z));
            ++count;
        }
    }
    return sum / float(count);
}

#endif
