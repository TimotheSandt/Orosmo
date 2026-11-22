#version 450
// Images de l'interface (fonds en neuf parties, UI-07) : sommets (x, y, u, v) en pixels logiques, comme les glyphes.
layout(location = 0) in vec4 vertex;
layout(location = 1) in vec4 vTint; // teinte de la commande, répétée sur ses sommets (lots, R2D-01)

layout(push_constant) uniform Push
{
    vec4 params; // xy : taille de la surface en pixels
    vec4 tint; // inutilisé : la teinte est un attribut de sommet
} pc;

layout(location = 0) out vec2 TexCoords;
layout(location = 1) out vec4 Tint;

void main()
{
    gl_Position = vec4(vertex.x / pc.params.x * 2.0 - 1.0, 1.0 - vertex.y / pc.params.y * 2.0, 0.0, 1.0);
    TexCoords = vertex.zw;
    Tint = vTint;
}
