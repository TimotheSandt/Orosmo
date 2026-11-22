#version 450
// REN-07/08 : caster d'ombre instancié (lots d'ObjectSet). Position à l'emplacement 0, colonnes du modèle (GpuInstance,
// instance_batch.hpp) aux emplacements 8 à 11 ; push constants : clip de la vue d'ombre (monde).
layout(location = 0) in vec3 aPos;
layout(location = 8) in vec4 iModel0;
layout(location = 9) in vec4 iModel1;
layout(location = 10) in vec4 iModel2;
layout(location = 11) in vec4 iModel3;

layout(push_constant) uniform Push
{
    mat4 viewProj;
} pc;

void main()
{
    gl_Position = pc.viewProj * (mat4(iModel0, iModel1, iModel2, iModel3) * vec4(aPos, 1.0));
}
