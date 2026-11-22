#version 450
// REN-18 : agrandissement du bloom (filtre tente 3×3), ajouté au niveau supérieur par mélange additif.
layout(set = 1, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push
{
    vec4 params; // xy : taille d'un texel de la source, z : rayon du filtre (en texels), w : facteur appliqué
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 d = pc.params.xy * pc.params.z;
    vec3 sum = texture(source, uv).rgb * 4.0;
    sum += (texture(source, uv + vec2(-d.x, 0.0)).rgb + texture(source, uv + vec2(d.x, 0.0)).rgb + texture(source, uv + vec2(0.0, -d.y)).rgb +
            texture(source, uv + vec2(0.0, d.y)).rgb) * 2.0;
    sum += texture(source, uv + vec2(-d.x, -d.y)).rgb + texture(source, uv + vec2(d.x, -d.y)).rgb + texture(source, uv + vec2(-d.x, d.y)).rgb +
           texture(source, uv + vec2(d.x, d.y)).rgb;
    FragColor = vec4(sum * (pc.params.w / 16.0), 0.0);
}
