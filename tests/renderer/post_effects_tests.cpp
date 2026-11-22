// REN-17 (HDR, exposition, tonemapping, sRGB) et REN-18 (bloom, exposition automatique par histogramme, LUT, vignettage).
// Suite CPU « post/math » : références de tonemap.hpp (sans GPU). Suite GPU « renderer/post » : fonctions GLSL comparées aux
// références CPU, chaque effet exécuté dans un graphe sur une image connue (valeurs lues), chaîne complète dans SceneRenderer
// (ordre, entrées déclarées, scène RGBA16F seulement si un effet HDR est actif). Sur chaque backend, validation à 0.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/post_effects.hpp>
#include <orosmo/renderer/render_graph.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/tonemap.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;
namespace core = orosmo::core;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

const rd::Tonemapper kOperators[] = {rd::Tonemapper::kClamp, rd::Tonemapper::kAces, rd::Tonemapper::kAgx, rd::Tonemapper::kReinhardExtended};

// ── Aides GPU ───────────────────────────────────────────────────────────────────────────────────────────────────
struct PostGpu
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::shared_ptr<rd::PostContext> context;

    explicit PostGpu(Backend backend) : gpu(backend, {16, 16})
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::PostContext::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        context = *created;
    }
    ~PostGpu()
    {
        context.reset();
        pipelines.reset();
        library.reset();
    }
};

std::vector<std::uint16_t> toHalves(const std::vector<glm::vec4> &pixels)
{
    std::vector<std::uint16_t> out;
    for (const glm::vec4 &p : pixels)
    {
        for (int c = 0; c < 4; ++c)
        {
            out.push_back(glm::packHalf1x16(p[c]));
        }
    }
    return out;
}

glm::vec4 halfToVec(const std::uint16_t *h) { return {glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]), glm::unpackHalf1x16(h[3])}; }

// Valeur effectivement stockée par une texture RGBA16F.
glm::vec4 roundHalf(const glm::vec4 &v)
{
    glm::vec4 r;
    for (int c = 0; c < 4; ++c)
    {
        r[c] = glm::unpackHalf1x16(glm::packHalf1x16(v[c]));
    }
    return r;
}

TextureHandle imageTexture(Gpu &gpu, Format format, Extent2D size, std::span<const std::byte> data)
{
    TextureDesc d;
    d.format = format;
    d.size = {size.width, size.height, 1};
    d.usage = TextureUsage::kSampled | TextureUsage::kRenderTarget | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
    d.debugName = "post.input";
    auto t = gpu->createTexture(d);
    REQUIRE(t.has_value());
    REQUIRE(gpu->updateTexture(*t, {}, data).has_value());
    return *t;
}

TextureHandle hdrTexture(Gpu &gpu, Extent2D size, const std::vector<glm::vec4> &pixels)
{
    const auto halves = toHalves(pixels);
    return imageTexture(gpu, Format::kRGBA16Float, size, std::as_bytes(std::span(halves)));
}

std::vector<glm::vec4> readHdr(Gpu &gpu, TextureHandle t)
{
    auto raw = gpu->readTexture(t);
    REQUIRE(raw.has_value());
    std::vector<glm::vec4> out(raw->size() / 8);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        std::uint16_t h[4];
        std::memcpy(h, raw->data() + i * 8, sizeof h);
        out[i] = halfToVec(h);
    }
    return out;
}

// Exécute les effets dans un graphe dont la « scène » est `texture` (comme le fait SceneRenderer après sa passe de scène).
std::vector<std::string> runEffects(PostGpu &g, std::initializer_list<rd::PostEffect *> effects, TextureHandle texture, Format format, Extent2D size,
                                    const rd::FrameData &frame = {})
{
    rd::RenderGraph graph(*g.gpu.device);
    rd::RenderGraphBuilder::Resources res;
    res.sceneColor = graph.importTexture("scene.color", texture, {.format = format, .size = size});
    res.renderSize = size;
    res.outputSize = size;
    // Comme la passe « scene » de SceneRenderer : un écrivain de la scène déclaré avant les effets (ici, contenu déjà envoyé).
    graph.addPass("scene", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).write(res.sceneColor);
    rd::RenderGraphBuilder builder(graph, res, g.gpu->capabilities(), "scene");
    for (rd::PostEffect *e : effects)
    {
        REQUIRE(e->prepare(frame).has_value());
        auto declared = e->declarePasses(builder);
        INFO((declared ? std::string() : declared.error().message));
        REQUIRE(declared.has_value());
    }
    g.gpu.run(
        [&](CommandEncoder &e)
        {
            auto r = graph.execute(e);
            INFO((r ? std::string() : r.error().message));
            REQUIRE(r.has_value());
        });
    return graph.executionOrder();
}

std::ptrdiff_t position(const std::vector<std::string> &names, const std::string &name)
{
    return std::distance(names.begin(), std::find(names.begin(), names.end(), name));
}

bool contains(const std::vector<std::string> &names, const std::string &name) { return std::find(names.begin(), names.end(), name) != names.end(); }

} // namespace

TEST_SUITE("post/math")
{
    TEST_CASE("tonemapping references: range, monotony, fixed points of each operator")
    {
        for (const rd::Tonemapper op : kOperators)
        {
            INFO(rd::toString(op));
            CHECK(rd::parseTonemapper(rd::toString(op)) == op);
            float previous = -1.0f;
            for (float x = 0.0f; x < 64.0f; x = x * 1.5f + 0.01f)
            {
                const glm::vec3 y = rd::tonemap(op, glm::vec3(x));
                for (int c = 0; c < 3; ++c)
                {
                    CHECK((y[c] >= 0.0f && y[c] <= 1.0f));
                }
                CHECK(y.g >= previous - 1e-6f); // gris : croissante
                previous = y.g;
            }
            CHECK(rd::tonemap(op, glm::vec3(-1.0f)).r == doctest::Approx(rd::tonemap(op, glm::vec3(0.0f)).r)); // négatifs bornés
        }
        CHECK_FALSE(rd::parseTonemapper("filmic").has_value());
        CHECK(rd::tonemap(rd::Tonemapper::kClamp, {0.25f, 2.0f, 0.5f}) == glm::vec3(0.25f, 1.0f, 0.5f));
        // Reinhard étendu : L (1 + L / w²) / (1 + L) ; le point blanc devient 1, 1 donne (1 + 1/16) / 2 pour w = 4.
        CHECK(rd::tonemap(rd::Tonemapper::kReinhardExtended, glm::vec3(4.0f), 4.0f).g == doctest::Approx(1.0f));
        CHECK(rd::tonemap(rd::Tonemapper::kReinhardExtended, glm::vec3(1.0f), 4.0f).g == doctest::Approx(17.0f / 32.0f));
        // ACES (ajustement de Hill) : noir presque noir, très clair presque blanc, compresse le blanc de scène.
        CHECK(rd::tonemap(rd::Tonemapper::kAces, glm::vec3(0.0f)).g < 0.01f);
        CHECK(rd::tonemap(rd::Tonemapper::kAces, glm::vec3(100.0f)).g > 0.95f);
        CHECK(rd::tonemap(rd::Tonemapper::kAces, glm::vec3(1.0f)).g < 0.9f);
        // AgX : le gris moyen 0,18 reste dans les tons moyens, désature les couleurs très saturées et lumineuses.
        const float agxMid = rd::tonemap(rd::Tonemapper::kAgx, glm::vec3(0.18f)).g;
        CHECK((agxMid > 0.1f && agxMid < 0.4f));
        const glm::vec3 hot = rd::tonemap(rd::Tonemapper::kAgx, glm::vec3(50.0f, 0.0f, 0.0f));
        CHECK(hot.g > 0.05f);
    }

    TEST_CASE("sRGB curve: closed values and round trip")
    {
        CHECK(rd::linearToSrgb(glm::vec3(0.0f)).r == 0.0f);
        CHECK(rd::linearToSrgb(glm::vec3(1.0f)).r == doctest::Approx(1.0f));
        CHECK(rd::linearToSrgb(glm::vec3(0.214041f)).r == doctest::Approx(0.5f).epsilon(1e-4));
        CHECK(rd::linearToSrgb(glm::vec3(0.002f)).r == doctest::Approx(0.002f * 12.92f));
        for (float x = 0.0f; x <= 1.0f; x += 0.05f)
        {
            CHECK(rd::srgbToLinear(rd::linearToSrgb(glm::vec3(x))).r == doctest::Approx(x).epsilon(1e-5).scale(1.0));
        }
    }

    TEST_CASE("luminance histogram and exposure references")
    {
        CHECK(rd::histogramBin(0.0f, -10.0f, 6.0f) == 0);
        CHECK(rd::histogramBin(std::nanf(""), -10.0f, 6.0f) == 0);
        CHECK(rd::histogramBin(std::exp2(-10.0f), -10.0f, 6.0f) == 1);
        CHECK(rd::histogramBin(std::exp2(6.0f), -10.0f, 6.0f) == 255);
        CHECK(rd::histogramBin(1e6f, -10.0f, 6.0f) == 255);
        CHECK(rd::histogramBin(1.0f, -10.0f, 6.0f) == 159); // t = 10/16, 0.625 × 254 + 1 = 159.75
        std::vector<std::uint32_t> bins(rd::kHistogramBins, 0);
        bins[0] = 1000; // trop sombre : ignoré
        bins[rd::histogramBin(1.0f, -10.0f, 6.0f)] = 50;
        bins[rd::histogramBin(0.25f, -10.0f, 6.0f)] = 50;
        const float average = rd::histogramAverageLuminance(bins, -10.0f, 6.0f);
        CHECK(average == doctest::Approx(0.5f).epsilon(0.05)); // moyenne géométrique de 1 et 0,25, à une demi-case près
        CHECK(rd::histogramAverageLuminance(std::vector<std::uint32_t>(rd::kHistogramBins, 0), -10.0f, 6.0f) == doctest::Approx(std::exp2(-10.0f)));
        CHECK(rd::targetExposure(0.36f, 0.18f, -6.0f, 6.0f) == doctest::Approx(0.5f));
        CHECK(rd::targetExposure(1e-9f, 0.18f, -6.0f, 6.0f) == doctest::Approx(64.0f)); // borné
        CHECK(rd::adaptationFactor(0.0f, 2.0f) == 1.0f);
        CHECK(rd::adaptationFactor(1.0f, 0.0f) == 1.0f);
        CHECK(rd::adaptationFactor(0.5f, 2.0f) == doctest::Approx(1.0f - std::exp(-1.0f)));
    }

    TEST_CASE("vignette reference: center untouched, corners darkened by the intensity")
    {
        const rd::VignetteSettings s;
        CHECK(rd::vignetteFactor({0.5f, 0.5f}, 16.0f / 9.0f, s) == 1.0f);
        CHECK(rd::vignetteFactor({0.0f, 0.0f}, 16.0f / 9.0f, s) == doctest::Approx(1.0f - s.intensity));
        CHECK(rd::vignetteFactor({1.0f, 1.0f}, 1.0f, s) == doctest::Approx(1.0f - s.intensity));
        const float edge = rd::vignetteFactor({1.0f, 0.5f}, 16.0f / 9.0f, s);
        CHECK((edge < 1.0f && edge > 1.0f - s.intensity));
    }

    TEST_CASE("3D LUT: identity sampling, trilinear interpolation, .cube parsing")
    {
        const rd::ColorLut id = rd::identityLut(17);
        REQUIRE(id.valid());
        for (const glm::vec3 c : {glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.123f, 0.5f, 0.987f), glm::vec3(2.0f, -1.0f, 0.3f)})
        {
            const glm::vec3 expected = glm::clamp(c, 0.0f, 1.0f);
            const glm::vec3 got = rd::sampleLut(id, c);
            for (int k = 0; k < 3; ++k)
            {
                CHECK(got[k] == doctest::Approx(expected[k]).epsilon(1e-5).scale(1.0));
            }
        }
        const char *cube = "# commentaire\nTITLE \"swap\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n"
                           "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        auto parsed = rd::parseCubeLut(cube);
        REQUIRE(parsed.has_value());
        CHECK(parsed->size == 2);
        CHECK(parsed->values[1] == glm::vec3(1.0f, 0.0f, 0.0f)); // rouge le plus rapide
        CHECK(parsed->values[2] == glm::vec3(0.0f, 1.0f, 0.0f));
        CHECK(rd::sampleLut(*parsed, {0.25f, 0.5f, 0.75f}).b == doctest::Approx(0.75f));
        CHECK((tests::errorOf(rd::parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n")) == ErrorCode::kInvalidArgument)); // compte faux
        CHECK((tests::errorOf(rd::parseCubeLut("0 0 0\n")) == ErrorCode::kInvalidArgument));                // taille absente
        CHECK((tests::errorOf(rd::parseCubeLut("LUT_3D_SIZE 1\n")) == ErrorCode::kInvalidArgument));
        CHECK((tests::errorOf(rd::parseCubeLut("LUT_3D_SIZE 2\n0 0 x\n")) == ErrorCode::kInvalidArgument));
        CHECK((tests::errorOf(rd::parseCubeLut("LUT_1D_SIZE 4\n")) == ErrorCode::kUnsupported));
        CHECK((tests::errorOf(rd::parseCubeLut("DOMAIN_MAX 2 2 2\n")) == ErrorCode::kUnsupported));
    }
}

TEST_SUITE("renderer/post")
{
    TEST_CASE("GLSL post functions match the CPU references (tonemap operators, sRGB, histogram bins, vignette)")
    {
        // Entrées : couleurs HDR variées ; sorties : 4 opérateurs + sRGB du résultat ACES + case + vignettage.
        const std::vector<glm::vec4> colors = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.01f, 0.01f, 0.01f, 0.0f}, {0.18f, 0.18f, 0.18f, 0.0f}, {0.5f, 0.3f, 0.1f, 0.0f},
                                               {1.0f, 1.0f, 1.0f, 0.0f},  {2.0f, 0.5f, 0.1f, 0.0f},    {4.0f, 4.0f, 4.0f, 0.0f},    {16.0f, 2.0f, 30.0f, 0.0f},
                                               {100.0f, 100.0f, 100.0f, 0.0f}, {0.05f, 3.0f, 0.7f, 0.0f}};
        constexpr int kOut = 6 * 4; // par couleur : 4 opérateurs (vec4), sRGB (vec4), case + vignettage (vec4)
        const char *cs = R"(#version 450
#include "post/post_common.glsl"
layout(local_size_x = 1) in;
layout(set = 0, binding = 1, std430) buffer Out { vec4 o[]; };
layout(set = 0, binding = 2, std430) readonly buffer In { vec4 colors[]; };
void main()
{
    uint i = gl_WorkGroupID.x;
    vec3 c = colors[i].rgb;
    for (int op = 0; op < 4; ++op) { o[i * 6u + uint(op)] = vec4(tonemap(op, c, 4.0), 0.0); }
    o[i * 6u + 4u] = vec4(linearToSrgb(tonemap(kTonemapAces, c, 4.0)), 0.0);
    vec2 uv = vec2(float(i) / 9.0, 1.0 - float(i) / 13.0);
    o[i * 6u + 5u] = vec4(float(histogramBin(luminance(c), -10.0, 6.0)), vignetteFactor(uv, 1.7, vec4(0.35, 0.55, 0.45, 1.0)),
                          vignetteFactor(uv, 1.7, vec4(0.8, 0.2, 0.3, 0.0)), 0.0);
}
)";
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            CompileOptions options;
            options.name = "post_values";
            options.includeResolver = fileIncludeResolver({shaderDir()});
            auto shader = createShaderFromGlsl(*gpu.device, ShaderStage::kCompute, cs, options);
            INFO((shader ? std::string() : shader.error().message));
            REQUIRE(shader.has_value());
            auto pipe = gpu->createComputePipeline({.compute = *shader});
            REQUIRE(pipe.has_value());
            const std::uint64_t outBytes = colors.size() * kOut * sizeof(float);
            const BufferHandle out = gpu.buffer(BufferUsage::kStorage | BufferUsage::kCopySrc, {}, outBytes);
            const BufferHandle in = gpu.buffer(BufferUsage::kStorage, bytesOf(colors));
            BindGroupDesc g;
            g.entries = {{.binding = 1, .type = BindingType::kStorageBuffer, .buffer = out}, {.binding = 2, .type = BindingType::kStorageBuffer, .buffer = in}};
            auto group = gpu->createBindGroup(g);
            REQUIRE(group.has_value());
            gpu.run(
                [&](CommandEncoder &e)
                {
                    e.bindComputePipeline(*pipe);
                    e.setBindGroup(0, *group);
                    e.dispatch(static_cast<std::uint32_t>(colors.size()), 1, 1);
                });
            auto data = gpu->readBuffer(out, 0, outBytes);
            REQUIRE(data.has_value());
            std::vector<float> v(colors.size() * kOut);
            std::memcpy(v.data(), data->data(), outBytes);
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                const glm::vec3 c(colors[i]);
                const float *o = v.data() + i * kOut;
                for (int op = 0; op < 4; ++op)
                {
                    const glm::vec3 ref = rd::tonemap(kOperators[op], c, 4.0f);
                    for (int k = 0; k < 3; ++k)
                    {
                        INFO("color ", i, " operator ", rd::toString(kOperators[op]), " channel ", k);
                        CHECK(o[op * 4 + k] == doctest::Approx(ref[k]).epsilon(2e-4).scale(1.0));
                    }
                }
                const glm::vec3 srgb = rd::linearToSrgb(rd::tonemap(rd::Tonemapper::kAces, c));
                for (int k = 0; k < 3; ++k)
                {
                    CHECK(o[16 + k] == doctest::Approx(srgb[k]).epsilon(2e-4).scale(1.0));
                }
                INFO("color ", i);
                CHECK(o[20] == static_cast<float>(rd::histogramBin(rd::luminance(c), -10.0f, 6.0f)));
                const glm::vec2 uv(static_cast<float>(i) / 9.0f, 1.0f - static_cast<float>(i) / 13.0f);
                CHECK(o[21] == doctest::Approx(rd::vignetteFactor(uv, 1.7f, {0.35f, 0.55f, 0.45f, 1.0f})).epsilon(1e-4).scale(1.0));
                CHECK(o[22] == doctest::Approx(rd::vignetteFactor(uv, 1.7f, {0.8f, 0.2f, 0.3f, 0.0f})).epsilon(1e-4).scale(1.0));
            }
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("tonemap feature: exposure x operator x sRGB, pixel by pixel, for every operator")
    {
        const Extent2D size{8, 4};
        std::vector<glm::vec4> pixels;
        for (std::uint32_t i = 0; i < size.width * size.height; ++i)
        {
            const float x = 0.02f * std::pow(1.45f, static_cast<float>(i));
            pixels.push_back({x, x * 0.5f, std::fmod(x * 7.0f, 3.0f), 1.0f});
        }
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            auto created = rd::TonemapFeature::create(g.context);
            REQUIRE(created.has_value());
            rd::TonemapFeature &tonemap = **created;
            for (const rd::Tonemapper op : kOperators)
            {
                for (const bool srgb : {true, false})
                {
                    INFO(rd::toString(op), " srgb ", srgb);
                    tonemap.settings().tonemapper = op;
                    tonemap.settings().exposureEv = 1.0f; // × 2
                    tonemap.settings().encodeSrgb = srgb;
                    rd::FrameData frame;
                    frame.exposure.x = 0.75f; // FrameEnvironment::exposure
                    const TextureHandle scene = hdrTexture(g.gpu, size, pixels);
                    const auto order = runEffects(g, {&tonemap}, scene, Format::kRGBA16Float, size, frame);
                    CHECK(order == std::vector<std::string>{"scene", "post.tonemap", "post.tonemap.copy"});
                    const auto result = readHdr(g.gpu, scene);
                    for (std::size_t i = 0; i < pixels.size(); ++i)
                    {
                        const glm::vec3 exposed = glm::vec3(roundHalf(pixels[i])) * 1.5f;
                        glm::vec3 ref = rd::tonemap(op, exposed, 4.0f);
                        if (srgb)
                        {
                            ref = rd::linearToSrgb(ref);
                        }
                        for (int k = 0; k < 3; ++k)
                        {
                            CHECK(result[i][k] == doctest::Approx(ref[k]).epsilon(2e-3).scale(1.0)); // précision de RGBA16F
                        }
                        CHECK(result[i].a == 1.0f);
                    }
                    g.gpu->destroy(scene);
                }
            }
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("auto exposure: GPU histogram equals the CPU histogram, exposure and adaptation follow the references")
    {
        // Trois zones : noir (case 0), lumière 4, ombre 0,05 ; puis une image plus sombre pour l'adaptation.
        const Extent2D size{40, 24}; // pas un multiple de 16 : tuiles partielles
        auto image = [&](float bright, float dark)
        {
            std::vector<glm::vec4> p;
            for (std::uint32_t y = 0; y < size.height; ++y)
            {
                for (std::uint32_t x = 0; x < size.width; ++x)
                {
                    const float v = x < 4 ? 0.0f : (x < 22 ? bright : dark);
                    p.push_back({v, v * 0.8f, v * 1.2f, 1.0f});
                }
            }
            return p;
        };
        auto cpuHistogram = [&](const std::vector<glm::vec4> &p, const rd::AutoExposureSettings &s)
        {
            std::vector<std::uint32_t> bins(rd::kHistogramBins, 0);
            for (const glm::vec4 &c : p)
            {
                ++bins[rd::histogramBin(rd::luminance(glm::vec3(roundHalf(c))), s.minLog2, s.maxLog2)];
            }
            return bins;
        };
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            if (!g.gpu->capabilities().compute)
            {
                MESSAGE("no compute: auto exposure skipped");
                continue;
            }
            auto created = rd::AutoExposureFeature::create(g.context);
            REQUIRE(created.has_value());
            rd::AutoExposureFeature &ae = **created;
            const rd::AutoExposureSettings s = ae.settings();

            const auto first = image(4.0f, 0.05f);
            const TextureHandle a = hdrTexture(g.gpu, size, first);
            rd::FrameData frame;
            frame.time.y = 0.0f; // première image : pas d'adaptation
            runEffects(g, {&ae}, a, Format::kRGBA16Float, size, frame);
            auto bins = ae.readHistogram();
            REQUIRE(bins.has_value());
            const auto expected = cpuHistogram(first, s);
            CHECK(*bins == expected);
            CHECK(expected[0] == 4u * size.height);
            auto state = ae.readExposure();
            REQUIRE(state.has_value());
            CHECK(state->valid);
            const float average = rd::histogramAverageLuminance(expected, s.minLog2, s.maxLog2);
            const float targetA = rd::targetExposure(average, s.key, s.minEv, s.maxEv);
            CHECK(state->averageLuminance == doctest::Approx(average).epsilon(1e-4));
            CHECK(state->target == doctest::Approx(targetA).epsilon(1e-4));
            CHECK(state->exposure == doctest::Approx(targetA).epsilon(1e-4)); // première image : immédiat
            // L'image est conservée (la passe ne fait que lire la scène).
            CHECK(readHdr(g.gpu, a)[30] == roundHalf(first[30]));

            // Image plus sombre, dt = 0,5 s : l'exposition se rapproche de la nouvelle visée d'un facteur 1 - exp(-dt × vitesse).
            const auto second = image(0.5f, 0.01f);
            const TextureHandle b = hdrTexture(g.gpu, size, second);
            frame.time.y = 0.5f;
            runEffects(g, {&ae}, b, Format::kRGBA16Float, size, frame);
            state = ae.readExposure();
            REQUIRE(state.has_value());
            const float targetB = rd::targetExposure(rd::histogramAverageLuminance(cpuHistogram(second, s), s.minLog2, s.maxLog2), s.key, s.minEv, s.maxEv);
            CHECK(targetB > targetA);
            CHECK(state->target == doctest::Approx(targetB).epsilon(1e-4));
            CHECK(state->exposure == doctest::Approx(targetA + (targetB - targetA) * rd::adaptationFactor(0.5f, s.adaptationSpeed)).epsilon(1e-4));

            // Remise à zéro : la visée est reprise telle quelle.
            REQUIRE(ae.resetAdaptation().has_value());
            runEffects(g, {&ae}, a, Format::kRGBA16Float, size, frame);
            state = ae.readExposure();
            REQUIRE(state.has_value());
            CHECK(state->exposure == doctest::Approx(targetA).epsilon(1e-4));

            // Le tonemapping lit l'exposition automatique quand elle est dans le graphe (déclarée en entrée, exécutée après).
            auto tm = rd::TonemapFeature::create(g.context);
            REQUIRE(tm.has_value());
            (*tm)->settings().tonemapper = rd::Tonemapper::kClamp;
            (*tm)->settings().encodeSrgb = false;
            const auto order = runEffects(g, {&ae, tm->get()}, b, Format::kRGBA16Float, size, frame);
            CHECK(position(order, "post.auto-exposure") < position(order, "post.tonemap"));
            state = ae.readExposure();
            REQUIRE(state.has_value());
            const float out = readHdr(g.gpu, b)[size.width * 2 + 30].r; // zone sombre (0,01)
            CHECK(out == doctest::Approx(std::min(1.0f, roundHalf(second[30]).r * state->exposure)).epsilon(2e-3));
            g.gpu->destroy(a);
            g.gpu->destroy(b);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("color grading: an identity LUT leaves the image unchanged, a channel-rotating LUT is sampled on the right axes")
    {
        // Image LDR (RGBA8) avec les 256 niveaux par canal : identité = octets identiques.
        const Extent2D size{256, 2};
        std::vector<std::uint8_t> bytes;
        for (std::uint32_t y = 0; y < size.height; ++y)
        {
            for (std::uint32_t x = 0; x < size.width; ++x)
            {
                bytes.insert(bytes.end(), {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(255 - x), static_cast<std::uint8_t>((x * 7 + y * 31) % 256), 255});
            }
        }
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            auto created = rd::ColorGradingFeature::create(g.context);
            REQUIRE(created.has_value());
            rd::ColorGradingFeature &grading = **created;
            CHECK(grading.lutSize() == 16);
            CHECK_FALSE(grading.needsHdr());
            const TextureHandle t = imageTexture(g.gpu, Format::kRGBA8Unorm, size, std::as_bytes(std::span(bytes)));
            runEffects(g, {&grading}, t, Format::kRGBA8Unorm, size);
            auto after = g.gpu->readTexture(t);
            REQUIRE(after.has_value());
            CHECK(std::memcmp(after->data(), bytes.data(), bytes.size()) == 0);

            // LUT (r, g, b) -> (g, b, 1 - r) : linéaire, donc exacte en trilinéaire ; une erreur d'axe se verrait.
            rd::ColorLut lut = rd::identityLut(8);
            for (glm::vec3 &c : lut.values)
            {
                c = {c.g, c.b, 1.0f - c.r};
            }
            REQUIRE(grading.setLut(lut).has_value());
            CHECK((tests::errorOf(grading.setLut(rd::ColorLut{})) == ErrorCode::kInvalidArgument));
            CHECK(grading.lutSize() == 8); // refus : l'ancienne LUT reste
            const TextureHandle u = imageTexture(g.gpu, Format::kRGBA8Unorm, size, std::as_bytes(std::span(bytes)));
            runEffects(g, {&grading}, u, Format::kRGBA8Unorm, size);
            auto rotated = g.gpu->readTexture(u);
            REQUIRE(rotated.has_value());
            int worst = 0;
            for (std::size_t i = 0; i < bytes.size(); i += 4)
            {
                const int expected[3] = {bytes[i + 1], bytes[i + 2], 255 - bytes[i]};
                for (int k = 0; k < 3; ++k)
                {
                    worst = std::max(worst, std::abs(static_cast<int>(static_cast<std::uint8_t>((*rotated)[i + k])) - expected[k]));
                }
            }
            CHECK(worst <= 1);

            // Force 0 : image inchangée quelle que soit la LUT.
            grading.setStrength(0.0f);
            const TextureHandle w = imageTexture(g.gpu, Format::kRGBA8Unorm, size, std::as_bytes(std::span(bytes)));
            runEffects(g, {&grading}, w, Format::kRGBA8Unorm, size);
            auto unchanged = g.gpu->readTexture(w);
            REQUIRE(unchanged.has_value());
            CHECK(std::memcmp(unchanged->data(), bytes.data(), bytes.size()) == 0);
            for (const TextureHandle h : {t, u, w})
            {
                g.gpu->destroy(h);
            }
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("vignette: multiplies the scene by the reference factor, alpha untouched")
    {
        const Extent2D size{32, 18};
        const std::vector<glm::vec4> pixels(size.width * size.height, glm::vec4(2.0f, 1.0f, 0.5f, 0.25f));
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            auto created = rd::VignetteFeature::create(g.context);
            REQUIRE(created.has_value());
            rd::VignetteFeature &vignette = **created;
            vignette.settings().intensity = 0.6f;
            const TextureHandle t = hdrTexture(g.gpu, size, pixels);
            runEffects(g, {&vignette}, t, Format::kRGBA16Float, size);
            const auto result = readHdr(g.gpu, t);
            const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
            for (std::uint32_t y = 0; y < size.height; ++y)
            {
                for (std::uint32_t x = 0; x < size.width; ++x)
                {
                    const glm::vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(size.width), (static_cast<float>(y) + 0.5f) / static_cast<float>(size.height));
                    const float f = rd::vignetteFactor(uv, aspect, vignette.settings());
                    const glm::vec4 &p = result[y * size.width + x];
                    CHECK(p.r == doctest::Approx(2.0f * f).epsilon(2e-3));
                    CHECK(p.b == doctest::Approx(0.5f * f).epsilon(2e-3));
                    CHECK(p.a == 0.25f);
                }
            }
            CHECK(result[0].r < result[size.width * 9 + 16].r); // coin plus sombre que le centre
            g.gpu->destroy(t);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("bloom: nothing below the threshold, a bright point spreads symmetrically around itself")
    {
        const Extent2D size{64, 64};
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            auto created = rd::BloomFeature::create(g.context);
            REQUIRE(created.has_value());
            rd::BloomFeature &bloom = **created;
            bloom.settings().intensity = 0.5f;

            // Sous le seuil (genou compris) : image identique.
            std::vector<glm::vec4> dim(size.width * size.height, glm::vec4(0.4f, 0.3f, 0.2f, 1.0f));
            const TextureHandle a = hdrTexture(g.gpu, size, dim);
            const auto order = runEffects(g, {&bloom}, a, Format::kRGBA16Float, size);
            CHECK(order[1] == "post.bloom.down0");
            CHECK(order.back() == "post.bloom.composite");
            CHECK(contains(order, "post.bloom.down4"));
            CHECK(contains(order, "post.bloom.up0"));
            for (const glm::vec4 &p : readHdr(g.gpu, a))
            {
                CHECK(p == roundHalf(glm::vec4(0.4f, 0.3f, 0.2f, 1.0f)));
            }

            // Un bloc 2×2 très lumineux au centre (pixels 31 et 32) : halo symétrique, décroissant, alpha inchangé.
            std::vector<glm::vec4> point(size.width * size.height, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            for (std::uint32_t y = 31; y <= 32; ++y)
            {
                for (std::uint32_t x = 31; x <= 32; ++x)
                {
                    point[y * size.width + x] = glm::vec4(50.0f, 50.0f, 50.0f, 1.0f);
                }
            }
            const TextureHandle b = hdrTexture(g.gpu, size, point);
            runEffects(g, {&bloom}, b, Format::kRGBA16Float, size);
            const auto r = readHdr(g.gpu, b);
            auto at = [&](std::uint32_t x, std::uint32_t y) { return r[y * size.width + x]; };
            CHECK(at(31, 31).r > 50.0f);   // le point reçoit son propre halo
            CHECK(at(36, 31).r > 0.01f);   // voisin éclairé
            CHECK(at(36, 31).r > at(44, 31).r); // décroissance
            CHECK(at(44, 31).r >= 0.0f);
            CHECK(at(27, 32).r == doctest::Approx(at(36, 31).r).epsilon(0.05)); // symétrie gauche / droite
            CHECK(at(31, 27).r == doctest::Approx(at(32, 36).r).epsilon(0.05)); // haut / bas
            CHECK(at(2, 2).a == 1.0f);
            g.gpu->destroy(a);
            g.gpu->destroy(b);
            g.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("SceneRenderer chain: HDR scene only when an HDR effect is active, effects ordered and fed by the scene pass")
    {
        FOR_EACH_BACKEND(backend)
        {
            PostGpu g(backend);
            REQUIRE(g.gpu.device);
            auto createdRenderer = rd::SceneRenderer::create(*g.gpu.device, *g.library, *g.pipelines, {.shaderDirectory = shaderDir()});
            REQUIRE(createdRenderer.has_value());
            rd::SceneRenderer &renderer = **createdRenderer;
            auto added = rd::addPostEffects(renderer, *g.gpu.device, *g.library, *g.pipelines, shaderDir());
            INFO((added ? std::string() : added.error().message));
            REQUIRE(added.has_value());
            rd::PostEffects fx = *added;
            REQUIRE(fx.tonemap != nullptr);
            CHECK(fx.autoExposure != nullptr); // OpenGL 4.3 et Vulkan ont le compute
            CHECK_FALSE(fx.hdrActive());
            renderer.setOutputResolution(16, 16);
            const TextureHandle out = g.gpu.target(Format::kRGBA8Unorm, {16, 16}, "output");
            const std::array<float, 4> clear{3.0f, 0.75f, 0.125f, 1.0f}; // couleur HDR (> 1) : effacement de la scène
            auto frame = [&]()
            {
                REQUIRE(fx.sync(renderer).has_value());
                g.gpu.run(
                    [&](CommandEncoder &e)
                    {
                        REQUIRE(renderer.beginFrame(e, {.clearColor = clear}).has_value());
                        rd::Camera camera;
                        REQUIRE(renderer.setCamera(camera).has_value());
                        auto ended = renderer.endFrame({out, Format::kRGBA8Unorm, {16, 16}});
                        INFO((ended ? std::string() : ended.error().message));
                        REQUIRE(ended.has_value());
                    });
            };

            // Aucun effet : scène RGBA8, aucune passe post.*, l'effacement est borné à 1 comme avant.
            frame();
            CHECK((renderer.sceneTarget().info().colorFormats[0] == Format::kRGBA8Unorm));
            for (const std::string &pass : renderer.renderGraph().executionOrder())
            {
                CHECK_FALSE(pass.starts_with("post."));
            }
            CHECK(rgba(g.gpu.pixel(out, 8, 8)) == std::array<std::uint8_t, 4>{255, 191, 32, 255});

            // Tonemapping seul (HDR) : scène RGBA16F, sortie = sRGB(ACES(couleur)).
            fx.tonemap->setEnabled(true);
            CHECK(fx.hdrActive());
            frame();
            CHECK((renderer.sceneTarget().info().colorFormats[0] == Format::kRGBA16Float));
            const glm::vec3 expected = rd::linearToSrgb(rd::tonemap(rd::Tonemapper::kAces, {3.0f, 0.75f, 0.125f}));
            const auto px = rgba(g.gpu.pixel(out, 8, 8));
            for (int k = 0; k < 3; ++k)
            {
                CHECK(near(px[k], static_cast<int>(std::lround(expected[k] * 255.0f))));
            }

            // Chaîne complète : ordre scène -> exposition -> bloom -> tonemapping -> LUT -> vignettage -> mise à l'échelle.
            for (rd::PostEffect *e : std::initializer_list<rd::PostEffect *>{fx.autoExposure, fx.bloom, fx.colorGrading, fx.vignette})
            {
                e->setEnabled(true);
            }
            frame();
            const auto order = renderer.renderGraph().executionOrder();
            const char *chain[] = {"scene", "post.auto-exposure", "post.bloom.down0", "post.bloom.composite", "post.tonemap", "post.tonemap.copy",
                                   "post.color-grading", "post.color-grading.copy", "post.vignette", "upscale"};
            for (std::size_t i = 0; i + 1 < std::size(chain); ++i)
            {
                const std::string step = std::string(chain[i]) + " < " + chain[i + 1];
                INFO(step);
                CHECK(position(order, chain[i]) < position(order, chain[i + 1]));
            }
            CHECK(renderer.renderGraph().culledPasses().empty());

            // Effets non HDR seuls (LUT, vignettage) : la scène repasse en RGBA8.
            fx.autoExposure->setEnabled(false);
            fx.bloom->setEnabled(false);
            fx.tonemap->setEnabled(false);
            CHECK_FALSE(fx.hdrActive());
            frame();
            CHECK((renderer.sceneTarget().info().colorFormats[0] == Format::kRGBA8Unorm));
            CHECK(contains(renderer.renderGraph().executionOrder(), "post.vignette"));
            fx.disableAll();
            frame();
            CHECK_FALSE(contains(renderer.renderGraph().executionOrder(), "post.vignette"));
            CHECK((tests::errorOf(renderer.setSceneColorFormat(Format::kR32Float)) == ErrorCode::kInvalidArgument));
            g.gpu->destroy(out);
            g.gpu.expectNoValidationErrors();
        }
    }
}
