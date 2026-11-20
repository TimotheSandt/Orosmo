#version 450
layout(location = 0) in vec2 TexCoords;
layout(location = 0) out vec4 color;

layout(set = 1, binding = 0) uniform sampler2D text;

layout(push_constant) uniform Push
{
    vec4 params; // z : msdfPixelRange, w : msdfWeight
    vec4 color;
} pc;

float median(vec3 value)
{
    return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

void main()
{
    vec4 sampleValue = texture(text, TexCoords);
    float signedDistance = median(sampleValue.rgb) - 0.5;
    vec2 unitRange = vec2(pc.params.z) / vec2(textureSize(text, 0));
    vec2 dx = dFdx(TexCoords);
    vec2 dy = dFdy(TexCoords);
    vec2 screenTexSize = inversesqrt(max(vec2(dot(dx, dx), dot(dy, dy)), vec2(0.0000001)));
    float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    float alpha = clamp((signedDistance + pc.params.w) * screenPxRange + 0.5, 0.0, 1.0);
    color = vec4(pc.color.rgb, alpha);
}
