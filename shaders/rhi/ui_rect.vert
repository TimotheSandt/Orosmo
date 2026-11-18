#version 450
// Rectangle plein de l'interface (origine en haut à gauche, Y vers le bas). Quad généré par gl_VertexIndex : pas de tampon de sommets.
layout(push_constant) uniform Push
{
    vec4 offsetScale;   // xy : décalage en pixels, zw : taille en pixels
    vec4 container;     // xy : taille de la surface en pixels
    vec4 color;
} pc;

void main()
{
    // 6 sommets : (0,0) (1,0) (1,1) (1,1) (0,1) (0,0)
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(1, 1), vec2(0, 1), vec2(0, 0));
    vec2 aPos = corners[gl_VertexIndex];
    vec2 pixelPos = aPos * pc.offsetScale.zw + pc.offsetScale.xy;
    vec2 normalizedPos = pixelPos / pc.container.xy * 2.0 - 1.0;
    normalizedPos.y = -normalizedPos.y; // le contenu de l'interface a Y vers le bas
    gl_Position = vec4(normalizedPos, 0.0, 1.0);
}
