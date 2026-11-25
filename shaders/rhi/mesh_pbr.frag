#version 450
// Matériau PBR métal/rugosité (REN-10) : BRDF GGX / Smith / Schlick de pbr.glsl, lumières de lights.glsl (mêmes types et même
// atténuation que mesh.frag), cartes optionnelles (OROSMO_PBR_*_MAP), alpha masqué ou mélangé, double face, sans éclairage
// (KHR_materials_unlit), éclairage par l'environnement et sondes (OROSMO_IBL, ibl.glsl, REN-13). Groupe 1 : material.hpp. Unités : une lumière de force 1 éclaire une surface blanche mate de face à 1
// (radiance × π × BRDF), comme le diffus de mesh.frag.
// Push constants : premier entier = fil de fer (1) ou pré-passe (2), convention de SceneRenderer.

#include "../include/orosmo/math.glsl"
#include "../include/orosmo/lights.glsl"
#include "../include/orosmo/pbr.glsl"
#include "frame_data.glsl"
#include "light_block.glsl"
#ifdef OROSMO_FORWARD_PLUS
#include "clusters.glsl"
#endif
#ifdef OROSMO_IBL
#include "ibl.glsl"
#endif
#include "aov.glsl"
#ifdef OROSMO_AOV_NORMALS
#include "../include/orosmo/coords.glsl"
#endif

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec2 uv;
#ifdef OROSMO_AOV_MOTION
layout(location = 4) in vec4 currentClip;
layout(location = 5) in vec4 previousClip;
#endif
#ifdef OROSMO_AOV_OBJECT_ID
layout(location = 6) flat in uint objectId;
#endif
layout(location = 7) in vec4 tangent;
layout(location = 8) in vec4 vertexColor;

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Push
{
    int wireframe;
} pc;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;
layout(set = 1, binding = 5, std140) uniform MaterialBlock
{
    vec4 baseColorFactor;
    vec4 emissive;
    vec4 params; // métal, rugosité, échelle de normale, force d'occlusion
    vec4 alpha;  // x : seuil
    uvec4 flags;
} material;

#ifdef OROSMO_SHADOWS
#include "shadow_receiver.glsl"
#else
float orosmoLightShadow(int lightIndex, vec3 position, vec3 n, vec3 toLight, vec3 eye)
{
    return 1.0;
}
#endif

struct Surface
{
    vec3 n;
    vec3 v;
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 ambientResponse; // ce que renvoie une lumière ambiante : albédo × (1 - métal) × occlusion
};

// Radiance de la lumière i vers le point, multipliée par π × BRDF ; une ambiante (type 1) éclaire le diffus seul.
vec3 lightContribution(int i, Surface s)
{
    LightStruct lght = lights.lights[i];
    if (lght.type == 1)
    {
        return lght.color * lght.strength * s.ambientResponse;
    }
    if (lght.type == 2)
    {
        vec3 l = normalize(lght.direction);
        return lght.color * lght.strength * kPi * brdfGGX(s.n, s.v, l, s.albedo, s.metallic, s.roughness) *
               orosmoLightShadow(i, crntPos, s.n, l, camera.position);
    }
    if (lght.type == 3 || lght.type == 4)
    {
        vec3 offset = (frame.cameraWorldHigh.xyz + frame.cameraWorldLow.xyz) - frame.cameraRelativeOrigin.xyz;
        vec3 toLight = (lght.position - offset) - crntPos;
        float d = length(toLight);
        float attenuation = lightAttenuation(d, lght.a, lght.b, lightRange(lght.color, lght.strength, lght.a, lght.b));
        if (attenuation <= 0.0)
        {
            return vec3(0.0);
        }
        vec3 l = d > kEpsilon ? toLight / d : vec3(0.0, 1.0, 0.0);
        float shadow = 1.0;
        if (lght.type == 4)
        {
            attenuation *= spotCone(lght.direction, l, lght.outerCone, lght.innerCone);
            shadow = orosmoLightShadow(i, crntPos, s.n, vec3(0.0), camera.position);
        }
        return lght.color * lght.strength * attenuation * kPi * brdfGGX(s.n, s.v, l, s.albedo, s.metallic, s.roughness) * shadow;
    }
    return vec3(0.0);
}

vec4 baseColor()
{
    vec4 c = material.baseColorFactor * vertexColor;
#ifdef OROSMO_PBR_BASE_COLOR_MAP
    c *= texture(baseColorMap, uv); // texture sRGB : lue linéaire
#endif
    return c;
}

vec3 shadingNormal()
{
    vec3 n = normalize(normal);
#ifdef OROSMO_PBR_DOUBLE_SIDED
    if (!gl_FrontFacing)
    {
        n = -n;
    }
#endif
#ifdef OROSMO_PBR_NORMAL_MAP
    vec3 t = tangent.xyz - n * dot(n, tangent.xyz); // Gram-Schmidt : la tangente interpolée n'est plus orthogonale
    if (dot(t, t) > 1e-8)
    {
        t = normalize(t);
        vec3 b = cross(n, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
#ifdef OROSMO_PBR_DOUBLE_SIDED
        if (!gl_FrontFacing)
        {
            b = -b;
        }
#endif
        vec3 tn = texture(normalMap, uv).xyz * 2.0 - 1.0;
        tn.xy *= material.params.z;
        n = normalize(mat3(t, b, n) * tn);
    }
#endif
    return n;
}

void main()
{
    vec4 base = baseColor();
#ifdef OROSMO_PBR_ALPHA_MASK
    if (base.a < material.alpha.x)
    {
        discard;
    }
#endif
    float alpha = 1.0;
#ifdef OROSMO_PBR_ALPHA_BLEND
    alpha = base.a;
#endif
    if (pc.wireframe == 2)
    {
        FragColor = vec4(0.0); // pré-passe de profondeur : couleur masquée
        return;
    }
    if (pc.wireframe == 1)
    {
        FragColor = vec4(0.0);
        return;
    }

    float metallic = material.params.x;
    float roughness = material.params.y;
#ifdef OROSMO_PBR_METALLIC_ROUGHNESS_MAP
    vec4 mr = texture(metallicRoughnessMap, uv);
    metallic *= mr.b;
    roughness *= mr.g;
#endif
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.03, 1.0);
    vec3 emission = material.emissive.rgb;
#ifdef OROSMO_PBR_EMISSIVE_MAP
    emission *= texture(emissiveMap, uv).rgb;
#endif
    vec3 n = shadingNormal();

#ifdef OROSMO_PBR_UNLIT
    vec3 color = base.rgb;
#else
    float occlusion = 1.0;
#ifdef OROSMO_PBR_OCCLUSION_MAP
    occlusion = 1.0 + material.params.w * (texture(occlusionMap, uv).r - 1.0);
#endif
    Surface s;
    s.n = n;
    s.v = normalize(camera.position - crntPos);
    s.albedo = base.rgb;
    s.metallic = metallic;
    s.roughness = roughness;
    // Ambiante : diffuse seulement, sans réflexion (l'IBL l'ajoute) ; un métal pur n'y renvoie rien.
    s.ambientResponse = base.rgb * (1.0 - metallic) * occlusion;
    vec3 color = lights.AmbientLightColor.rgb * lights.AmbientLightStrength * s.ambientResponse;
#ifdef OROSMO_IBL
    color += iblShade(crntPos, s.n, s.v, s.albedo, s.metallic, s.roughness, occlusion); // sondes relatives, comme crntPos
#endif
#ifdef OROSMO_FORWARD_PLUS
    const uint cluster = clusterIndexAt(crntPos);
    const uint count = clusterLightCount(cluster);
    for (uint k = 0u; k < count; k++)
    {
        color += lightContribution(int(clusterLight(cluster, k)), s);
    }
#else
    for (int i = 0; i < lights.size; i++)
    {
        color += lightContribution(i, s);
    }
#endif
    color += emission;
#endif
    FragColor = vec4(color, alpha);

#ifdef OROSMO_AOV_MOTION
    aovMotion = aovMotionVector(currentClip, previousClip, frame.jitter);
#endif
#ifdef OROSMO_AOV_NORMALS
    aovNormals = octEncode(n);
#endif
#ifdef OROSMO_AOV_MATERIAL
    aovMaterial = vec2(roughness, metallic);
#endif
#ifdef OROSMO_AOV_OBJECT_ID
    aovObjectId = objectId;
#endif
#ifdef OROSMO_AOV_LINEAR_DEPTH
    aovLinearDepth = -(frame.view * vec4(crntPos, 1.0)).z;
#endif
#ifdef OROSMO_AOV_EMISSION
    aovEmission = vec4(emission, 1.0);
#endif
#ifdef OROSMO_AOV_ALBEDO
    aovAlbedo = vec4(base.rgb, 1.0);
#endif
}
