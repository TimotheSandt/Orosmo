// REN-19 (anticrénelage). Suite CPU « aa/names » : noms des modes. Suite GPU « renderer/aa », sur chaque backend, validation à 0 :
//   - FXAA et SMAA lissent une arête en escalier (pixels intermédiaires comptés) sans toucher aux zones unies ;
//   - ordre dans le graphe : scène → TAA → post HDR → tonemapping → SMAA/FXAA → mise à l'échelle ;
//   - TAA : jitter seulement quand il est actif, une image fixe converge (écart entre images successives qui baisse) et son arête
//     est lissée, l'historique est rejeté sur un changement de scène brusque (profondeur), un objet en mouvement ne laisse pas de
//     traînée (borne ci-dessous).
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/antialiasing.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/layer2d_feature.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/post_effects.hpp>
#include <orosmo/renderer/render_graph.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/texture.hpp>
#include <orosmo/renderer/transparent_feature.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Traînée tolérée derrière un objet en mouvement (TAA) : pixels hors de l'objet (dilaté de 2 pixels) qui s'écartent du fond de plus
// de 0,1 (sur [0, 1]). Mesuré : 0 sur OpenGL et Vulkan ; la borne laisse une marge pour d'autres pilotes.
constexpr int kMaxGhostPixels = 4;

std::ptrdiff_t position(const std::vector<std::string> &names, const std::string &name)
{
    return std::distance(names.begin(), std::find(names.begin(), names.end(), name));
}

// Image RGBA8 en niveaux de gris (une valeur par pixel, 0 à 255).
std::vector<std::uint8_t> toRgba(const std::vector<std::uint8_t> &gray)
{
    std::vector<std::uint8_t> out;
    for (const std::uint8_t g : gray)
    {
        out.insert(out.end(), {g, g, g, 255});
    }
    return out;
}

std::vector<float> readGray(Gpu &gpu, TextureHandle texture)
{
    auto raw = gpu->readTexture(texture);
    REQUIRE(raw.has_value());
    std::vector<float> out(raw->size() / 4);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = static_cast<float>(std::to_integer<std::uint8_t>((*raw)[i * 4 + 1])) / 255.0f; // vert
    }
    return out;
}

int countBetween(const std::vector<float> &image, float lo, float hi)
{
    return static_cast<int>(std::count_if(image.begin(), image.end(), [&](float v) { return v > lo && v < hi; }));
}

// Exécute `effect` dans un graphe dont la « scène » est `texture` (comme SceneRenderer après sa passe de scène).
void runEffect(Gpu &gpu, rd::PostEffect &effect, TextureHandle texture, Extent2D size)
{
    rd::RenderGraph graph(*gpu.device);
    rd::RenderGraphBuilder::Resources res;
    res.sceneColor = graph.importTexture("scene.color", texture, {.format = Format::kRGBA8Unorm, .size = size});
    res.renderSize = size;
    res.outputSize = size;
    graph.addPass("scene", rd::PassType::kGeneric, [](rd::RenderPassContext &) -> core::Result<> { return {}; }).write(res.sceneColor);
    rd::RenderGraphBuilder builder(graph, res, gpu->capabilities(), "scene");
    REQUIRE(effect.prepare({}).has_value());
    auto declared = effect.declarePasses(builder);
    INFO((declared ? std::string() : declared.error().message));
    REQUIRE(declared.has_value());
    gpu.run(
        [&](CommandEncoder &e)
        {
            auto r = graph.execute(e);
            INFO((r ? std::string() : r.error().message));
            REQUIRE(r.has_value());
        });
}

// Quadrilatère de couleur unie dans le plan z, centré en (cx, cy), demi-côté `half`, normale +Z.
rd::MeshData quad(float cx, float cy, float z, float half, const glm::vec3 &color)
{
    rd::MeshData d;
    const float xs[4] = {cx - half, cx + half, cx + half, cx - half};
    const float ys[4] = {cy - half, cy - half, cy + half, cy + half};
    for (int i = 0; i < 4; ++i)
    {
        d.vertices.insert(d.vertices.end(), {xs[i], ys[i], z, 0.0f, 0.0f, 1.0f, color.r, color.g, color.b});
    }
    d.indices = {0, 1, 2, 0, 2, 3};
    d.attributeSizes = {3, 3, 3};
    d.name = "aa.quad";
    return d;
}

// Triangle incliné (arêtes non alignées sur la grille).
rd::MeshData slantedTriangle()
{
    rd::MeshData d;
    d.vertices = {-0.9f, -0.6f, -1.0f, 0, 0, 1, 1, 1, 1, 0.8f, -0.3f, -1.0f, 0, 0, 1, 1, 1, 1, -0.2f, 0.7f, -1.0f, 0, 0, 1, 1, 1, 1};
    d.indices = {0, 1, 2};
    d.attributeSizes = {3, 3, 3};
    d.name = "aa.triangle";
    return d;
}

// SceneRenderer + TAA (enregistré avant les effets) + effets + FXAA/SMAA, lumière ambiante 1 (couleur = couleur des sommets).
struct AaScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::shared_ptr<rd::PostContext> context;
    std::unique_ptr<rd::LightManager> lights;
    rd::AntiAliasing aa;
    rd::PostEffects fx;
    std::shared_ptr<rd::ShaderProgram> program;
    Extent2D size;
    TextureHandle out;
    // Scène mélangée (comme `--scene mixed` de la démo) : transparents et 2D monde AVANT le TAA, 2D en superposition après tout.
    rd::TransparentFeature *transparents = nullptr;
    rd::Layer2DFeature *world = nullptr;
    rd::Layer2DFeature *overlay = nullptr;

    rd::Layer2DFeature *addLayer(const char *name, rd::Layer2DMode mode)
    {
        auto feature = rd::Layer2DFeature::create(name, mode, *gpu.device, *library, *pipelines, shaderDir());
        INFO((feature ? std::string() : feature.error().message));
        REQUIRE(feature.has_value());
        auto added = renderer->addFeature(std::move(*feature));
        REQUIRE(added.has_value());
        return static_cast<rd::Layer2DFeature *>(*added);
    }

    AaScene(Backend backend, Extent2D extent, bool mixed = false) : gpu(backend, extent), size(extent)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        renderer->setOutputResolution(static_cast<int>(size.width), static_cast<int>(size.height));
        auto ctx = rd::PostContext::create(*gpu.device, *library, *pipelines, shaderDir());
        REQUIRE(ctx.has_value());
        context = *ctx;
        if (mixed)
        {
            auto t = rd::TransparentFeature::create(*gpu.device, *library, *pipelines, shaderDir());
            REQUIRE(t.has_value());
            auto added = renderer->addFeature(std::move(*t));
            REQUIRE(added.has_value());
            transparents = static_cast<rd::TransparentFeature *>(*added);
            world = addLayer("aa.mix.world", rd::Layer2DMode::kWorld);
        }
        auto taa = rd::addTemporalAntiAliasing(*renderer, context);
        INFO((taa ? std::string() : taa.error().message));
        REQUIRE(taa.has_value());
        auto post = rd::addPostEffects(*renderer, *gpu.device, *library, *pipelines, shaderDir());
        REQUIRE(post.has_value());
        fx = *post;
        auto edge = rd::addEdgeAntiAliasing(*renderer, context);
        REQUIRE(edge.has_value());
        aa = *edge;
        aa.taa = *taa;
        if (mixed)
        {
            overlay = addLayer("aa.mix.overlay", rd::Layer2DMode::kOverlay);
        }
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::make_unique<rd::LightManager>(std::move(*l));
        lights->setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights->upload().has_value());
        renderer->setLights(lights.get());
        // Programme compilé pour les AOV du TAA (mouvement, profondeur linéaire), exigés dès son enregistrement.
        auto p = library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag", renderer->aovShaderDefines());
        INFO((p ? std::string() : p.error().message));
        REQUIRE(p.has_value());
        program = *p;
        out = gpu.target(Format::kRGBA8Unorm, size, "aa.output");
    }
    ~AaScene()
    {
        if (gpu.device)
        {
            gpu->destroy(out);
        }
        renderer.reset();
        context.reset();
    }

    rd::Mesh mesh(const rd::MeshData &data)
    {
        auto m = rd::Mesh::create(*gpu.device, data);
        REQUIRE(m.has_value());
        m->setProgram(program);
        return std::move(*m);
    }

    void frame(std::initializer_list<rd::Mesh *> meshes)
    {
        REQUIRE(fx.sync(*renderer).has_value());
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                rd::Camera camera;
                camera.setAspect(size.width, size.height);
                REQUIRE(renderer->setCamera(camera).has_value());
                for (rd::Mesh *m : meshes)
                {
                    auto drawn = renderer->drawMesh(*m, camera);
                    INFO((drawn ? std::string() : drawn.error().message));
                    REQUIRE(drawn.has_value());
                }
                auto ended = renderer->endFrame({out, Format::kRGBA8Unorm, size});
                INFO((ended ? std::string() : ended.error().message));
                REQUIRE(ended.has_value());
            });
    }

    std::vector<float> image() { return readGray(gpu, out); }
};

float meanAbsDifference(const std::vector<float> &a, const std::vector<float> &b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        sum += std::abs(a[i] - b[i]);
    }
    return static_cast<float>(sum / static_cast<double>(a.size()));
}

} // namespace

TEST_SUITE("aa/names")
{
    TEST_CASE("REN-19: anti-aliasing mode names")
    {
        for (const rd::AntiAliasingMode mode : {rd::AntiAliasingMode::kNone, rd::AntiAliasingMode::kFxaa, rd::AntiAliasingMode::kSmaa, rd::AntiAliasingMode::kTaa})
        {
            CHECK((rd::parseAntiAliasingMode(rd::toString(mode)) == mode));
        }
        CHECK((rd::parseAntiAliasingMode("SMAA") == rd::AntiAliasingMode::kSmaa));
        CHECK_FALSE(rd::parseAntiAliasingMode("msaa").has_value());
        rd::AntiAliasing none;
        CHECK((none.mode() == rd::AntiAliasingMode::kNone));
        CHECK(none.select(rd::AntiAliasingMode::kNone).has_value());
        CHECK_FALSE(none.select(rd::AntiAliasingMode::kTaa).has_value()); // non enregistré
    }
}

TEST_SUITE("renderer/aa")
{
    TEST_CASE("REN-19: FXAA and SMAA smooth a staircase edge (horizontal and vertical) and keep flat areas")
    {
        FOR_EACH_BACKEND(backend)
        {
            constexpr std::uint32_t kSize = 64;
            Gpu gpu(backend, {kSize, kSize});
            REQUIRE(gpu.device);
            rd::ShaderLibrary library(*gpu.device);
            rd::PipelineCache pipelines(*gpu.device);
            auto context = rd::PostContext::create(*gpu.device, library, pipelines, shaderDir());
            REQUIRE(context.has_value());
            auto fxaa = rd::FxaaFeature::create(*context);
            REQUIRE(fxaa.has_value());
            auto smaa = rd::SmaaFeature::create(*context);
            REQUIRE(smaa.has_value());
            for (const bool transposed : {false, true})
            {
                // Demi-plan blanc sous une droite de pente 1/5 (paliers de 5 pixels) ; transposé : contour presque vertical.
                std::vector<std::uint8_t> gray(kSize * kSize);
                const int edgePixels = static_cast<int>(kSize); // une transition par colonne (ou par ligne)
                for (std::uint32_t y = 0; y < kSize; ++y)
                {
                    for (std::uint32_t x = 0; x < kSize; ++x)
                    {
                        const float u = static_cast<float>(transposed ? y : x);
                        const float v = static_cast<float>(transposed ? x : y);
                        gray[y * kSize + x] = v >= 20.0f + std::floor(u / 5.0f) ? 255 : 0;
                    }
                }
                const auto pixels = toRgba(gray);
                for (rd::PostEffect *effect : {static_cast<rd::PostEffect *>(fxaa->get()), static_cast<rd::PostEffect *>(smaa->get())})
                {
                    INFO(effect->name(), (transposed ? " (vertical edge)" : " (horizontal edge)"));
                    TextureDesc desc;
                    desc.format = Format::kRGBA8Unorm;
                    desc.size = {kSize, kSize, 1};
                    desc.usage = TextureUsage::kSampled | TextureUsage::kRenderTarget | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
                    auto texture = gpu->createTexture(desc);
                    REQUIRE(texture.has_value());
                    REQUIRE(gpu->updateTexture(*texture, {}, std::as_bytes(std::span(pixels))).has_value());
                    CHECK(countBetween(readGray(gpu, *texture), 0.02f, 0.98f) == 0); // aucun pixel intermédiaire avant
                    runEffect(gpu, *effect, *texture, {kSize, kSize});
                    const auto result = readGray(gpu, *texture);
                    const int intermediate = countBetween(result, 0.02f, 0.98f);
                    MESSAGE(effect->name(), (transposed ? " vertical" : " horizontal"), ": ", intermediate, " intermediate pixels for ", edgePixels,
                            " edge transitions");
                    CHECK(intermediate >= edgePixels); // au moins un pixel intermédiaire par transition, en moyenne
                    // Zones unies loin du contour : inchangées.
                    auto at = [&](std::uint32_t u, std::uint32_t v) { return transposed ? result[u * kSize + v] : result[v * kSize + u]; };
                    CHECK(at(2, 2) == 0.0f);
                    CHECK(at(60, 2) == 0.0f);
                    CHECK(at(2, 60) == 1.0f);
                    CHECK(at(30, 60) == 1.0f);
                    gpu->destroy(*texture);
                }
            }
            fxaa->reset();
            smaa->reset();
            context->reset();
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-19: graph order scene -> TAA -> HDR post -> tonemapping -> SMAA/FXAA -> upscale; disabled AA adds no pass")
    {
        FOR_EACH_BACKEND(backend)
        {
            AaScene s(backend, {16, 16});
            REQUIRE(s.gpu.device);
            s.frame({});
            for (const std::string &pass : s.renderer->renderGraph().executionOrder())
            {
                CHECK_FALSE(pass.starts_with("aa."));
            }
            CHECK(s.renderer->cameraJitter() == glm::vec2(0.0f)); // TAA désactivé : aucun jitter
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kTaa).has_value());
            s.aa.smaa->setEnabled(true); // les deux à la fois, pour l'ordre seulement
            s.aa.fxaa->setEnabled(true);
            s.fx.bloom->setEnabled(true);
            s.fx.tonemap->setEnabled(true);
            s.frame({});
            s.frame({});
            CHECK(s.renderer->cameraJitter() != glm::vec2(0.0f)); // TAA actif : jitter de Halton
            const auto order = s.renderer->renderGraph().executionOrder();
            const char *chain[] = {"scene",        "aa.taa.resolve", "aa.taa.copy",    "post.bloom.down0", "post.bloom.composite", "post.tonemap",
                                   "post.tonemap.copy", "aa.fxaa",   "aa.fxaa.copy",   "aa.smaa.edges",    "aa.smaa.weights",      "aa.smaa.blend",
                                   "aa.smaa.copy", "upscale"};
            for (const char *name : chain)
            {
                INFO(name);
                REQUIRE(std::find(order.begin(), order.end(), name) != order.end()); // position() d'une passe absente ne ferait pas échouer
            }
            for (std::size_t i = 0; i + 1 < std::size(chain); ++i)
            {
                const std::string step = std::string(chain[i]) + " < " + chain[i + 1];
                INFO(step);
                CHECK(position(order, chain[i]) < position(order, chain[i + 1]));
            }
            CHECK(s.renderer->renderGraph().culledPasses().empty());
            CHECK(s.aa.taa->historyValid());
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kNone).has_value());
            CHECK((s.aa.mode() == rd::AntiAliasingMode::kNone));
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-19: TAA converges on a still image and smooths its edges")
    {
        FOR_EACH_BACKEND(backend)
        {
            AaScene s(backend, {48, 48});
            REQUIRE(s.gpu.device);
            rd::Mesh triangle = s.mesh(slantedTriangle());
            s.frame({&triangle});
            const auto aliased = s.image();
            CHECK(countBetween(aliased, 0.02f, 0.98f) == 0); // sans anticrénelage : tout ou rien
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kTaa).has_value());
            std::vector<std::vector<float>> frames;
            for (int i = 0; i < 32; ++i)
            {
                s.frame({&triangle});
                frames.push_back(s.image());
            }
            auto change = [&](int from, int to)
            {
                float sum = 0.0f;
                for (int i = from; i < to; ++i)
                {
                    sum += meanAbsDifference(frames[static_cast<std::size_t>(i)], frames[static_cast<std::size_t>(i + 1)]);
                }
                return sum / static_cast<float>(to - from);
            };
            const float early = change(1, 4);
            const float late = change(26, 31);
            MESSAGE("TAA mean change between frames: early ", early, ", late ", late);
            CHECK(early > 0.0f);
            CHECK(late < early * 0.5f); // l'écart entre images successives baisse : l'accumulation converge
            const int intermediate = countBetween(frames.back(), 0.02f, 0.98f);
            MESSAGE("TAA intermediate pixels: ", intermediate);
            CHECK(intermediate >= 30); // contours du triangle (≈ 100 pixels de long) lissés
            // L'intérieur et le fond restent unis.
            const auto &last = frames.back();
            CHECK(last[2 * 48 + 2] == 0.0f);
            CHECK(last[24 * 48 + 22] == 1.0f);
            const auto info = s.aa.taa->readHistoryInfo();
            REQUIRE(info.has_value());
            CHECK((*info)[24 * 48 + 22][3] == doctest::Approx(12.0f)); // maxAccumulatedFrames atteint
            CHECK((*info)[24 * 48 + 22][0] == doctest::Approx(1.0f).epsilon(0.01)); // profondeur linéaire du triangle
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-19: TAA rejects the history on an abrupt scene change (depth), keeps it otherwise unless reset")
    {
        FOR_EACH_BACKEND(backend)
        {
            AaScene s(backend, {16, 16});
            REQUIRE(s.gpu.device);
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kTaa).has_value());
            s.aa.taa->settings().clampHistory = false; // sans écrêtage : seul le rejet peut effacer l'historique
            rd::Mesh red = s.mesh(quad(0, 0, -1.0f, 10.0f, {1, 0, 0}));
            rd::Mesh greenNear = s.mesh(quad(0, 0, -1.0f, 10.0f, {0, 1, 0}));
            rd::Mesh greenFar = s.mesh(quad(0, 0, -4.0f, 40.0f, {0, 1, 0}));
            auto center = [&]() { return rgba(s.gpu.pixel(s.out, 8, 8)); };
            auto accumulateRed = [&]()
            {
                s.aa.taa->resetHistory(); // départ propre : historique entièrement rouge
                for (int i = 0; i < 16; ++i)
                {
                    s.frame({&red});
                }
                CHECK(center()[0] == 255);
            };

            // Même profondeur, autre couleur, sans écrêtage : l'historique est gardé (mélange).
            accumulateRed();
            s.frame({&greenNear});
            const auto kept = center();
            MESSAGE("same depth: ", int(kept[0]), " ", int(kept[1]));
            CHECK(kept[0] > 128);

            // Coupure : la profondeur change (1 -> 4) : historique rejeté, image courante seule.
            accumulateRed();
            s.frame({&greenFar});
            const auto cut = center();
            CHECK(cut[0] <= 2);
            CHECK(cut[1] >= 253);
            const auto info = s.aa.taa->readHistoryInfo();
            REQUIRE(info.has_value());
            CHECK((*info)[8 * 16 + 8][3] == doctest::Approx(1.0f)); // accumulation repartie de 1
            CHECK((*info)[8 * 16 + 8][0] == doctest::Approx(4.0f).epsilon(0.01));

            // Coupure annoncée (resetHistory) à profondeur égale : historique ignoré.
            accumulateRed();
            s.aa.taa->resetHistory();
            s.frame({&greenNear});
            CHECK(center()[0] <= 2);
            // Avec l'écrêtage (défaut), la couleur seule suffit aussi à effacer l'historique à profondeur égale.
            s.aa.taa->settings().clampHistory = true;
            accumulateRed();
            s.frame({&greenNear});
            CHECK(center()[0] <= 2);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-19: TAA leaves no excessive ghosting behind a moving object")
    {
        FOR_EACH_BACKEND(backend)
        {
            constexpr std::uint32_t kSize = 64;
            AaScene s(backend, {kSize, kSize});
            REQUIRE(s.gpu.device);
            rd::Mesh background = s.mesh(quad(0, 0, -5.0f, 50.0f, {0.5f, 0.5f, 0.5f}));
            rd::Mesh object = s.mesh(quad(0, 0, -2.0f, 0.25f, {1, 1, 1}));
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kTaa).has_value());
            constexpr int kFrames = 16;
            constexpr float kStep = 0.08f; // ≈ 1,8 pixel par image à 2 unités de la caméra (champ de 70°, 64 pixels)
            auto place = [&](int i) { object.setPosition({-0.6f + kStep * static_cast<float>(i), 0.0f, 0.0f}); };
            for (int i = 0; i < kFrames; ++i)
            {
                place(i);
                s.frame({&background, &object});
            }
            const auto taa = s.image();
            // Référence : la même image sans anticrénelage (sans jitter).
            REQUIRE(s.aa.select(rd::AntiAliasingMode::kNone).has_value());
            place(kFrames - 1);
            s.frame({&background, &object});
            const auto reference = s.image();
            auto covered = [&](int x, int y)
            {
                for (int dy = -2; dy <= 2; ++dy)
                {
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int u = std::clamp(x + dx, 0, int(kSize) - 1);
                        const int v = std::clamp(y + dy, 0, int(kSize) - 1);
                        if (reference[static_cast<std::size_t>(v) * kSize + static_cast<std::size_t>(u)] > 0.55f)
                        {
                            return true;
                        }
                    }
                }
                return false;
            };
            int objectPixels = 0;
            int ghosts = 0;
            float worst = 0.0f;
            for (int y = 0; y < int(kSize); ++y)
            {
                for (int x = 0; x < int(kSize); ++x)
                {
                    const std::size_t i = static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x);
                    objectPixels += reference[i] > 0.55f ? 1 : 0;
                    if (!covered(x, y))
                    {
                        const float delta = std::abs(taa[i] - reference[i]);
                        worst = std::max(worst, delta);
                        ghosts += delta > 0.1f ? 1 : 0;
                    }
                }
            }
            MESSAGE("TAA ghosting: ", ghosts, " pixels off by more than 0.1 (worst ", worst, "), object of ", objectPixels, " pixels");
            CHECK(objectPixels > 20);
            CHECK(ghosts <= kMaxGhostPixels);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("REN-19: each AA mode runs with transparents and 2D layers (world and overlay) of a mixed scene")
    {
        FOR_EACH_BACKEND(backend)
        {
            constexpr std::uint32_t kSize = 32;
            AaScene s(backend, {kSize, kSize}, true);
            REQUIRE(s.gpu.device);
            REQUIRE(s.transparents != nullptr);
            rd::Mesh wall = s.mesh(quad(0, 0, -4.0f, 1.5f, {0, 1, 0}));
            rd::Mesh glass = s.mesh(quad(0.5f, 0, -2.0f, 0.6f, {0, 0, 1}));
            const std::uint8_t texel[4] = {255, 255, 255, 255};
            rd::TextureInfo info;
            info.filter = rd::FilterMode::kNearest;
            auto white = rd::Texture::create(*s.gpu.device, info, std::as_bytes(std::span(texel)));
            REQUIRE(white.has_value());
            for (const rd::AntiAliasingMode mode : {rd::AntiAliasingMode::kTaa, rd::AntiAliasingMode::kFxaa, rd::AntiAliasingMode::kSmaa, rd::AntiAliasingMode::kNone})
            {
                INFO("mode ", rd::toString(mode));
                REQUIRE(s.aa.select(mode).has_value());
                for (int i = 0; i < 3; ++i)
                {
                    s.gpu.run(
                        [&](CommandEncoder &e)
                        {
                            REQUIRE(s.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                            rd::Camera camera;
                            camera.setAspect(kSize, kSize);
                            REQUIRE(s.renderer->setCamera(camera).has_value());
                            REQUIRE(s.renderer->drawMesh(wall, camera).has_value());
                            auto submitted = s.transparents->submit(*s.renderer, glass, {.mode = rd::BlendMode::kBlend, .alpha = 0.5f});
                            INFO((submitted ? std::string() : submitted.error().message));
                            REQUIRE(submitted.has_value());
                            s.world->begin({});
                            auto material = s.world->renderer().material(*white, true);
                            REQUIRE(material.has_value());
                            rd::Sprite sprite;
                            sprite.material = *material;
                            sprite.size = {0.5f, 0.5f};
                            sprite.color = {1, 0, 0, 1};
                            REQUIRE(s.world->renderer().draw(sprite).has_value());
                            rd::Camera2D screen;
                            screen.setViewport(kSize, kSize);
                            screen.setCenter({kSize * 0.5f, kSize * 0.5f});
                            s.overlay->begin(screen);
                            s.overlay->renderer().rect({0, 0}, {4, 4}, {1, 1, 1, 1});
                            auto ended = s.renderer->endFrame({s.out, Format::kRGBA8Unorm, s.size});
                            INFO((ended ? std::string() : ended.error().message));
                            REQUIRE(ended.has_value());
                        });
                }
                const auto order = s.renderer->renderGraph().executionOrder();
                CHECK(std::find(order.begin(), order.end(), "transparents") != order.end());
                CHECK(std::find(order.begin(), order.end(), "aa.mix.world") != order.end());
                REQUIRE(std::find(order.begin(), order.end(), "aa.mix.overlay") != order.end());
                if (mode != rd::AntiAliasingMode::kNone)
                {
                    const char *lastAaPass = mode == rd::AntiAliasingMode::kTaa ? "aa.taa.copy" : mode == rd::AntiAliasingMode::kFxaa ? "aa.fxaa.copy" : "aa.smaa.copy";
                    REQUIRE(std::find(order.begin(), order.end(), lastAaPass) != order.end());
                    CHECK(position(order, lastAaPass) < position(order, "aa.mix.overlay")); // la 2D en superposition vient après l'anticrénelage
                }
                if (mode == rd::AntiAliasingMode::kTaa)
                {
                    CHECK(position(order, "transparents") < position(order, "aa.taa.resolve"));
                    CHECK(position(order, "aa.mix.world") < position(order, "aa.taa.resolve"));
                    CHECK(s.aa.taa->historyValid());
                }
                CHECK(s.gpu->validationErrorCount() == 0);
            }
            s.gpu.expectNoValidationErrors();
        }
    }
}
