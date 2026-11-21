#version 450
// Données du rectangle : attributs par instance transmis par le shader de sommets (constants sur le rectangle).
layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in vec4 vSize;        // xy : décalage, zw : taille
layout(location = 2) flat in vec4 vColor;
layout(location = 3) flat in vec4 vShape;       // x : rayon, y : bordure, z : adoucissement
layout(location = 4) flat in vec4 vBorderColor;
layout(location = 0) out vec4 FragColor;

void main()
{
    if (vShape.x <= 0.0 && vShape.y <= 0.0 && vShape.z <= 0.0)
    {
        FragColor = vColor; // rectangle simple : exactement la couleur (images de référence inchangées)
        return;
    }
    // Distance signée au rectangle arrondi (négative à l'intérieur), en pixels logiques.
    vec2 halfSize = vSize.zw * 0.5;
    float radius = min(vShape.x, min(halfSize.x, halfSize.y));
    vec2 q = abs(vLocal - halfSize) - halfSize + vec2(radius);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    float aa = max(fwidth(d), 1e-4);
    float coverage = vShape.z > 0.0 ? 1.0 - smoothstep(-vShape.z, 0.0, d) : clamp(0.5 - d / aa, 0.0, 1.0);
    vec4 c = vColor;
    if (vShape.y > 0.0)
    {
        float inside = clamp(0.5 - (d + vShape.y) / aa, 0.0, 1.0); // 1 à l'intérieur de la bordure
        // Mélange en prémultiplié (une bordure opaque sur un intérieur transparent garde sa couleur au pixel de transition), puis
        // retour en couleur droite pour le mélange SrcAlpha du pipeline.
        vec4 m = mix(vec4(vBorderColor.rgb * vBorderColor.a, vBorderColor.a), vec4(vColor.rgb * vColor.a, vColor.a), inside);
        c = m.a > 0.0 ? vec4(m.rgb / m.a, m.a) : vec4(0.0);
    }
    FragColor = vec4(c.rgb, c.a * coverage);
}
