#version 450
// TER-07 : texturage du terrain : quatre couches d'un tableau de textures (0 sable, 1 herbe, 2 roche, 3 neige) mélangées selon
// l'altitude et la pente, chacune lue en projection TRIPLANAIRE (pas d'étirement sur les pentes raides). Éclairage : celui des
// maillages (ambiante + directionnelles).
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform sampler2DArray layers;

layout(push_constant) uniform Push
{
    int wireframe;
    int pad0;
    int pad1;
    int pad2;
    vec4 origin;
    vec4 bands;
    vec4 texture_;
} pc;

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

vec3 triplanar(float layer, vec3 n)
{
    vec3 w = pow(abs(n), vec3(4.0));
    w /= (w.x + w.y + w.z);
    vec3 p = vWorld * pc.texture_.x;
    return texture(layers, vec3(p.zy, layer)).rgb * w.x + texture(layers, vec3(p.xz, layer)).rgb * w.y +
           texture(layers, vec3(p.xy, layer)).rgb * w.z;
}

// Poids des couches : roche sur les pentes, sinon sable en bas, neige en haut, herbe entre les deux.
vec4 layerWeights(vec3 n)
{
    float blend = max(pc.bands.w, 1e-3);
    float h = vWorld.y;
    float slope = 1.0 - n.y;
    float rock = smoothstep(pc.bands.z - 0.05, pc.bands.z + 0.05, slope);
    float sand = 1.0 - smoothstep(pc.bands.x - blend, pc.bands.x + blend, h);
    float snow = smoothstep(pc.bands.y - blend, pc.bands.y + blend, h);
    float grass = max(1.0 - sand - snow, 0.0);
    vec4 flat_ = vec4(sand, grass, 0.0, snow) * (1.0 - rock);
    vec4 w = flat_ + vec4(0.0, 0.0, rock, 0.0);
    return w / max(w.x + w.y + w.z + w.w, 1e-4);
}

vec3 lighting(vec3 n)
{
    vec3 l = lights.AmbientLightColor.rgb * lights.AmbientLightStrength;
    for (int i = 0; i < lights.size; i++)
    {
        LightStruct light = lights.lights[i];
        if (light.type == 1)
        {
            l += light.color * light.strength;
        }
        else if (light.type == 2)
        {
            l += light.color * light.strength * max(dot(n, normalize(light.direction)), 0.0);
        }
    }
    return l;
}

void main()
{
    if (pc.wireframe == 1)
    {
        FragColor = vec4(0.0);
        return;
    }
    vec3 n = normalize(vNormal);
    vec4 w = layerWeights(n);
    vec3 albedo = triplanar(0.0, n) * w.x + triplanar(1.0, n) * w.y + triplanar(2.0, n) * w.z + triplanar(3.0, n) * w.w;
    FragColor = vec4(albedo * lighting(n), 1.0);
}
