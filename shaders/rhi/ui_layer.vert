#version 450
// Image d'une couche (conteneur dessiné hors écran).
layout(push_constant) uniform Push
{
    vec4 offsetScale;     // xy : décalage, zw : taille visible en pixels
    vec4 containerScroll; // xy : taille de la surface, zw : décalage de défilement
    vec4 content;         // xy : taille du contenu de la couche
    vec4 color;           // couleur de fond / teinte
} pc;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(1, 1), vec2(0, 1), vec2(0, 0));
    vec2 aPos = corners[gl_VertexIndex];
    gl_Position = vec4((aPos.x * pc.offsetScale.z + pc.offsetScale.x) / pc.containerScroll.x * 2.0 - 1.0,
                       1.0 - (aPos.y * pc.offsetScale.w + pc.offsetScale.y) / pc.containerScroll.y * 2.0, 0.0, 1.0);
    v_TexCoord = aPos;
}
