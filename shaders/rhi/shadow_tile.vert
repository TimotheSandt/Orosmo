#version 450
// REN-08 : triangle couvrant la tuile d'atlas (viewport) pour l'effacer ou y recopier le cache (shadow_tile.frag).
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
