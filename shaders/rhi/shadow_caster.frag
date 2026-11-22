#version 450
// REN-07/08 : casters d'ombre : profondeur seule (aucune sortie de couleur).
// REN-39 : avec OROSMO_SHADOW_ALPHA (casters transparents), alphaParams = (alpha, seuil, mode, 0), mode 2 = masqué (rejet si alpha <
// seuil), 3 = tramé (trame de Bayer 4x4 fixe en texels de la carte d'ombre : stable dans le temps ; le PCF du récepteur la lisse).
// Même décision que `transparentShadowKeeps` (include/orosmo/renderer/transparent_shadow.hpp). Les modes aucune (0) et pleine (1)
// n'utilisent pas ce shader.
#ifdef OROSMO_SHADOW_ALPHA
layout(push_constant) uniform Push
{
    mat4 matrix;
    vec4 alphaParams;
} pc;
#endif

void main()
{
#ifdef OROSMO_SHADOW_ALPHA
    const int mode = int(pc.alphaParams.z + 0.5);
    if (mode == 2)
    {
        if (pc.alphaParams.x < pc.alphaParams.y)
        {
            discard;
        }
    }
    else if (mode == 3)
    {
        const float bayerMask[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        const ivec2 q = ivec2(gl_FragCoord.xy) & 3;
        if ((bayerMask[q.y * 4 + q.x] + 0.5) / 16.0 > pc.alphaParams.x)
        {
            discard;
        }
    }
#endif
}
