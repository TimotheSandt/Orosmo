// Boîte à outils de shaders (REN-22) : conversions de coordonnées et d'espaces.
// Convention : uv a son origine en haut à gauche (comme Vulkan) ; NDC a y vers le bas dans le même sens que uv (uv 0 = ndc -1).
#ifndef OROSMO_COORDS_GLSL
#define OROSMO_COORDS_GLSL

#include "math.glsl"

vec2 uvToNdc(vec2 uv) { return uv * 2.0 - 1.0; }
vec2 ndcToUv(vec2 ndc) { return ndc * 0.5 + 0.5; }

// Pixel (centre à +0.5) <-> uv pour une cible de `size` pixels.
vec2 pixelToUv(vec2 pixel, vec2 size) { return pixel / size; }
vec2 uvToPixel(vec2 uv, vec2 size) { return uv * size; }

// Encodage octaédrique d'un vecteur unitaire en [0, 1]^2 (normales compactes) et son inverse.
vec2 octEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0)
    {
        e = (1.0 - abs(e.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e * 0.5 + 0.5;
}

vec3 octDecode(vec2 f)
{
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

// Direction unitaire <-> (azimut, élévation) : azimut dans (-pi, pi] autour de +y, élévation dans [-pi/2, pi/2].
vec3 sphericalToDir(float azimuth, float elevation)
{
    float c = cos(elevation);
    return vec3(c * sin(azimuth), sin(elevation), c * cos(azimuth));
}

vec2 dirToSpherical(vec3 d)
{
    // Aux pôles (x = z = 0) atan(0, 0) est indéfini : azimut conventionnel 0.
    float azimuth = (d.x == 0.0 && d.z == 0.0) ? 0.0 : atan(d.x, d.z);
    return vec2(azimuth, asin(clamp(d.y, -1.0, 1.0)));
}

// Normale d'espace tangent (déjà décodée dans [-1, 1]) -> espace monde, avec la base (t, b, n) orthonormée.
vec3 tangentToWorld(vec3 tangentNormal, vec3 t, vec3 b, vec3 n)
{
    return normalize(t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z);
}

#endif
