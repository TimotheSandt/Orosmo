#version 450
// REN-19 : SMAA 1x, passe 2/3 : poids de mélange (variante analytique de SMAABlendingWeightCalculationPS, sans diagonales).
// Pour un contour au-dessus du pixel (horizontal) : longueur de la ligne de part et d'autre (texels lus un par un, au plus
// `maxSteps`), forme de chaque extrémité (contour croisé du côté du dessus : +1/2, du dessous : -1/2, aucun ou les deux : 0), puis
// aire entre le contour et la ligne de reconstruction (hauteur de l'extrémité, 0 au milieu de la ligne : formes L, Z et U, la
// construction qui produit AreaTex). Même chose, transposée, pour un contour à gauche du pixel (vertical).
// Sortie RGBA8 : r = part du voisin du dessus dans ce pixel, g = part de ce pixel dans le voisin du dessus, b = part du voisin
// de gauche dans ce pixel, a = part de ce pixel dans le voisin de gauche.

layout(set = 1, binding = 0) uniform sampler2D edgesTexture;

layout(push_constant) uniform Push
{
    vec4 params; // x : pas de recherche maximal
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

ivec2 gSize;

vec2 edgesAt(ivec2 p)
{
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, gSize)))
    {
        return vec2(0.0);
    }
    return texelFetch(edgesTexture, p, 0).rg;
}
bool leftEdge(ivec2 p) { return edgesAt(p).r > 0.5; }
bool topEdge(ivec2 p) { return edgesAt(p).g > 0.5; }

// Aire, sur le pixel [x0, x0 + 1], entre le contour (hauteur 0) et la ligne de reconstruction de la ligne [a, b] : hauteur hA en
// a, 0 au milieu, hB en b (linéaire par morceaux). x : aire du côté négatif (h < 0), y : du côté positif.
vec2 coverage(float x0, float a, float b, float hA, float hB)
{
    const float mid = 0.5 * (a + b);
    vec2 result = vec2(0.0);
    float s0 = max(x0, a);
    float s1 = min(x0 + 1.0, mid);
    if (s1 > s0 && hA != 0.0)
    {
        const float h = hA * (mid - 0.5 * (s0 + s1)) / (mid - a); // linéaire : valeur moyenne au milieu de l'intervalle
        result += (h < 0.0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0)) * (s1 - s0) * abs(h);
    }
    s0 = max(x0, mid);
    s1 = min(x0 + 1.0, b);
    if (s1 > s0 && hB != 0.0)
    {
        const float h = hB * (0.5 * (s0 + s1) - mid) / (b - mid);
        result += (h < 0.0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0)) * (s1 - s0) * abs(h);
    }
    return result;
}

// Forme d'une extrémité : contour croisé du côté négatif seulement (`before` : ligne ou colonne d'indice inférieur) : +1/2 ; du
// côté positif seulement : -1/2 ; aucun ou les deux : 0.
float endHeight(bool before, bool after) { return before == after ? 0.0 : (before ? 0.5 : -0.5); }

void main()
{
    gSize = textureSize(edgesTexture, 0);
    const ivec2 p = clamp(ivec2(uv * vec2(gSize)), ivec2(0), gSize - 1);
    const int steps = int(pc.params.x);
    vec4 weights = vec4(0.0);

    if (topEdge(p))
    {
        // Contour horizontal entre la ligne p.y - 1 (« dessus ») et p.y : recherche vers la gauche puis vers la droite.
        int dl = 0;
        float hL = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const ivec2 q = p - ivec2(i, 0);
            dl = i;
            const bool up = leftEdge(q - ivec2(0, 1));
            const bool down = leftEdge(q);
            if (up || down || !topEdge(q - ivec2(1, 0)))
            {
                hL = endHeight(up, down);
                break;
            }
        }
        int dr = 0;
        float hR = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const ivec2 n = p + ivec2(i + 1, 0);
            dr = i;
            const bool up = leftEdge(n - ivec2(0, 1));
            const bool down = leftEdge(n);
            if (up || down || !topEdge(n))
            {
                hR = endHeight(up, down);
                break;
            }
        }
        const float x = float(p.x);
        const vec2 area = coverage(x, x - float(dl), x + float(dr) + 1.0, hL, hR);
        weights.r = area.x; // h < 0 : la ligne passe dans ce pixel, qui prend la couleur du dessus
        weights.g = area.y; // h > 0 : elle passe dans le pixel du dessus, qui prend la couleur de celui-ci
    }

    if (leftEdge(p))
    {
        // Contour vertical entre la colonne p.x - 1 (« gauche ») et p.x : recherche vers le haut puis vers le bas.
        int du = 0;
        float hU = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const ivec2 q = p - ivec2(0, i);
            du = i;
            const bool left = topEdge(q - ivec2(1, 0));
            const bool right = topEdge(q);
            if (left || right || !leftEdge(q - ivec2(0, 1)))
            {
                hU = endHeight(left, right);
                break;
            }
        }
        int dd = 0;
        float hD = 0.0;
        for (int i = 0; i < steps; ++i)
        {
            const ivec2 n = p + ivec2(0, i + 1);
            dd = i;
            const bool left = topEdge(n - ivec2(1, 0));
            const bool right = topEdge(n);
            if (left || right || !leftEdge(n))
            {
                hD = endHeight(left, right);
                break;
            }
        }
        const float y = float(p.y);
        const vec2 area = coverage(y, y - float(du), y + float(dd) + 1.0, hU, hD);
        weights.b = area.x;
        weights.a = area.y;
    }
    FragColor = weights;
}
