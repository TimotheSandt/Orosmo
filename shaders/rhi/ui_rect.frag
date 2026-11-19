#version 450
layout(push_constant) uniform Push
{
    vec4 offsetScale;
    vec4 container;
    vec4 color;
    vec4 shape;       // x : rayon, y : bordure, z : adoucissement
    vec4 borderColor;
} pc;

layout(location = 0) in vec2 vLocal;
layout(location = 0) out vec4 FragColor;

void main()
{
    if (pc.shape.x <= 0.0 && pc.shape.y <= 0.0 && pc.shape.z <= 0.0)
    {
        FragColor = pc.color; // rectangle simple : exactement la couleur (images de référence inchangées)
        return;
    }
    // Distance signée au rectangle arrondi (négative à l'intérieur), en pixels logiques.
    vec2 halfSize = pc.offsetScale.zw * 0.5;
    float radius = min(pc.shape.x, min(halfSize.x, halfSize.y));
    vec2 q = abs(vLocal - halfSize) - halfSize + vec2(radius);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float aa = max(fwidth(d), 1e-4);
    float coverage = pc.shape.z > 0.0 ? 1.0 - smoothstep(-pc.shape.z, 0.0, d) : clamp(0.5 - d / aa, 0.0, 1.0);
    vec4 c = pc.color;
    if (pc.shape.y > 0.0)
    {
        float inside = clamp(0.5 - (d + pc.shape.y) / aa, 0.0, 1.0); // 1 à l'intérieur de la bordure
        c = mix(pc.borderColor, pc.color, inside);
    }
    FragColor = vec4(c.rgb, c.a * coverage);
}
