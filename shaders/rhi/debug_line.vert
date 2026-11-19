#version 450
// REN-34 : segments de debug (position monde ou relative à la caméra, couleur par sommet), groupe 0 = caméra de la scène.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(location = 0) out vec4 color;

void main()
{
    color = aColor;
    gl_Position = camera.matrix * vec4(aPos, 1.0);
}
