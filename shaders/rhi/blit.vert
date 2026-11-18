#version 450
// Triangle plein écran sans tampon de sommets ; uv de (0,0) à (1,1) sur l'écran.
layout(location = 0) out vec2 uv;

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
