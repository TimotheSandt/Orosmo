#version 450
// REN-18 : réduction du bloom (13 échantillons, Jimenez 2014) ; la première réduction applique le seuil doux.
#include "post_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push
{
    vec4 params;  // xy : taille d'un texel de la source, z : seuil, w : genou (douceur du seuil)
    vec4 options; // x : 1 = appliquer le seuil (première réduction), y : borne de luminance (anti-lucioles)
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

vec3 tap(vec2 offset) { return texture(source, uv + offset * pc.params.xy).rgb; }

void main()
{
    vec3 a = tap(vec2(-2.0, -2.0)), b = tap(vec2(0.0, -2.0)), c = tap(vec2(2.0, -2.0));
    vec3 d = tap(vec2(-2.0, 0.0)), e = tap(vec2(0.0, 0.0)), f = tap(vec2(2.0, 0.0));
    vec3 g = tap(vec2(-2.0, 2.0)), h = tap(vec2(0.0, 2.0)), i = tap(vec2(2.0, 2.0));
    vec3 j = tap(vec2(-1.0, -1.0)), k = tap(vec2(1.0, -1.0)), l = tap(vec2(-1.0, 1.0)), m = tap(vec2(1.0, 1.0));
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (pc.options.x > 0.5)
    {
        color = min(color, vec3(pc.options.y));
        float brightness = max(color.r, max(color.g, color.b));
        float knee = pc.params.w;
        float soft = clamp(brightness - pc.params.z + knee, 0.0, 2.0 * knee);
        soft = knee > 0.0 ? soft * soft / (4.0 * knee + 1e-5) : 0.0;
        color *= max(soft, brightness - pc.params.z) / max(brightness, 1e-5);
    }
    FragColor = vec4(color, 1.0);
}
