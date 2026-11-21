#version 450
// Maillage éclairé instancié (REN-28, GLSL 450). Groupe 0 : image (caméra, lumières). Les matrices viennent du tampon d'instances
// (pas d'instance), disposition GpuInstance de include/orosmo/renderer/instance_batch.hpp : emplacements 8 à 15. À associer à
// mesh.frag compilé avec OROSMO_INSTANCED (tramage du LOD, REN-31).

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

layout(location = 8) in vec4 iModel0;
layout(location = 9) in vec4 iModel1;
layout(location = 10) in vec4 iModel2;
layout(location = 11) in vec4 iModel3;
layout(location = 12) in vec4 iNormal0; // w : seuil de tramage du LOD
layout(location = 13) in vec4 iNormal1;
layout(location = 14) in vec4 iNormal2;
layout(location = 15) in vec4 iColor;

layout(set = 0, binding = 0, std140) uniform CamBlock
{
    vec3 position;
    mat4 matrix;
} camera;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 crntPos;
layout(location = 2) out vec3 color;
layout(location = 3) flat out float dither;

void main()
{
    const mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    crntPos = vec3(model * vec4(aPos, 1.0f));
    normal = mat3(iNormal0.xyz, iNormal1.xyz, iNormal2.xyz) * aNormal;
    color = aColor * iColor.rgb;
    dither = iNormal0.w;

    gl_Position = camera.matrix * vec4(crntPos, 1.0f);
}
