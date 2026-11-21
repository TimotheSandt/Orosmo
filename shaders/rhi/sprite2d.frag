#version 450
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 color;

layout(set = 1, binding = 0) uniform sampler2D image;

void main()
{
    color = texture(image, vUv) * vColor;
}
