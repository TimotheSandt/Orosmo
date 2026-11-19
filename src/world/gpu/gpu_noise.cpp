#include "engine/world/gpu_noise.hpp"

#include "engine/rhi/shader_compiler.hpp"

#include <cstring>

namespace engine::world
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Traduction ligne pour ligne de src/world/noise.cpp et terrain.cpp. `precise` interdit au compilateur de fusionner ou de
// réordonner les calculs flottants (pas de FMA), comme -ffp-contract=off côté processeur.
constexpr const char *kSource = R"(#version 450
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, std430) buffer Heights { float heights[]; } outData;

layout(push_constant) uniform Push
{
    ivec4 tile;   // x, z, lod, résolution
    uvec4 seeds;  // graine, base, octaves, octaves de distorsion
    vec4 relief;  // fréquence, lacunarité, gain, espacement des échantillons
    vec4 warp;    // amplitude, fréquence, échelle de hauteur, décalage de hauteur
} pc;

const float kInvSqrt2 = 0.70710678118654752;
const vec2 kGradients2[8] = vec2[8](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0), vec2(kInvSqrt2, kInvSqrt2),
                                    vec2(-kInvSqrt2, kInvSqrt2), vec2(kInvSqrt2, -kInvSqrt2), vec2(-kInvSqrt2, -kInvSqrt2));
const float kPerlin2Scale = 1.41421356237309505;
const vec3 kSimplexGradients[12] = vec3[12](vec3(1, 1, 0), vec3(-1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(1, 0, 1), vec3(-1, 0, 1),
                                            vec3(1, 0, -1), vec3(-1, 0, -1), vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, 1, -1), vec3(0, -1, -1));
const float kF2 = 0.36602540378443865;
const float kG2 = 0.21132486540518712;

uint hashMix(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
uint hash2(int x, int y, uint seed) { return hashMix(uint(x) ^ hashMix(uint(y) ^ hashMix(seed))); }
uint deriveSeed(uint seed, uint salt) { return hashMix(seed ^ hashMix(salt + 0x9e3779b9u)); }

float fade(float t) { precise float r = t * t * t * (t * (t * 6.0 - 15.0) + 10.0); return r; }
float lerp1(float a, float b, float t) { precise float r = a + t * (b - a); return r; }
float grad2(uint h, float x, float y) { vec2 g = kGradients2[h & 7u]; precise float r = g.x * x + g.y * y; return r; }
float unit16(uint bits) { return float(bits & 0xffffu) * (1.0 / 65536.0); }

float perlin2(float x, float y, uint seed)
{
    int ix = int(floor(x));
    int iy = int(floor(y));
    precise float fx = x - float(ix);
    precise float fy = y - float(iy);
    precise float n00 = grad2(hash2(ix, iy, seed), fx, fy);
    precise float n10 = grad2(hash2(ix + 1, iy, seed), fx - 1.0, fy);
    precise float n01 = grad2(hash2(ix, iy + 1, seed), fx, fy - 1.0);
    precise float n11 = grad2(hash2(ix + 1, iy + 1, seed), fx - 1.0, fy - 1.0);
    precise float u = fade(fx);
    precise float v = fade(fy);
    precise float r = lerp1(lerp1(n00, n10, u), lerp1(n01, n11, u), v) * kPerlin2Scale;
    return r;
}

float simplexCorner(float cx, float cy, int gi, int gj, uint seed)
{
    precise float r = 0.5 - cx * cx - cy * cy;
    if (r <= 0.0)
    {
        return 0.0;
    }
    r *= r;
    vec3 g = kSimplexGradients[hash2(gi, gj, seed) % 12u];
    precise float out_ = r * r * (g.x * cx + g.y * cy);
    return out_;
}

float simplex2(float x, float y, uint seed)
{
    precise float s = (x + y) * kF2;
    int i = int(floor(x + s));
    int j = int(floor(y + s));
    precise float t = float(i + j) * kG2;
    precise float x0 = x - (float(i) - t);
    precise float y0 = y - (float(j) - t);
    int i1 = x0 > y0 ? 1 : 0;
    int j1 = x0 > y0 ? 0 : 1;
    precise float x1 = x0 - float(i1) + kG2;
    precise float y1 = y0 - float(j1) + kG2;
    precise float x2 = x0 - 1.0 + 2.0 * kG2;
    precise float y2 = y0 - 1.0 + 2.0 * kG2;
    precise float r = 70.0 * (simplexCorner(x0, y0, i, j, seed) + simplexCorner(x1, y1, i + i1, j + j1, seed) + simplexCorner(x2, y2, i + 1, j + 1, seed));
    return r;
}

float worleyF1(float x, float y, uint seed)
{
    int ix = int(floor(x));
    int iy = int(floor(y));
    precise float fx = x - float(ix);
    precise float fy = y - float(iy);
    precise float d1 = 1e30;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            uint h = hash2(ix + dx, iy + dy, seed);
            precise float px = float(dx) + unit16(h) - fx;
            precise float py = float(dy) + unit16(h >> 16) - fy;
            precise float d = px * px + py * py;
            d1 = min(d1, d);
        }
    }
    return sqrt(d1);
}

float basis2(uint basis, float x, float y, uint seed)
{
    if (basis == 1u) return simplex2(x, y, seed);
    if (basis == 2u) { precise float r = 1.0 - 2.0 * min(worleyF1(x, y, seed), 1.0); return r; }
    return perlin2(x, y, seed);
}

float fbm2(float x, float y, uint seed, uint basis, int octaves, float frequency, float lacunarity, float gain)
{
    octaves = clamp(octaves, 1, 16);
    precise float sum = 0.0;
    precise float norm = 0.0;
    precise float amplitude = 1.0;
    precise float f = frequency;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amplitude * basis2(basis, x * f, y * f, deriveSeed(seed, uint(o)));
        norm += amplitude;
        amplitude *= gain;
        f *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

float warpedFbm2(float x, float y, uint seed)
{
    if (pc.warp.x == 0.0)
    {
        return fbm2(x, y, seed, pc.seeds.y, int(pc.seeds.z), pc.relief.x, pc.relief.y, pc.relief.z);
    }
    precise float qx = fbm2(x, y, deriveSeed(seed, 0x51edu), 0u, int(pc.seeds.w), pc.warp.y, 2.0, 0.5);
    precise float qy = fbm2(x, y, deriveSeed(seed, 0x7a3bu), 0u, int(pc.seeds.w), pc.warp.y, 2.0, 0.5);
    precise float wx = x + pc.warp.x * qx;
    precise float wy = y + pc.warp.x * qy;
    return fbm2(wx, wy, seed, pc.seeds.y, int(pc.seeds.z), pc.relief.x, pc.relief.y, pc.relief.z);
}

float samplePosition(int tile, uint i)
{
    double global = double(tile) * double(pc.tile.w) + double(i);
    double finest = global * double(1 << pc.tile.z);
    return float(finest * double(pc.relief.w));
}

void main()
{
    uint n = uint(pc.tile.w) + 1u;
    uvec2 id = gl_GlobalInvocationID.xy;
    if (id.x >= n || id.y >= n)
    {
        return;
    }
    float x = samplePosition(pc.tile.x, id.x);
    float z = samplePosition(pc.tile.y, id.y);
    precise float h = pc.warp.w + pc.warp.z * warpedFbm2(x, z, pc.seeds.x);
    outData.heights[id.y * n + id.x] = h;
}
)";

struct Push
{
    std::int32_t tile[4];
    std::uint32_t seeds[4];
    float relief[4];
    float warp[4];
};
static_assert(sizeof(Push) == 64);

} // namespace

core::Result<std::unique_ptr<GpuNoise>> GpuNoise::create(rhi::Device &device)
{
    if (!device.capabilities().compute)
    {
        return makeError(ErrorCode::kUnsupported, "GpuNoise: the device has no compute shaders");
    }
    std::unique_ptr<GpuNoise> noise(new GpuNoise(device));
    auto shader = rhi::createShaderFromGlsl(device, rhi::ShaderStage::kCompute, kSource, {.name = "world/gpu/noise.comp"});
    if (!shader)
    {
        return std::unexpected(shader.error());
    }
    noise->shader_ = *shader;
    auto pipeline = device.createComputePipeline({.compute = *shader, .debugName = "world.noise"});
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    noise->pipeline_ = *pipeline;
    return noise;
}

GpuNoise::~GpuNoise()
{
    device_.destroy(pipeline_);
    device_.destroy(shader_);
}

core::Result<std::vector<float>> GpuNoise::generateTile(const TerrainParams &params, std::uint32_t seed, const TileKey &key)
{
    const std::uint32_t n = params.tileResolution + 1;
    const std::uint64_t bytes = static_cast<std::uint64_t>(n) * n * sizeof(float);
    rhi::BufferDesc bd;
    bd.size = bytes;
    bd.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopySrc;
    bd.memory = rhi::MemoryKind::kReadback;
    bd.debugName = "world.noise.heights";
    auto buffer = device_.createBuffer(bd);
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    rhi::BindGroupDesc gd;
    gd.entries.push_back({.binding = 0, .type = rhi::BindingType::kStorageBuffer, .buffer = *buffer});
    gd.debugName = "world.noise";
    auto group = device_.createBindGroup(gd);
    if (!group)
    {
        device_.destroy(*buffer);
        return std::unexpected(group.error());
    }
    Push push{};
    push.tile[0] = key.x;
    push.tile[1] = key.z;
    push.tile[2] = static_cast<std::int32_t>(key.lod);
    push.tile[3] = static_cast<std::int32_t>(params.tileResolution);
    push.seeds[0] = seed;
    push.seeds[1] = static_cast<std::uint32_t>(params.relief.basis);
    push.seeds[2] = static_cast<std::uint32_t>(params.relief.octaves);
    push.seeds[3] = static_cast<std::uint32_t>(params.warp.octaves);
    push.relief[0] = params.relief.frequency;
    push.relief[1] = params.relief.lacunarity;
    push.relief[2] = params.relief.gain;
    push.relief[3] = params.sampleSpacing;
    push.warp[0] = params.warp.amplitude;
    push.warp[1] = params.warp.frequency;
    push.warp[2] = params.heightScale;
    push.warp[3] = params.heightOffset;
    auto encoder = device_.createCommandEncoder();
    encoder->bindComputePipeline(pipeline_);
    encoder->setBindGroup(0, *group);
    encoder->setPushConstants(std::as_bytes(std::span(&push, 1)));
    encoder->dispatch((n + 7) / 8, (n + 7) / 8, 1);
    auto submitted = device_.submit(std::move(encoder));
    core::Result<std::vector<float>> out = submitted ? core::Result<std::vector<float>>(std::vector<float>{}) : std::unexpected(submitted.error());
    if (submitted)
    {
        auto data = device_.readBuffer(*buffer, 0, bytes);
        if (!data)
        {
            out = std::unexpected(data.error());
        }
        else
        {
            std::vector<float> heights(static_cast<std::size_t>(n) * n);
            std::memcpy(heights.data(), data->data(), bytes);
            out = std::move(heights);
        }
    }
    device_.destroy(*group);
    device_.destroy(*buffer);
    return out;
}

} // namespace engine::world
