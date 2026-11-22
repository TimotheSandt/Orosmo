#version 450
// REN-18 : étalonnage par LUT 3D (lecture trilinéaire aux centres des texels), mélangé à l'image par `strength`.
layout(set = 1, binding = 0) uniform sampler2D scene;
layout(set = 1, binding = 1) uniform sampler3D lut;

layout(push_constant) uniform Push
{
    vec4 params; // x : (taille - 1) / taille, y : 0,5 / taille, z : force (0 : aucune, 1 : LUT entière)
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 source = texture(scene, uv);
    vec3 c = clamp(source.rgb, vec3(0.0), vec3(1.0));
    vec3 graded = texture(lut, c * pc.params.x + pc.params.y).rgb;
    FragColor = vec4(mix(source.rgb, graded, pc.params.z), source.a);
}
