#version 450
// REN-18 : vignettage, appliqué par mélange multiplicatif sur la scène (aucune lecture de texture).
#include "post_common.glsl"

layout(push_constant) uniform Push
{
    vec4 settings; // intensité, rayon, douceur, rondeur
    vec4 target;   // x : rapport largeur / hauteur
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main() { FragColor = vec4(vec3(vignetteFactor(uv, pc.target.x, pc.settings)), 1.0); }
