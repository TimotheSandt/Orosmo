#version 450
// REN-08 : tuile d'atlas. Sans OROSMO_SHADOW_COPY : effacement (profondeur 1 = rien). Avec : recopie du cache de la géométrie fixe
// (même disposition que l'atlas : même texel). Le RHI n'a pas de copie de texture à texture : la copie passe par gl_FragDepth.
#ifdef OROSMO_SHADOW_COPY
layout(set = 0, binding = 0) uniform sampler2D shadowCache;
#endif

void main()
{
#ifdef OROSMO_SHADOW_COPY
    gl_FragDepth = texelFetch(shadowCache, ivec2(gl_FragCoord.xy), 0).r;
#else
    gl_FragDepth = 1.0;
#endif
}
