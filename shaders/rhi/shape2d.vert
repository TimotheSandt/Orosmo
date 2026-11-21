#version 450
// Formes 2D (R2D-03) : triangles colorés en coordonnées du monde (lignes, rectangles, cercles, polygones), un dessin par lot.
layout(push_constant) uniform Push
{
    mat4 viewProj;
} pc;

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = pc.viewProj * vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
}
