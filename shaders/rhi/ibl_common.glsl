// REN-13 : conventions partagées de l'IBL (include/orosmo/renderer/ibl.hpp) : directions des faces d'un cube, coordonnées
// équirectangulaires, base des harmoniques sphériques d'ordre 2. Références CPU dans src/renderer/ibl.cpp.
#ifndef OROSMO_IBL_COMMON_GLSL
#define OROSMO_IBL_COMMON_GLSL

// Direction du centre du texel (x, y) de la face z d'un cube de côté `size` (convention des cubes Vulkan / OpenGL).
vec3 iblCubeDirection(uvec3 id, float size)
{
    vec2 st = (vec2(id.xy) + 0.5) / size * 2.0 - 1.0;
    float s = st.x;
    float t = st.y;
    vec3 d;
    if (id.z == 0u)
        d = vec3(1.0, -t, -s);
    else if (id.z == 1u)
        d = vec3(-1.0, -t, s);
    else if (id.z == 2u)
        d = vec3(s, 1.0, t);
    else if (id.z == 3u)
        d = vec3(s, -1.0, -t);
    else if (id.z == 4u)
        d = vec3(s, -t, 1.0);
    else
        d = vec3(-s, -t, -1.0);
    return normalize(d);
}

// u = 0 vers +Z, 0,5 vers -Z, 0,75 vers +X ; v = 0 en haut (+Y).
vec2 iblEquirectUv(vec3 d)
{
    return vec2(atan(d.x, -d.z) * (0.5 * kInvPi) + 0.5, acos(clamp(d.y, -1.0, 1.0)) * kInvPi);
}

void iblShBasis(vec3 n, out float y[9])
{
    y[0] = 0.282095;
    y[1] = 0.488603 * n.y;
    y[2] = 0.488603 * n.z;
    y[3] = 0.488603 * n.x;
    y[4] = 1.092548 * n.x * n.y;
    y[5] = 1.092548 * n.y * n.z;
    y[6] = 0.315392 * (3.0 * n.z * n.z - 1.0);
    y[7] = 1.092548 * n.x * n.z;
    y[8] = 0.546274 * (n.x * n.x - n.y * n.y);
}

#endif
