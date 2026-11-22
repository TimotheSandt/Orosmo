// REN-21 : sorties auxiliaires (AOV) d'un fragment shader de maillage. Les macros OROSMO_AOV_<NOM> (= emplacement de sortie) viennent
// de `orosmo::renderer::aovShaderDefines(set)` donné à loadGraphics ; sans elles ce fichier ne déclare rien (le shader est celui
// d'avant M5). Usage : #include "aov.glsl", puis `writeAovs(...)` à la fin de main().
#ifndef OROSMO_AOV_GLSL
#define OROSMO_AOV_GLSL

#ifdef OROSMO_AOV_MOTION
layout(location = OROSMO_AOV_MOTION) out vec2 aovMotion;
#endif
#ifdef OROSMO_AOV_NORMALS
layout(location = OROSMO_AOV_NORMALS) out vec2 aovNormals;
#endif
#ifdef OROSMO_AOV_MATERIAL
layout(location = OROSMO_AOV_MATERIAL) out vec2 aovMaterial;
#endif
#ifdef OROSMO_AOV_OBJECT_ID
layout(location = OROSMO_AOV_OBJECT_ID) out uint aovObjectId;
#endif
#ifdef OROSMO_AOV_LINEAR_DEPTH
layout(location = OROSMO_AOV_LINEAR_DEPTH) out float aovLinearDepth;
#endif
#ifdef OROSMO_AOV_EMISSION
layout(location = OROSMO_AOV_EMISSION) out vec4 aovEmission;
#endif
#ifdef OROSMO_AOV_ALBEDO
layout(location = OROSMO_AOV_ALBEDO) out vec4 aovAlbedo;
#endif

// motion : (NDC courant - NDC précédent) / 2, jitter retiré ; `jitter` = FrameData.jitter (xy courant, zw précédent).
vec2 aovMotionVector(vec4 currentClip, vec4 previousClip, vec4 jitter)
{
    const vec2 current = currentClip.xy / currentClip.w - jitter.xy;
    const vec2 previous = previousClip.xy / previousClip.w - jitter.zw;
    return (current - previous) * 0.5;
}

#endif
