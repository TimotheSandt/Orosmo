#version 450
// Éclairage : ambiante, directionnelles, ponctuelles et projecteurs (REN-05, lights.glsl).
// Convention des programmes de maillage : le premier entier du bloc push_constant est le drapeau « fil de fer » (1), ou 2 dans la
// pré-passe de profondeur de SceneRenderer (couleur masquée : le programme peut sauter son éclairage).
// Variantes : OROSMO_FORWARD_PLUS (REN-04) lit les lumières du cluster du fragment (clusters.glsl, ForwardPlusLighting) au lieu de
// toutes les lumières ; OROSMO_CLUSTER_DEBUG (avec OROSMO_FORWARD_PLUS) affiche le nombre de lumières par cluster.

// Boîte à outils partagée (REN-22) : chemin relatif à ce fichier, sans racine d'inclusion à configurer.
#include "../include/orosmo/math.glsl"
#include "../include/orosmo/lights.glsl"
#include "frame_data.glsl"
#include "light_block.glsl"
#ifdef OROSMO_FORWARD_PLUS
#include "clusters.glsl"
#endif
// REN-21 : sorties auxiliaires (macros OROSMO_AOV_* : aovShaderDefines) ; sans elles, rien de plus que la couleur.
#include "aov.glsl"
#ifdef OROSMO_AOV_NORMALS
#include "../include/orosmo/coords.glsl"
#endif

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
#ifdef OROSMO_TEXTURED
// R2D-05 : couleur lue dans une texture (groupe 1, binding 0) au lieu de la couleur des sommets ; à associer à mesh_textured.vert.
layout(location = 2) in vec2 uv;
layout(set = 1, binding = 0) uniform sampler2D albedo;
#else
layout(location = 2) in vec3 color;
#endif
#ifdef OROSMO_INSTANCED
// REN-31 : seuil de tramage par instance (lod.hpp) ; les seuils f et -f de deux niveaux couvrent chaque pixel une fois.
layout(location = 3) flat in float dither;
#endif
#ifdef OROSMO_AOV_MOTION
layout(location = 4) in vec4 currentClip;
layout(location = 5) in vec4 previousClip;
#endif
#ifdef OROSMO_AOV_OBJECT_ID
layout(location = 6) flat in uint objectId;
#endif

layout(location = 0) out vec4 FragColor;

#ifdef OROSMO_TRANSPARENT
// REN-32 / REN-37 : matériau à mode de fusion. `mode` suit BlendMode (blend_mode.hpp) : 0 opaque, 1 mask, 2 mask_a2c, 3 dithered,
// 4 blend, 5 additive, 6 premultiplied, 7 transmission. `alpha` est l'opacité de l'objet.
layout(push_constant) uniform Push
{
    int wireframe;
    float alpha;
    float cutoff;
    int mode;
} pc;
#else
layout(push_constant) uniform Push
{
    int wireframe;
} pc;
#endif

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

// REN-06 à REN-08 : ombres (variante OROSMO_SHADOWS : groupe 1, bindings 6 et 7, voir shadow_receiver.glsl). Sans elle : 1.
#ifdef OROSMO_SHADOWS
#include "shadow_receiver.glsl"
#else
float orosmoLightShadow(int lightIndex, vec3 position, vec3 n, vec3 toLight, vec3 eye)
{
    return 1.0;
}
#endif

vec4 directLight(vec3 lightDirection, vec3 lightColor, float lightIntensity)
{
    // lumière diffuse
    vec3 norm = normalize(normal);
    lightDirection = normalize(lightDirection);
    float diffuse = saturate(dot(norm, lightDirection));

    // lumière spéculaire
    vec3 viexDirection = normalize(camera.position - crntPos);
    vec3 reflectionDirection = reflect(-lightDirection, norm);
    float specAmount = pow(saturate(dot(viexDirection, reflectionDirection)), 16);
    float specularLight = 0.5f;
    float specular = specAmount * specularLight;

    return vec4(lightColor, 1.0f) * (diffuse + specular) * lightIntensity;
}

vec4 wireframeRender()
{
    return vec4(0.0f);
}

// Apport de la lumière i (même fonction en forward et en forward+ : même image).
vec4 lightContribution(int i)
{
    LightStruct lght = lights.lights[i];
    if (lght.type == 1)
    {
        return vec4(lght.color * lght.strength, 1.0f);
    }
    if (lght.type == 2)
    {
        return directLight(lght.direction, lght.color, lght.strength) *
               orosmoLightShadow(i, crntPos, normalize(normal), normalize(lght.direction), camera.position);
    }
    if (lght.type == 3 || lght.type == 4)
    {
        // Positions des lumières en coordonnées monde, ramenées dans l'espace des shaders (rendu relatif à la caméra, REN-02).
        vec3 offset = (frame.cameraWorldHigh.xyz + frame.cameraWorldLow.xyz) - frame.cameraRelativeOrigin.xyz;
        const bool spot = lght.type == 4;
        // REN-08 : ombre du projecteur (1 sans OROSMO_SHADOWS ou sans vue d'ombre pour cette lumière).
        const float shadow = spot ? orosmoLightShadow(i, crntPos, normalize(normal), vec3(0.0f), camera.position) : 1.0f;
        return vec4(localLightRadiance(lght.position - offset, lght.color, lght.strength, lght.a, lght.b, crntPos, normal, camera.position,
                                       spot ? 64.0f : 16.0f, lght.direction, lght.outerCone, lght.innerCone, spot) *
                        shadow,
                    0.0f);
    }
    return vec4(0.0f);
}

vec4 fillRender()
{
    vec4 l = vec4(lights.AmbientLightColor.rgb * lights.AmbientLightStrength, 1.0f);
#ifdef OROSMO_FORWARD_PLUS
    const uint cluster = clusterIndexAt(crntPos);
#ifdef OROSMO_CLUSTER_DEBUG
    return vec4(clusterHeatColor(cluster), 1.0f);
#endif
    const uint count = clusterLightCount(cluster);
    for (uint k = 0u; k < count; k++)
    {
        l += lightContribution(int(clusterLight(cluster, k)));
    }
#else
    for (int i = 0; i < lights.size; i++)
    {
        l += lightContribution(i);
    }
#endif

#ifdef OROSMO_TEXTURED
    return vec4(texture(albedo, uv).rgb, 1.0f) * l;
#else
    return vec4(color, 1.0f) * l;
#endif
}

void writeAovs()
{
#ifdef OROSMO_AOV_MOTION
    aovMotion = aovMotionVector(currentClip, previousClip, frame.jitter);
#endif
#ifdef OROSMO_AOV_NORMALS
    aovNormals = octEncode(normalize(normal));
#endif
#ifdef OROSMO_AOV_MATERIAL
    aovMaterial = vec2(0.5, 0.0); // rugosité, métal : constantes du matériau par défaut (pas de PBR avant REN-09)
#endif
#ifdef OROSMO_AOV_OBJECT_ID
    aovObjectId = objectId;
#endif
#ifdef OROSMO_AOV_LINEAR_DEPTH
    aovLinearDepth = 1.0 / gl_FragCoord.w; // w de l'espace de découpe = distance le long de l'axe de la vue
#endif
#ifdef OROSMO_AOV_EMISSION
    aovEmission = vec4(0.0); // aucune émission avant REN-09
#endif
#ifdef OROSMO_AOV_ALBEDO
    aovAlbedo = vec4(color, 1.0);
#endif
}

void main()
{
#ifdef OROSMO_INSTANCED
    if (dither != 0.0f)
    {
        const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        const ivec2 p = ivec2(gl_FragCoord.xy) & 3;
        const float n = bayer[p.y * 4 + p.x] / 16.0f;
        if ((dither > 0.0f && n < dither) || (dither < 0.0f && n >= -dither))
        {
            discard;
        }
    }
#endif
    if (pc.wireframe == 2)
    {
        FragColor = vec4(0.0f); // pré-passe de profondeur (REN-04) : couleur masquée, aucun éclairage à calculer
    }
    else if (pc.wireframe == 1)
    {
        FragColor = wireframeRender();
    }
    else
    {
        FragColor = fillRender();
    }
#ifdef OROSMO_TRANSPARENT
    float alpha = pc.alpha;
    if (pc.mode == 1 || pc.mode == 2)
    {
        // Seuil : pixel gardé ou rejeté (alpha-to-coverage, pas encore dans le RHI : mask_a2c se comporte comme mask).
        if (alpha < pc.cutoff)
        {
            discard;
        }
        alpha = 1.0f;
    }
    else if (pc.mode == 3)
    {
        // Tramage de Bayer 4x4 : la part de pixels gardés vaut alpha (lissé ensuite par le TAA).
        const float bayerMask[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        const ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        if ((bayerMask[q.y * 4 + q.x] + 0.5f) / 16.0f > alpha)
        {
            discard;
        }
        alpha = 1.0f;
    }
    else if (pc.mode == 0)
    {
        alpha = 1.0f;
    }
    else if (pc.mode == 6)
    {
        FragColor.rgb *= alpha; // prémultiplié
    }
    FragColor.a = alpha;
#endif
    writeAovs();
}
