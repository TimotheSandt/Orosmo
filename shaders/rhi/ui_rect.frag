#version 450
layout(push_constant) uniform Push
{
    vec4 offsetScale;
    vec4 container;
    vec4 color;
} pc;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = pc.color;
}
