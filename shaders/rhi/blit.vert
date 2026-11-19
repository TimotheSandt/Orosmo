#version 450
// Triangle plein écran sans tampon de sommets. Repère du RHI (A26) : NDC y = +1 est le haut de la cible et v = 0 sa première
// ligne (le haut) : le haut de l'écran échantillonne v = 0.
layout(location = 0) out vec2 uv;

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
