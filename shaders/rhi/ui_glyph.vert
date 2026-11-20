#version 450
// Glyphes MSDF : sommets (x, y, u, v) en pixels logiques de l'interface.
layout(location = 0) in vec4 vertex;

layout(push_constant) uniform Push
{
    vec4 params; // xy : taille de la surface en pixels, z : msdfPixelRange, w : msdfWeight
    vec4 color;  // rgb : couleur du texte
} pc;

layout(location = 0) out vec2 TexCoords;

void main()
{
    // ortho(0, largeur, hauteur, 0) : (x, y) en pixels -> NDC, Y vers le bas
    gl_Position = vec4(vertex.x / pc.params.x * 2.0 - 1.0, 1.0 - vertex.y / pc.params.y * 2.0, 0.0, 1.0);
    TexCoords = vertex.zw;
}
