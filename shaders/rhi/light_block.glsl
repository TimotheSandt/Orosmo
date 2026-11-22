// REN-05 : tampon des lumières (groupe 0, binding 1, std430), même disposition que `GpuLightHeader` + `GpuLight`
// (include/orosmo/renderer/light.hpp, vérifiée par static_assert et par tests/renderer/forward_plus_tests.cpp).
// Types : 0 aucune, 1 ambiante, 2 directionnelle (direction VERS la lumière), 3 ponctuelle, 4 projecteur (direction = faisceau).
#ifndef OROSMO_LIGHT_BLOCK_GLSL
#define OROSMO_LIGHT_BLOCK_GLSL

struct LightStruct
{
    int type;

    vec3 position;
    vec3 direction;
    vec3 color;

    float strength;

    float outerCone;
    float innerCone;
    float a;
    float b;
    float _pad[3];
};

layout(set = 0, binding = 1, std430) buffer LightBlock
{
    int size;

    vec4 AmbientLightColor;
    float AmbientLightStrength;

    LightStruct lights[];
} lights;

#endif
