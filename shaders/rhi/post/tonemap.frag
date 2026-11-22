#version 450
// REN-17 : exposition (manuelle × automatique), tonemapping, puis encodage sRGB de sortie.
#include "post_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 1, std140) uniform ExposureBlock
{
    vec4 state; // x : exposition automatique (1 sans exposition automatique), y : luminance moyenne, z : exposition visée
} autoExposure;

layout(push_constant) uniform Push
{
    vec4 params; // x : exposition manuelle (linéaire), y : point blanc, z : opérateur, w : 1 = encoder en sRGB
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 source = texture(scene, uv);
    vec3 c = source.rgb * (pc.params.x * autoExposure.state.x);
    c = tonemap(int(pc.params.z + 0.5), c, pc.params.y);
    if (pc.params.w > 0.5)
    {
        c = linearToSrgb(saturate(c));
    }
    FragColor = vec4(c, source.a);
}
