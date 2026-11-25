#version 450
// REN-19 : TAA, résolution d'une image. Entrées : scène courante (jitter de Halton, HDR ou LDR), vecteurs de mouvement et profondeur
// linéaire (AOV, REN-21), historique de l'image précédente (couleur accumulée et information). Étapes :
//   1. voisinage 3×3 de la scène : moments dans l'espace YCoCg des couleurs compressées c / (1 + L) (poids anti-lucioles,
//      réversible : l'accumulation reste en HDR), et texel le plus proche (profondeur minimale) dont on prend le mouvement ;
//   2. reprojection : uv précédent = uv - mouvement (le mouvement est (NDC courant - NDC précédent) / 2, sans jitter ; v descend
//      quand y NDC monte, repère A26) ; hors de l'image : historique rejeté ;
//   3. rejet : aucune profondeur de l'historique (3×3 autour du point reprojeté) à moins de `depthTolerance` (relatif) de celle du
//      pixel (désocclusion, coupure de scène), ou mouvement trop différent de celui de l'historique (rejet progressif) ;
//   4. écrêtage de l'historique dans la boîte moyenne ± gamma × écart type (∩ min/max), vers son centre ;
//   5. mélange : poids de l'image courante 1 / n (n images accumulées, au plus maxAccumulatedFrames ; 1 après un rejet).
// Sorties : 0 couleur accumulée (RGBA16F, alpha de la scène), 1 information (profondeur du pixel, son mouvement, n).
#include "post_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 1) uniform sampler2D motionTexture;
layout(set = 1, binding = 2) uniform sampler2D depthTexture;
layout(set = 1, binding = 3) uniform sampler2D historyColor; // échantillonneur linéaire
layout(set = 1, binding = 4) uniform sampler2D historyInfo;

layout(push_constant) uniform Push
{
    vec4 params; // x : images accumulées au plus, y : gamma de l'écrêtage, z : tolérance de profondeur, w : tolérance de mouvement (px)
    vec4 flags;  // x : 1 = historique ignoré (réinitialisation), y : 1 = écrêtage
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outInfo;

vec3 compress(vec3 c) { return c / (1.0 + luminance(c)); }
// 1 - L vaut 1 / (1 + L_hdr) : la borne 2^-16 couvre jusqu'au maximum de RGBA16F (65504), sans écraser les valeurs HDR.
vec3 expand(vec3 t) { return t / max(1.0 - luminance(t), 1.0 / 65536.0); }
vec3 toYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// Ramène q dans la boîte [lo, hi] le long du segment vers son centre (Playdead, « Temporal Reprojection Anti-Aliasing in INSIDE »).
vec3 clipToBox(vec3 lo, vec3 hi, vec3 q)
{
    const vec3 center = 0.5 * (hi + lo);
    const vec3 extent = 0.5 * (hi - lo) + 1e-5;
    const vec3 v = q - center;
    const vec3 unit = abs(v / extent);
    const float m = max(unit.x, max(unit.y, unit.z));
    return m > 1.0 ? center + v / m : q;
}

bool depthMatches(float reference, float previous)
{
    if (reference <= 0.0 || previous <= 0.0)
    {
        return reference <= 0.0 && previous <= 0.0; // 0 : pas de géométrie (ciel) des deux côtés
    }
    return abs(previous - reference) <= pc.params.z * reference;
}

void main()
{
    const ivec2 size = textureSize(scene, 0);
    const vec2 sizeF = vec2(size);
    const ivec2 p = clamp(ivec2(uv * sizeF), ivec2(0), size - 1);
    const vec4 center = texelFetch(scene, p, 0);
    const vec3 current = toYCoCg(compress(max(center.rgb, vec3(0.0))));

    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    vec3 lo = vec3(1e30);
    vec3 hi = vec3(-1e30);
    float closest = 1e30;
    ivec2 closestP = p;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const ivec2 q = clamp(p + ivec2(dx, dy), ivec2(0), size - 1);
            const vec3 c = toYCoCg(compress(max(texelFetch(scene, q, 0).rgb, vec3(0.0))));
            m1 += c;
            m2 += c * c;
            lo = min(lo, c);
            hi = max(hi, c);
            const float d = texelFetch(depthTexture, q, 0).r;
            if (d > 0.0 && d < closest)
            {
                closest = d;
                closestP = q;
            }
        }
    }
    const float ownDepth = texelFetch(depthTexture, p, 0).r;
    const vec2 ownMotion = texelFetch(motionTexture, p, 0).rg;
    const float reference = closest < 1e30 ? closest : 0.0;
    const vec2 motion = texelFetch(motionTexture, closestP, 0).rg;
    const vec2 previousUv = uv + vec2(-motion.x, motion.y);

    float frames = 1.0;
    bool blended = false; // faux : historique rejeté, la couleur courante sort telle quelle (sans compression puis expansion)
    vec3 result = current;
    const bool inside = all(greaterThanEqual(previousUv, vec2(0.0))) && all(lessThanEqual(previousUv, vec2(1.0)));
    if (pc.flags.x < 0.5 && inside)
    {
        const ivec2 pp = clamp(ivec2(previousUv * sizeF), ivec2(0), size - 1);
        bool depthOk = false;
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                depthOk = depthOk || depthMatches(reference, texelFetch(historyInfo, clamp(pp + ivec2(dx, dy), ivec2(0), size - 1), 0).r);
            }
        }
        const vec4 info = texelFetch(historyInfo, pp, 0);
        const float tolerance = max(pc.params.w, 1e-3);
        const float motionDelta = length((info.gb - motion) * sizeF);
        float reject = depthOk ? 0.0 : 1.0;
        reject = max(reject, saturate((motionDelta - tolerance) / tolerance));
        if (reject < 1.0)
        {
            vec3 history = toYCoCg(compress(max(texture(historyColor, previousUv).rgb, vec3(0.0))));
            if (pc.flags.y > 0.5)
            {
                const vec3 mean = m1 / 9.0;
                const vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
                history = clipToBox(max(lo, mean - pc.params.y * sigma), min(hi, mean + pc.params.y * sigma), history);
            }
            const float accumulated = min(info.a + 1.0, pc.params.x);
            result = mix(history, current, mix(1.0 / accumulated, 1.0, reject));
            blended = true;
            frames = mix(accumulated, 1.0, reject);
        }
    }
    outColor = vec4(blended ? expand(fromYCoCg(result)) : center.rgb, center.a);
    outInfo = vec4(ownDepth, ownMotion, frames);
}
