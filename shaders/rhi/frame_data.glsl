// REN-03 : données par image (groupe 0, binding 2), même disposition std140 que `orosmo::renderer::FrameData`
// (include/orosmo/renderer/frame_data.hpp, tailles et offsets vérifiés par static_assert et par tests/renderer/render_graph_tests.cpp).
// Usage : #include "frame_data.glsl", puis `frame.viewProj`, `frame.time.x`...
#ifndef OROSMO_FRAME_DATA_GLSL
#define OROSMO_FRAME_DATA_GLSL

layout(set = 0, binding = 2, std140) uniform FrameBlock
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invView;
    mat4 invProj;
    mat4 invViewProj;
    mat4 prevViewProj;
    vec4 cameraRelativeOrigin; // xyz : position de la caméra vue par les shaders (0 en rendu relatif)
    vec4 cameraWorldHigh;      // position monde en double = high + low
    vec4 cameraWorldLow;
    vec4 jitter;               // xy courant, zw précédent
    vec4 viewport;             // largeur, hauteur, 1/largeur, 1/hauteur
    vec4 time;                 // secondes, delta, numéro d'image, graine de bruit
    vec4 sunDirection;         // xyz, w = 1 si une lumière principale existe
    vec4 sunColor;             // couleur × intensité
    vec4 planet;               // rayon, hauteur de l'atmosphère, albédo du sol, réservé
    vec4 scatteringRayleigh;
    vec4 scatteringMie;
    vec4 exposure;             // exposition, réservé
} frame;

#endif
