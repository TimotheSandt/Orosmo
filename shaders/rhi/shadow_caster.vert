#version 450
// REN-07/08 : caster d'ombre d'un maillage (Mesh). Seule la position (emplacement 0) est lue ; push constants : clip de la vue
// d'ombre × modèle (monde), calculé sur le CPU. Profondeur classique [0, 1] (0 près de la lumière).
layout(location = 0) in vec3 aPos;

layout(push_constant) uniform Push
{
    mat4 matrix;
} pc;

void main()
{
    gl_Position = pc.matrix * vec4(aPos, 1.0);
}
