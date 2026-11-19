#version 450
// TER-07 : tuile de terrain. Sommets (position relative à l'origine de la tuile, normale) ; le bloc du modèle place la tuile (et
// retire la position de la caméra en rendu relatif). La position MONDE (origine + locale) sert au texturage triplanaire.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(set = 2, binding = 0, std140) uniform ModelBlock
{
    mat4 model;
    mat4 normalMatrix;
} model;

layout(push_constant) uniform Push
{
    int wireframe;
    int pad0;
    int pad1;
    int pad2;
    vec4 origin;   // xyz : origine monde de la tuile
    vec4 bands;    // x : sable sous (m), y : neige au-dessus (m), z : pente de roche (1 - n.y), w : largeur de transition
    vec4 texture_; // x : échelle des textures (1/m)
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;

void main()
{
    vec4 placed = model.model * vec4(aPos, 1.0);
    vNormal = mat3(model.normalMatrix) * aNormal;
    vWorld = pc.origin.xyz + aPos;
    gl_Position = camera.matrix * placed;
}
