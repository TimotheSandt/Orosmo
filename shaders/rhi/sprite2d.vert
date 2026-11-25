#version 450
// Sprites 2D (R2D-01) : un quad par INSTANCE (6 sommets par gl_VertexIndex), tous les sprites d'un lot en un seul dessin.
// Repère du monde : Y vers le haut ; l'angle de la caméra et le zoom sont dans `viewProj` (Camera2D::matrix).
layout(push_constant) uniform Push
{
    mat4 viewProj;
} pc;

layout(location = 0) in vec4 iPositionSize; // xy : position du pivot (monde), zw : taille (unités de monde)
layout(location = 1) in vec4 iUvRect;       // xy : uv du coin haut-gauche du sprite, zw : uv du coin bas-droit
layout(location = 2) in vec4 iColor;
layout(location = 3) in vec4 iPivotRotation; // xy : pivot (0..1, depuis le coin bas-gauche), z : rotation (radians), w : inutilisé

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main()
{
    // 6 sommets : (0,0) (1,0) (1,1) (1,1) (0,1) (0,0) ; (0,0) = coin bas-gauche du sprite
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(1, 1), vec2(0, 1), vec2(0, 0));
    vec2 corner = corners[gl_VertexIndex];
    vec2 local = (corner - iPivotRotation.xy) * iPositionSize.zw;
    float c = cos(iPivotRotation.z);
    float s = sin(iPivotRotation.z);
    vec2 world = iPositionSize.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    gl_Position = pc.viewProj * vec4(world, 0.0, 1.0);
    vUv = vec2(mix(iUvRect.x, iUvRect.z, corner.x), mix(iUvRect.w, iUvRect.y, corner.y));
    vColor = iColor;
}
