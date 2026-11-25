// REN-22 : boîte à outils de shaders (shaders/include/orosmo/*.glsl). Chaque fichier est compilé sur chaque backend ; les
// fonctions pures sont exécutées en compute sur le GPU et comparées à une référence CPU (formules fermées ou réimplémentation
// documentée ci-dessous).
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <filesystem>
#include <string>

namespace
{

std::filesystem::path includeRoot() { return tests::repoRoot(__FILE__) / "shaders" / "include"; }

CompileOptions toolboxOptions(const char *name)
{
    CompileOptions o;
    o.name = name;
    o.includeResolver = fileIncludeResolver({includeRoot()});
    return o;
}

// ── Références CPU ──────────────────────────────────────────────────────────────────────────────────────────────
std::uint32_t pcg(std::uint32_t v)
{
    const std::uint32_t state = v * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
std::uint32_t pcg2(std::uint32_t x, std::uint32_t y) { return pcg(x ^ pcg(y)); }
float toUnit(std::uint32_t h) { return static_cast<float>(h >> 8u) * (1.0f / 16777216.0f); }
float hash21(int x, int y) { return toUnit(pcg2(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))); }

float quintic(float f) { return f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f); }
float valueNoise(float px, float py)
{
    const float ix = std::floor(px), iy = std::floor(py);
    const float ux = quintic(px - ix), uy = quintic(py - iy);
    const int cx = static_cast<int>(ix), cy = static_cast<int>(iy);
    const float a = hash21(cx, cy), b = hash21(cx + 1, cy), d = hash21(cx, cy + 1), e = hash21(cx + 1, cy + 1);
    const float top = a + (b - a) * ux, bottom = d + (e - d) * ux;
    return top + (bottom - top) * uy;
}
float fbm(float px, float py, int octaves, float lacunarity, float gain)
{
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * valueNoise(px, py);
        norm += amp;
        px *= lacunarity;
        py *= lacunarity;
        amp *= gain;
    }
    return sum / norm;
}

// Une chaîne par fichier d'inclusion : un fragment/compute minimal qui l'inclut et appelle une de ses fonctions.
struct IncludeCase
{
    const char *file;
    const char *body; // corps de main() d'un compute (écrit dans out_[0])
};

const IncludeCase kIncludeCases[] = {
    {"orosmo/math.glsl", "out_[0] = saturate(2.0) + sqr(2.0) + remap(1.0, 0.0, 2.0, 0.0, 4.0);"},
    {"orosmo/noise.glsl", "out_[0] = valueNoise(vec2(0.5)) + fbm(vec2(0.5), 3, 2.0, 0.5) + gradientNoise(vec2(0.5));"},
    {"orosmo/coords.glsl", "out_[0] = octDecode(octEncode(vec3(0.0, 0.0, 1.0))).z + ndcToUv(vec2(0.0)).x;"},
    {"orosmo/depth.glsl", "out_[0] = linearDepthInfiniteReverseZ(0.5, 0.1) + viewPositionFromDepth(vec2(0.5), 0.5, mat4(1.0)).x;"},
    {"orosmo/pbr.glsl", "out_[0] = brdfGGX(vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(1), 0.0, 0.5).x;"},
};

std::string computeWith(const char *file, const char *body)
{
    return std::string("#version 450\n#include \"") + file +
           "\"\nlayout(local_size_x = 1) in;\nlayout(set = 0, binding = 1, std430) buffer Out { float out_[]; };\nvoid main() { " + body + " }\n";
}

} // namespace

TEST_SUITE("renderer/shader-toolbox")
{
    TEST_CASE("every toolbox include compiles as a compute shader on every backend")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            for (const IncludeCase &c : kIncludeCases)
            {
                INFO(c.file);
                auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, computeWith(c.file, c.body), toolboxOptions(c.file));
                INFO((shader ? std::string() : shader.error().message));
                CHECK(shader.has_value());
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("every toolbox include compiles as a fragment shader, and the shadow include with a comparison sampler")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            for (const IncludeCase &c : kIncludeCases)
            {
                const std::string fs = std::string("#version 450\n#include \"") + c.file +
                                       "\"\nlayout(location = 0) out vec4 color;\nvoid main() { float out_[1]; " + c.body + " color = vec4(out_[0]); }\n";
                INFO(c.file);
                auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kFragment, fs, toolboxOptions(c.file));
                INFO((shader ? std::string() : shader.error().message));
                CHECK(shader.has_value());
            }
            const char *shadowFs = R"(#version 450
#include "orosmo/shadow.glsl"
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;
layout(location = 0) out vec4 color;
void main() { color = vec4(shadowPcf(shadowMap, vec3(0.5), vec2(0.01), 1) * shadowSlopeBias(0.5, 0.001, 0.002)); })";
            auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kFragment, shadowFs, toolboxOptions("shadow"));
            INFO((shader ? std::string() : shader.error().message));
            CHECK(shader.has_value());
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("an include that does not exist is a compile error, not a silent skip")
    {
        auto r = compileGlsl(ShaderStage::kFragment, "#version 450\n#include \"orosmo/absent.glsl\"\nvoid main() {}\n", toolboxOptions("absent"));
        CHECK_FALSE(r.has_value());
    }

    TEST_CASE("pure functions evaluated on the GPU match their CPU references (math, noise, coords, depth, pbr)")
    {
        // Matrices de projection comme Camera : reverse-Z, zéro-à-un, plan lointain fini ou infini.
        const float nearP = 0.1f, farP = 500.0f, fov = glm::radians(60.0f), aspect = 1.5f;
        const glm::mat4 finite = glm::perspectiveRH_ZO(fov, aspect, farP, nearP);
        glm::mat4 infinite(0.0f);
        const float f = 1.0f / std::tan(0.5f * fov);
        infinite[0][0] = f / aspect;
        infinite[1][1] = f;
        infinite[2][3] = -1.0f;
        infinite[3][2] = nearP;
        const glm::vec3 viewPos(1.0f, -2.0f, -10.0f);
        auto project = [&](const glm::mat4 &p, glm::vec3 &uvDepth)
        {
            const glm::vec4 clip = p * glm::vec4(viewPos, 1.0f);
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            uvDepth = glm::vec3(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f, ndc.z);
        };
        glm::vec3 uvdFinite, uvdInfinite;
        project(finite, uvdFinite);
        project(infinite, uvdInfinite);
        struct In
        {
            glm::mat4 invFinite;
            glm::mat4 invInfinite;
            glm::vec4 uvdFinite;
            glm::vec4 uvdInfinite;
            glm::vec4 nearFar;
        } in{glm::inverse(finite), glm::inverse(infinite), glm::vec4(uvdFinite, 0), glm::vec4(uvdInfinite, 0), glm::vec4(nearP, farP, 0, 0)};

        const char *cs = R"(#version 450
#include "orosmo/noise.glsl"
#include "orosmo/coords.glsl"
#include "orosmo/depth.glsl"
#include "orosmo/pbr.glsl"
layout(local_size_x = 1) in;
layout(set = 0, binding = 1, std430) buffer Out { uint o[]; };
layout(set = 0, binding = 2, std430) readonly buffer In { mat4 invFinite; mat4 invInfinite; vec4 uvdFinite; vec4 uvdInfinite; vec4 nearFar; };
void put(int i, float v) { o[i] = floatBitsToUint(v); }
void put3(int i, vec3 v) { put(i, v.x); put(i + 1, v.y); put(i + 2, v.z); }
void main()
{
    o[0] = pcgHash(12345u);
    o[1] = pcgHash(uvec2(3u, 7u));
    put(2, hash21(ivec2(5, 9)));
    put(3, valueNoise(vec2(5.0, 9.0)));
    put(4, valueNoise(vec2(2.37, -4.81)));
    put(5, gradientNoise(vec2(3.0, 4.0)));
    put(6, fbm(vec2(1.3, 2.7), 4, 2.0, 0.5));
    put(7, saturate(1.5));
    put(8, remap(5.0, 0.0, 10.0, 100.0, 200.0));
    put(9, srgbToLinear(vec3(0.5)).x);
    put(10, linearToSrgb(srgbToLinear(vec3(0.25))).x);
    put3(11, octDecode(octEncode(normalize(vec3(1.0, 2.0, -3.0)))));
    put3(14, octDecode(octEncode(normalize(vec3(-1.0, 0.5, 2.0)))));
    vec2 s = dirToSpherical(normalize(vec3(0.3, 0.4, -0.8)));
    put3(17, sphericalToDir(s.x, s.y));
    put3(20, viewPositionFromDepth(uvdFinite.xy, uvdFinite.z, invFinite));
    put3(23, viewPositionFromDepth(uvdInfinite.xy, uvdInfinite.z, invInfinite));
    put(26, linearDepth(uvdFinite.z, invFinite));
    put(27, linearDepth(uvdInfinite.z, invInfinite));
    put(28, linearDepthReverseZ(uvdFinite.z, nearFar.x, nearFar.y));
    put(29, linearDepthInfiniteReverseZ(uvdInfinite.z, nearFar.x));
    put(30, distributionGGX(1.0, 0.5));
    put(31, visibilitySmithGGXCorrelated(1.0, 1.0, 0.5));
    put3(32, fresnelSchlick(1.0, vec3(0.04)));
    put3(35, fresnelSchlick(0.0, vec3(0.04)));
    put3(38, brdfGGX(vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(1.0), 0.0, 0.5));
    put3(41, brdfGGX(vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, -1), vec3(1.0), 0.0, 0.5));
    vec3 n = vec3(0, 0, 1);
    vec3 a = normalize(vec3(0.3, 0.2, 0.9));
    vec3 b = normalize(vec3(-0.4, 0.1, 0.7));
    put(44, brdfGGX(n, a, b, vec3(0.8, 0.5, 0.2), 0.3, 0.6).x / dot(n, b));
    put(45, brdfGGX(n, b, a, vec3(0.8, 0.5, 0.2), 0.3, 0.6).x / dot(n, a));
    float sum = 0.0;
    for (int i = 0; i < 8; ++i) { sum += orthonormalBasisDot(i); }
    put(46, sum);
    put(47, remap(1.5e-7, 1.0e-7, 2.0e-7, 10.0, 20.0)); // intervalle non nul mais minuscule : remappé (milieu)
    put(48, remap(2.0e-7, 0.0, 2.0e-7, 10.0, 20.0));    // x = b donne d
    put(49, remap(3.0, 2.0, 2.0, 7.0, 9.0));            // a == b donne c
    put(50, srgbToLinear(vec3(-0.5)).x);                // négatif : branche linéaire, pas de NaN
    put(51, linearToSrgb(vec3(-0.5)).x);
    put3(52, vec3(dirToSpherical(vec3(0.0, 1.0, 0.0)), 0.0));  // pôle nord : azimut conventionnel 0
    put3(55, vec3(dirToSpherical(vec3(0.0, -1.0, 0.0)), 0.0)); // pôle sud
}
)";
        // orthonormalBasisDot est définie ici pour exercer orthonormalBasis : t, b et n doivent être orthonormaux.
        std::string source = cs;
        source.replace(source.find("layout(local_size_x"), 0,
                       "float orthonormalBasisDot(int i)\n{\n    vec3 n = normalize(vec3(float(i) - 3.5, 1.0 + float(i), 2.0 - float(i)));\n"
                       "    vec3 t, b; orthonormalBasis(n, t, b);\n    return abs(dot(t, n)) + abs(dot(b, n)) + abs(dot(t, b)) + abs(length(t) - 1.0) + abs(length(b) - 1.0);\n}\n");

        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, source, toolboxOptions("values"));
            INFO((shader ? std::string() : shader.error().message));
            REQUIRE(shader.has_value());
            auto pipe = gpu->createComputePipeline({.compute = *shader});
            REQUIRE(pipe.has_value());
            const BufferHandle out = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 64 * 4);
            const BufferHandle inBuf = gpu.buffer(BufferUsage::kStorage, bytesOf(in));
            BindGroupDesc g;
            g.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = out}, {.binding = 2, .type = BindingType::kStorageBuffer, .buffer = inBuf}};
            auto group = gpu->createBindGroup(g);
            REQUIRE(group.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(1, 1, 1);
                });
            auto data = gpu->readBuffer(out, 0, 64 * 4);
            REQUIRE(data.has_value());
            std::uint32_t u[64];
            float v[64];
            std::memcpy(u, data->data(), sizeof u);
            std::memcpy(v, data->data(), sizeof v);
            const auto approx = [](float x) { return doctest::Approx(x).epsilon(1e-4).scale(1.0); };
            const auto near3 = [&](int i, glm::vec3 ref, float eps = 1e-4f)
            {
                for (int k = 0; k < 3; ++k)
                {
                    CHECK(v[i + k] == doctest::Approx(ref[k]).epsilon(eps).scale(1.0));
                }
            };

            // hachage : exact, bit à bit
            CHECK(u[0] == pcg(12345u));
            CHECK(u[1] == pcg2(3u, 7u));
            CHECK(v[2] == hash21(5, 9));
            // bruit
            CHECK(v[3] == hash21(5, 9)); // aux coins entiers : la valeur du coin
            CHECK(v[4] == approx(valueNoise(2.37f, -4.81f)));
            CHECK(std::abs(v[5]) < 1e-6f); // bruit de gradient nul aux coins
            CHECK(v[6] == approx(fbm(1.3f, 2.7f, 4, 2.0f, 0.5f)));
            CHECK((v[6] >= 0.0f && v[6] <= 1.0f));
            // maths
            CHECK(v[7] == 1.0f);
            CHECK(v[8] == 150.0f);
            CHECK(v[9] == approx(0.21404114f));
            CHECK(v[10] == approx(0.25f));
            CHECK(v[47] == approx(15.0f));
            CHECK(v[48] == approx(20.0f));
            CHECK(v[49] == 7.0f);
            CHECK(v[50] == approx(-0.5f / 12.92f));
            CHECK(v[51] == approx(-0.5f * 12.92f));
            near3(52, glm::vec3(0.0f, glm::half_pi<float>(), 0.0f), 1e-5f);
            near3(55, glm::vec3(0.0f, -glm::half_pi<float>(), 0.0f), 1e-5f);
            // coordonnées : allers-retours
            near3(11, glm::normalize(glm::vec3(1, 2, -3)), 1e-3f);
            near3(14, glm::normalize(glm::vec3(-1, 0.5f, 2)), 1e-3f);
            near3(17, glm::normalize(glm::vec3(0.3f, 0.4f, -0.8f)), 1e-3f);
            // profondeur : reconstruction de la position d'espace vue, plan lointain fini et infini
            near3(20, viewPos, 2e-3f);
            near3(23, viewPos, 2e-3f);
            CHECK(v[26] == doctest::Approx(10.0f).epsilon(2e-3));
            CHECK(v[27] == doctest::Approx(10.0f).epsilon(2e-3));
            CHECK(v[28] == doctest::Approx(10.0f).epsilon(2e-3));
            CHECK(v[29] == doctest::Approx(10.0f).epsilon(2e-3));
            // PBR : valeurs fermées. alpha = 0.5 : D(h = n) = 1 / (pi alpha^2) ; V(1, 1) = 1/4 ; Fresnel : F(0°) = f0, F(90°) = 1.
            CHECK(v[30] == approx(1.0f / (3.14159265f * 0.25f)));
            CHECK(v[31] == approx(0.25f));
            near3(32, glm::vec3(0.04f));
            near3(35, glm::vec3(1.0f));
            // n = v = l, albedo 1, rugosité 0.5 (alpha 0.25), diélectrique : (1 - F) / pi + F D V, nDotL = 1.
            {
                const double pi = 3.14159265358979323846, a2 = 0.0625, F = 0.04;
                const double D = a2 / (pi * (a2) * (a2)); // nDotH = 1 : d = a2, D = a2 / (pi a2^2) = 1 / (pi a2)
                const double expected = (1.0 - F) / pi + F * D * 0.25 * 1.0; // V(1, 1, a) = 0.25 quel que soit a
                near3(38, glm::vec3(static_cast<float>(expected)), 1e-4f);
            }
            near3(41, glm::vec3(0.0f)); // lumière derrière la surface
            CHECK(v[44] == approx(v[45])); // réciprocité d'Helmholtz : f(v, l) = f(l, v)
            CHECK(v[44] > 0.0f);
            CHECK(v[46] < 1e-3f); // base orthonormée
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("shadowPcf: comparison sampling against a depth map, border, radius and receiver bias")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4}, "color");
            const TextureHandle depth = gpu.target(Format::kD32Float, {4, 4}, "shadow-depth");
            gpu.run(
                [&](CommandEncoder &e)
                {
                    RenderPassDesc pass = passOn(color);
                    pass.depth = DepthAttachment{.texture = depth, .clearDepth = 0.5f}; // occulteur à 0.5 partout
                    e.beginRenderPass(pass);
                    e.endRenderPass();
                });
            auto sampler = gpu->createSampler({.minFilter = Filter::kNearest,
                                               .magFilter = Filter::kNearest,
                                               .mipFilter = std::nullopt,
                                               .addressU = AddressMode::kClampToEdge,
                                               .addressV = AddressMode::kClampToEdge,
                                               .compare = CompareOp::kLessEqual});
            REQUIRE(sampler.has_value());

            const char *cs = R"(#version 450
#include "orosmo/shadow.glsl"
layout(local_size_x = 1) in;
layout(set = 0, binding = 1, std430) buffer Out { float o[]; };
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;
void main()
{
    vec2 texel = vec2(0.25);
    o[0] = shadowPcf(shadowMap, vec3(0.5, 0.5, 0.4), texel, 0);  // récepteur devant l'occulteur : éclairé
    o[1] = shadowPcf(shadowMap, vec3(0.5, 0.5, 0.6), texel, 0);  // derrière : dans l'ombre
    o[2] = shadowPcf(shadowMap, vec3(0.5, 0.5, 0.6), texel, 1);  // PCF 3x3 sur profondeur uniforme : toujours dans l'ombre
    o[3] = shadowPcf(shadowMap, vec3(1.5, 0.5, 0.6), texel, 1);  // hors de la carte : pas d'ombre
    o[4] = shadowPcf(shadowMap, vec3(0.5, 0.5, 1.5), texel, 0);  // plan lointain dépassé : pas d'ombre
    o[5] = shadowSlopeBias(1.0, 0.001, 0.002);                  // face à la lumière : biais constant seul
    o[6] = shadowSlopeBias(0.5, 0.001, 0.002);                  // tan(60°) = 1.7320508
}
)";
            auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, cs, toolboxOptions("pcf"));
            INFO((shader ? std::string() : shader.error().message));
            REQUIRE(shader.has_value());
            auto pipe = gpu->createComputePipeline({.compute = *shader});
            REQUIRE(pipe.has_value());
            const BufferHandle out = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, 32);
            BindGroupDesc g;
            g.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = out},
                         {.binding = 2, .type = BindingType::kSampledTexture, .texture = depth, .sampler = *sampler}};
            auto group = gpu->createBindGroup(g);
            REQUIRE(group.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(1, 1, 1);
                });
            auto data = gpu->readBuffer(out, 0, 32);
            REQUIRE(data.has_value());
            float v[8];
            std::memcpy(v, data->data(), sizeof v);
            CHECK(v[0] == 1.0f);
            CHECK(v[1] == 0.0f);
            CHECK(v[2] == 0.0f);
            CHECK(v[3] == 1.0f);
            CHECK(v[4] == 1.0f);
            CHECK(v[5] == doctest::Approx(0.001f));
            CHECK(v[6] == doctest::Approx(0.001f + 0.002f * 1.7320508f).epsilon(1e-4));
            gpu.expectNoValidationErrors();
        }
    }
}
