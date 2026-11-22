#version 450
// REN-19 : FXAA (algorithme « qualité » de FXAA 3.11, T. Lottes / NVIDIA, réécrit ici d'après sa description publique) : contraste
// local, direction du contour, recherche de ses extrémités, décalage d'échantillonnage le long de la normale au contour, lissage
// sous-pixel. Lit des valeurs affichées (après tonemapping) ; v = 0 est la première ligne (repère du RHI, A26).
#include "post_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D scene; // échantillonneur linéaire, bords bornés

layout(push_constant) uniform Push
{
    vec4 params; // xy : taille d'un texel (1/w, 1/h), z : seuil relatif, w : seuil absolu
    vec4 extra;  // x : force sous-pixel
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

const int kSteps = 12;
const float kQuality[kSteps] = float[kSteps](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float lumaAt(vec2 p) { return luminance(saturate(texture(scene, p).rgb)); }

void main()
{
    const vec2 texel = pc.params.xy;
    const vec4 center = texture(scene, uv);
    const float lC = luminance(saturate(center.rgb));
    const float lU = lumaAt(uv + vec2(0.0, -texel.y));
    const float lD = lumaAt(uv + vec2(0.0, texel.y));
    const float lL = lumaAt(uv + vec2(-texel.x, 0.0));
    const float lR = lumaAt(uv + vec2(texel.x, 0.0));
    const float lMin = min(lC, min(min(lU, lD), min(lL, lR)));
    const float lMax = max(lC, max(max(lU, lD), max(lL, lR)));
    const float range = lMax - lMin;
    if (range < max(pc.params.w, lMax * pc.params.z))
    {
        FragColor = center; // pas de contour : pixel inchangé
        return;
    }
    const float lUL = lumaAt(uv + vec2(-texel.x, -texel.y));
    const float lUR = lumaAt(uv + vec2(texel.x, -texel.y));
    const float lDL = lumaAt(uv + vec2(-texel.x, texel.y));
    const float lDR = lumaAt(uv + vec2(texel.x, texel.y));
    const float lUD = lU + lD;
    const float lLR = lL + lR;
    const float leftCorners = lUL + lDL;
    const float rightCorners = lUR + lDR;
    const float upCorners = lUL + lUR;
    const float downCorners = lDL + lDR;
    // Contour horizontal (variation verticale) ou vertical.
    const float edgeH = abs(-2.0 * lL + leftCorners) + abs(-2.0 * lC + lUD) * 2.0 + abs(-2.0 * lR + rightCorners);
    const float edgeV = abs(-2.0 * lU + upCorners) + abs(-2.0 * lC + lLR) * 2.0 + abs(-2.0 * lD + downCorners);
    const bool horizontal = edgeH >= edgeV;
    const float luma1 = horizontal ? lU : lL;
    const float luma2 = horizontal ? lD : lR;
    const float grad1 = luma1 - lC;
    const float grad2 = luma2 - lC;
    const bool steepest1 = abs(grad1) >= abs(grad2);
    const float gradScaled = 0.25 * max(abs(grad1), abs(grad2));
    float stepLength = horizontal ? texel.y : texel.x;
    float localAverage;
    if (steepest1)
    {
        stepLength = -stepLength;
        localAverage = 0.5 * (luma1 + lC);
    }
    else
    {
        localAverage = 0.5 * (luma2 + lC);
    }
    // Point de départ : sur le contour, à mi-chemin entre le pixel et son voisin du côté le plus contrasté.
    vec2 start = uv;
    if (horizontal)
    {
        start.y += stepLength * 0.5;
    }
    else
    {
        start.x += stepLength * 0.5;
    }
    const vec2 offset = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 uv1 = start - offset;
    vec2 uv2 = start + offset;
    float end1 = lumaAt(uv1) - localAverage;
    float end2 = lumaAt(uv2) - localAverage;
    bool reached1 = abs(end1) >= gradScaled;
    bool reached2 = abs(end2) >= gradScaled;
    if (!reached1)
    {
        uv1 -= offset;
    }
    if (!reached2)
    {
        uv2 += offset;
    }
    for (int i = 2; i < kSteps && !(reached1 && reached2); ++i)
    {
        if (!reached1)
        {
            end1 = lumaAt(uv1) - localAverage;
            reached1 = abs(end1) >= gradScaled;
        }
        if (!reached2)
        {
            end2 = lumaAt(uv2) - localAverage;
            reached2 = abs(end2) >= gradScaled;
        }
        if (!reached1)
        {
            uv1 -= offset * kQuality[i];
        }
        if (!reached2)
        {
            uv2 += offset * kQuality[i];
        }
    }
    const float distance1 = horizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    const float distance2 = horizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    const bool direction1 = distance1 < distance2;
    const float distanceFinal = min(distance1, distance2);
    const float thickness = distance1 + distance2;
    const float pixelOffset = -distanceFinal / thickness + 0.5;
    // L'extrémité la plus proche doit varier dans le sens opposé au centre : sinon, pas de décalage.
    const bool centerSmaller = lC < localAverage;
    const bool correct = ((direction1 ? end1 : end2) < 0.0) != centerSmaller;
    float finalOffset = correct ? pixelOffset : 0.0;
    // Lissage sous-pixel (détails plus petits qu'un pixel).
    const float average = (1.0 / 12.0) * (2.0 * (lUD + lLR) + leftCorners + rightCorners);
    const float sub1 = saturate(abs(average - lC) / range);
    const float sub2 = (-2.0 * sub1 + 3.0) * sub1 * sub1;
    finalOffset = max(finalOffset, sub2 * sub2 * pc.extra.x);
    vec2 finalUv = uv;
    if (horizontal)
    {
        finalUv.y += finalOffset * stepLength;
    }
    else
    {
        finalUv.x += finalOffset * stepLength;
    }
    FragColor = vec4(texture(scene, finalUv).rgb, center.a);
}
