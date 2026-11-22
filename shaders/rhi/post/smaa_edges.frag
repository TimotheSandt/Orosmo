#version 450
// REN-19 : SMAA 1x, passe 1/3 : contours de luminance (seuil + adaptation au contraste local, comme SMAALumaEdgeDetectionPS).
// Sortie RG8 : r = contour entre le pixel et son voisin de gauche, g = entre le pixel et son voisin du dessus (ligne précédente ;
// v = 0 est la première ligne, repère du RHI A26). Coordonnées entières : aucune dépendance au filtrage.
#include "post_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D scene;

layout(push_constant) uniform Push
{
    vec4 params; // x : seuil, y : facteur d'adaptation au contraste local
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

ivec2 gSize;

float lumaAt(ivec2 p) { return luminance(saturate(texelFetch(scene, clamp(p, ivec2(0), gSize - 1), 0).rgb)); }

void main()
{
    gSize = textureSize(scene, 0);
    const ivec2 p = clamp(ivec2(uv * vec2(gSize)), ivec2(0), gSize - 1);
    const float c = lumaAt(p);
    const float l = lumaAt(p - ivec2(1, 0));
    const float t = lumaAt(p - ivec2(0, 1));
    const vec2 delta = abs(c - vec2(l, t));
    vec2 edges = step(vec2(pc.params.x), delta);
    if (p.x == 0)
    {
        edges.x = 0.0; // bord de l'image : pas de voisin
    }
    if (p.y == 0)
    {
        edges.y = 0.0;
    }
    if (edges.x + edges.y > 0.0)
    {
        // Adaptation au contraste local : un contour nettement plus faible qu'un contour voisin est ignoré.
        const float r = lumaAt(p + ivec2(1, 0));
        const float b = lumaAt(p + ivec2(0, 1));
        const float ll = lumaAt(p - ivec2(2, 0));
        const float tt = lumaAt(p - ivec2(0, 2));
        vec2 maxDelta = max(delta, abs(vec2(r, b) - c));
        maxDelta = max(maxDelta, abs(vec2(ll, tt) - vec2(l, t)));
        const float finalDelta = max(maxDelta.x, maxDelta.y);
        edges *= step(vec2(finalDelta), pc.params.y * delta);
    }
    FragColor = vec4(edges, 0.0, 1.0);
}
