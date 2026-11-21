#version 450
layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 color;

layout(set = 1, binding = 0) uniform sampler2D image;

layout(push_constant) uniform Push
{
    vec4 params;
    vec4 tint; // texel × teinte (couleur non prémultipliée)
} pc;

void main()
{
    color = texture(image, TexCoords) * pc.tint;
}
