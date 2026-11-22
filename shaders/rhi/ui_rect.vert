#version 450
// Rectangles de l'interface (origine en haut à gauche, Y vers le bas), dessinés par INSTANCES (R2D-01) : un dessin pour tout un lot
// de rectangles consécutifs. Quad généré par gl_VertexIndex ; les données de chaque rectangle sont des attributs par instance.
layout(push_constant) uniform Push
{
    vec4 container; // xy : taille de la surface en pixels
} pc;

layout(location = 0) in vec4 iOffsetScale; // xy : décalage en pixels, zw : taille en pixels
layout(location = 1) in vec4 iColor;
layout(location = 2) in vec4 iShape;       // x : rayon des coins, y : épaisseur de bordure, z : adoucissement (pixels logiques)
layout(location = 3) in vec4 iBorderColor;

layout(location = 0) out vec2 vLocal; // position dans le rectangle, en pixels logiques
layout(location = 1) flat out vec4 vSize;
layout(location = 2) flat out vec4 vColor;
layout(location = 3) flat out vec4 vShape;
layout(location = 4) flat out vec4 vBorderColor;

void main()
{
    // 6 sommets : (0,0) (1,0) (1,1) (1,1) (0,1) (0,0)
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(1, 1), vec2(0, 1), vec2(0, 0));
    vec2 aPos = corners[gl_VertexIndex];
    vLocal = aPos * iOffsetScale.zw;
    vSize = iOffsetScale;
    vColor = iColor;
    vShape = iShape;
    vBorderColor = iBorderColor;
    vec2 pixelPos = vLocal + iOffsetScale.xy;
    vec2 normalizedPos = pixelPos / pc.container.xy * 2.0 - 1.0;
    normalizedPos.y = -normalizedPos.y; // le contenu de l'interface a Y vers le bas
    gl_Position = vec4(normalizedPos, 0.0, 1.0);
}
