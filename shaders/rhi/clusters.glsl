// REN-04 : grille de clusters du forward+ (groupe 0, liaisons 3 et 4), remplie par `light_clusters.comp` et lue par les matériaux
// compilés avec OROSMO_FORWARD_PLUS. Même disposition que `GpuClusterHeader` (include/orosmo/renderer/forward_plus.hpp).
//
// Grille : X × Y cases de l'écran (en NDC, indépendantes de la résolution) × Z tranches de profondeur de vue exponentielles
// (tranche k ≥ 1 : à partir de near × (far / near)^(k / Z) ; la tranche 0 commence à l'œil, la dernière va à l'infini).
// Cluster c = x + X × (y + Y × z). Ses lumières : clusterLightIndices[c × budget + i], i < min(clusterCounts[c], budget).
#ifndef OROSMO_CLUSTERS_GLSL
#define OROSMO_CLUSTERS_GLSL

#include "frame_data.glsl"

#ifndef OROSMO_CLUSTER_ACCESS
#define OROSMO_CLUSTER_ACCESS readonly // le compute qui les remplit définit OROSMO_CLUSTER_ACCESS vide
#endif

layout(set = 0, binding = 3, std430) OROSMO_CLUSTER_ACCESS buffer ClusterGrid
{
    uvec4 clusterGrid;    // x, y, z : taille de la grille ; w : budget de lumières listées par cluster
    vec4 clusterDepth;    // x : near, y : far (tranches exponentielles), z : Z / log(far / near), w : réservé
    uvec4 clusterLimits;  // x : budget de lumières examinées (les suivantes sont ignorées) ; yzw : réservés
    uint clusterCounts[]; // par cluster : lumières qui le touchent (peut dépasser le budget : seules les w premières sont listées)
};

layout(set = 0, binding = 4, std430) OROSMO_CLUSTER_ACCESS buffer ClusterLights
{
    uint clusterLightIndices[];
};

// Profondeur de vue où commence la tranche k (0 pour la première).
float clusterSliceStart(uint k)
{
    return k == 0u ? 0.0 : clusterDepth.x * exp(float(k) / clusterDepth.z);
}

// Tranche d'une profondeur de vue (distance positive devant la caméra), bornée à la grille.
uint clusterSlice(float viewDepth)
{
    if (viewDepth <= clusterDepth.x)
    {
        return 0u;
    }
    float s = floor(log(viewDepth / clusterDepth.x) * clusterDepth.z);
    return uint(clamp(s, 0.0, float(clusterGrid.z - 1u)));
}

// Cluster d'un point dans l'espace des shaders (celui de `crntPos` : monde, ou relatif à la caméra en rendu relatif).
uint clusterIndexAt(vec3 shaderPosition)
{
    vec4 clip = frame.viewProj * vec4(shaderPosition, 1.0);
    vec2 ndc = clip.xy / clip.w;
    vec2 size = vec2(clusterGrid.xy);
    vec2 cell = clamp(floor((ndc * 0.5 + 0.5) * size), vec2(0.0), size - 1.0);
    float viewDepth = -(frame.view * vec4(shaderPosition, 1.0)).z;
    return uint(cell.x) + clusterGrid.x * (uint(cell.y) + clusterGrid.y * clusterSlice(viewDepth));
}

// Nombre de lumières listées pour ce cluster (au plus le budget).
uint clusterLightCount(uint cluster)
{
    return min(clusterCounts[cluster], clusterGrid.w);
}

// i-ème lumière (indice dans le tampon des lumières) du cluster.
uint clusterLight(uint cluster, uint i)
{
    return clusterLightIndices[cluster * clusterGrid.w + i];
}

// Mode de debug « lumières par cluster » : bleu (0) -> vert -> jaune -> rouge (budget atteint), magenta si le budget déborde.
vec3 clusterHeatColor(uint cluster)
{
    uint count = clusterCounts[cluster];
    if (count > clusterGrid.w)
    {
        return vec3(1.0, 0.0, 1.0);
    }
    float t = clusterGrid.w > 0u ? float(count) / float(clusterGrid.w) : 0.0;
    if (count == 0u)
    {
        return vec3(0.0, 0.0, 0.25);
    }
    return t < 0.5 ? mix(vec3(0.0, 0.3, 1.0), vec3(0.0, 1.0, 0.2), t * 2.0) : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), t * 2.0 - 1.0);
}

#endif
