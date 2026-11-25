#version 450
// REN-19 : SMAA 1x, passe 3/3 : mélange du voisinage (SMAANeighborhoodBlendingPS). Les quatre poids d'un pixel : r de son propre
// texel (voisin du dessus), g du texel du dessous, b de son propre texel (voisin de gauche), a du texel de droite. La direction
// dominante (verticale ou horizontale) est seule appliquée, comme dans la référence.

layout(set = 1, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 1) uniform sampler2D weightsTexture;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

ivec2 gSize;

vec4 colorAt(ivec2 p) { return texelFetch(scene, clamp(p, ivec2(0), gSize - 1), 0); }
vec4 weightsAt(ivec2 p)
{
    if (any(greaterThanEqual(p, gSize)))
    {
        return vec4(0.0);
    }
    return texelFetch(weightsTexture, p, 0);
}

void main()
{
    gSize = textureSize(scene, 0);
    const ivec2 p = clamp(ivec2(uv * vec2(gSize)), ivec2(0), gSize - 1);
    const vec4 center = colorAt(p);
    const vec4 own = weightsAt(p);
    const float up = own.r;
    const float left = own.b;
    const float down = weightsAt(p + ivec2(0, 1)).g;
    const float right = weightsAt(p + ivec2(1, 0)).a;
    if (up + down + left + right <= 0.0)
    {
        FragColor = center;
        return;
    }
    vec3 color;
    if (max(up, down) >= max(left, right))
    {
        const float scale = 1.0 / max(1.0, up + down);
        color = center.rgb * (1.0 - (up + down) * scale) + colorAt(p - ivec2(0, 1)).rgb * up * scale + colorAt(p + ivec2(0, 1)).rgb * down * scale;
    }
    else
    {
        const float scale = 1.0 / max(1.0, left + right);
        color = center.rgb * (1.0 - (left + right) * scale) + colorAt(p - ivec2(1, 0)).rgb * left * scale + colorAt(p + ivec2(1, 0)).rgb * right * scale;
    }
    FragColor = vec4(color, center.a);
}
