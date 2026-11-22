// REN-32 (tri des transparents) et REN-37 (rendu de chaque BlendMode) sur le GPU, sur chaque backend (FOR_EACH_BACKEND), couches de
// validation à 0. Les images de référence sont calculées sur le CPU (formule de mélange de docs/03 §11 bis) et comparées pixel par
// pixel ; la caméra est orthographique (une unité = un pixel), éclairage ambiant 1 : couleur affichée = couleur du sommet.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/transparent_feature.hpp>

#include <algorithm>
#include <array>
#include <filesystem>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr std::uint32_t kSize = 8;
using Color = std::array<float, 3>;

struct TransparentScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    rd::TransparentFeature *feature = nullptr;
    rd::LightManager lights;
    rd::Camera camera;
    std::shared_ptr<rd::ShaderProgram> meshProgram;

    explicit TransparentScene(Backend backend) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        auto f = rd::TransparentFeature::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((f ? std::string() : f.error().message));
        REQUIRE(f.has_value());
        auto added = renderer->addFeature(std::move(*f));
        REQUIRE(added.has_value());
        feature = static_cast<rd::TransparentFeature *>(*added);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::move(*l);
        lights.setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights.upload().has_value());
        renderer->setLights(&lights);
        renderer->setOutputResolution(kSize, kSize);
        camera.setAspect(kSize, kSize);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(100.0f);
        camera.setOrthographic(static_cast<float>(kSize));
        auto program = library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        meshProgram = *program;
    }
    ~TransparentScene()
    {
        renderer.reset();
    }

    // Carré de côté `side` (plan z = 0, normale +Z), couleur des sommets donnée ; placé à (x, 0, z).
    rd::Mesh quad(float side, const Color &c, float x, float z, const char *name)
    {
        const float h = side * 0.5f;
        rd::MeshData d;
        d.vertices = {-h, -h, 0, 0, 0, 1, c[0], c[1], c[2], //
                      h,  -h, 0, 0, 0, 1, c[0], c[1], c[2], //
                      h,  h,  0, 0, 0, 1, c[0], c[1], c[2], //
                      -h, h,  0, 0, 0, 1, c[0], c[1], c[2]};
        d.indices = {0, 1, 2, 0, 2, 3};
        d.attributeSizes = {3, 3, 3};
        d.name = name;
        auto mesh = rd::Mesh::create(*gpu.device, d);
        REQUIRE(mesh.has_value());
        mesh->setProgram(meshProgram);
        mesh->setPosition({x, 0.0f, z});
        return std::move(*mesh);
    }

    // Image : `opaque` dessinés par drawMesh (passe « scene »), `items` déposés dans le plugin dans cet ordre.
    struct Item
    {
        rd::Mesh *mesh;
        rd::TransparentMaterial material;
    };
    TextureHandle render(const std::array<float, 3> &clear, const std::vector<Item> &items, const std::vector<rd::Mesh *> &opaque = {})
    {
        const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output");
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {clear[0], clear[1], clear[2], 1.0f}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                for (rd::Mesh *mesh : opaque)
                {
                    REQUIRE(renderer->drawMesh(*mesh, camera).has_value());
                }
                for (const Item &item : items)
                {
                    auto submitted = feature->submit(*renderer, *item.mesh, item.material);
                    INFO((submitted ? std::string() : submitted.error().message));
                    REQUIRE(submitted.has_value());
                }
                const auto ended = renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                INFO((ended ? std::string() : ended.error().message));
                REQUIRE(ended.has_value());
            });
        return out;
    }
    std::array<int, 3> at(TextureHandle t, int x, int y)
    {
        const auto p = rgba(gpu.pixel(t, x, y));
        return {p[0], p[1], p[2]};
    }
};

using Rgb = std::array<int, 3>;
bool close(const Rgb &a, const Rgb &b, int tolerance = 2)
{
    return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
}
Rgb byte(float r, float g, float b) { return {static_cast<int>(r * 255.0f + 0.5f), static_cast<int>(g * 255.0f + 0.5f), static_cast<int>(b * 255.0f + 0.5f)}; }

// Référence CPU du mélange d'un mode : source (r, g, b) d'opacité `a` sur le fond `dst`.
std::array<float, 3> blendReference(rd::BlendMode mode, const Color &src, float a, const Color &dst)
{
    std::array<float, 3> out{};
    for (int i = 0; i < 3; ++i)
    {
        switch (mode)
        {
        case rd::BlendMode::kBlend:
        case rd::BlendMode::kTransmission: out[i] = src[i] * a + dst[i] * (1.0f - a); break;
        case rd::BlendMode::kAdditive: out[i] = std::min(1.0f, src[i] * a + dst[i]); break;
        case rd::BlendMode::kPremultiplied: out[i] = src[i] * a + dst[i] * (1.0f - a); break; // le shader prémultiplie : src * a + dst * (1 - a)
        default: out[i] = src[i]; break;
        }
    }
    return out;
}

} // namespace

TEST_SUITE("renderer/transparency")
{
    TEST_CASE("sortTransparents: groups, priority, then back to front (REN-32)")
    {
        using E = rd::TransparentFeature::Entry;
        const auto entry = [](rd::BlendMode mode, float z, int priority, std::uint32_t seq)
        {
            E e;
            e.material.mode = mode;
            e.material.priority = priority;
            e.center = {0, 0, z};
            e.sequence = seq;
            return e;
        };
        const std::vector<E> entries = {
            entry(rd::BlendMode::kAdditive, -2, 0, 0), // 0
            entry(rd::BlendMode::kBlend, -3, 0, 1),    // 1 : proche de la caméra à l'origine
            entry(rd::BlendMode::kBlend, -9, 0, 2),    // 2 : loin
            entry(rd::BlendMode::kOpaque, -5, 0, 3),   // 3
            entry(rd::BlendMode::kMask, -1, 0, 4),     // 4 : opaque aussi, ordre de dépôt
            entry(rd::BlendMode::kBlend, -6, 0, 5),    // 5 : milieu
            entry(rd::BlendMode::kBlend, -1, -1, 6),   // 6 : priorité basse : dessiné d'abord, même s'il est devant
        };
        const auto order = rd::sortTransparents(entries, {0, 0, 0});
        CHECK(order == std::vector<std::uint32_t>{3, 4, 6, 2, 5, 1, 0});
        // Égalité de distance : ordre de dépôt.
        const std::vector<E> tie = {entry(rd::BlendMode::kBlend, -4, 0, 0), entry(rd::BlendMode::kBlend, -4, 0, 1)};
        CHECK(rd::sortTransparents(tie, {0, 0, 0}) == std::vector<std::uint32_t>{0, 1});
    }

    TEST_CASE("each BlendMode renders its reference image; the pass runs after the scene (REN-37, REN-32)")
    {
        FOR_EACH_BACKEND(backend)
        {
            TransparentScene scene(backend);
            REQUIRE(scene.gpu.device);
            const Color background{0.0f, 0.0f, 1.0f};
            const Color red{1.0f, 0.0f, 0.0f};
            // Carré rouge sur la moitié gauche de l'image (colonnes 0 à 3) ; le fond (bleu) reste à droite.
            auto left = scene.quad(4.0f, red, -2.0f, -5.0f, "left");
            left.setScale({1.0f, 2.0f, 1.0f}); // 4 x 8 : toute la moitié gauche
            for (const rd::BlendMode mode : rd::kAllBlendModes)
            {
                INFO("backend ", tests::backendName(backend), ", mode ", std::string(rd::blendModeName(mode)));
                rd::TransparentMaterial material{.mode = mode, .alpha = 0.5f, .cutoff = 0.5f};
                const TextureHandle out = scene.render(background, {{&left, material}});
                if (mode == rd::BlendMode::kDithered)
                {
                    // Tramage de Bayer 4x4 : la moitié des pixels de la zone couverte (4 x 8) est gardée, exactement.
                    int kept = 0;
                    for (int y = 0; y < static_cast<int>(kSize); ++y)
                    {
                        for (int x = 0; x < static_cast<int>(kSize); ++x)
                        {
                            const Rgb p = scene.at(out, x, y);
                            if (x >= 4)
                            {
                                CHECK(close(p, byte(0, 0, 1)));
                            }
                            else
                            {
                                CHECK((close(p, byte(1, 0, 0)) || close(p, byte(0, 0, 1))));
                                kept += close(p, byte(1, 0, 0)) ? 1 : 0;
                            }
                        }
                    }
                    CHECK(kept == 16);
                    continue;
                }
                const auto expected = blendReference(mode, red, 0.5f, background);
                for (int y = 0; y < static_cast<int>(kSize); ++y)
                {
                    for (int x = 0; x < static_cast<int>(kSize); ++x)
                    {
                        const Rgb want = x < 4 ? byte(expected[0], expected[1], expected[2]) : byte(0, 0, 1);
                        const Rgb got = scene.at(out, x, y);
                        INFO("pixel ", x, ",", y, " = ", got[0], " ", got[1], " ", got[2], ", expected ", want[0], " ", want[1], " ", want[2]);
                        CHECK(close(got, want));
                    }
                }
                CHECK(scene.feature->stats().submitted == 1);
            }
            // kMask : sous le seuil, le pixel est rejeté partout ; kMaskA2C se comporte comme kMask (limite du RHI).
            for (const rd::BlendMode mode : {rd::BlendMode::kMask, rd::BlendMode::kMaskA2C})
            {
                rd::TransparentMaterial material{.mode = mode, .alpha = 0.3f, .cutoff = 0.5f};
                const TextureHandle out = scene.render(background, {{&left, material}});
                CHECK(close(scene.at(out, 1, 1), byte(0, 0, 1)));
                material.alpha = 0.8f;
                const TextureHandle kept = scene.render(background, {{&left, material}});
                CHECK(close(scene.at(kept, 1, 1), byte(1, 0, 0))); // gardé, opaque
            }
            // Le plugin s'exécute après la passe de scène.
            const auto order = scene.renderer->renderGraph().executionOrder();
            const auto sceneAt = std::ranges::find(order, "scene");
            const auto transparentAt = std::ranges::find(order, rd::TransparentFeature::kPassName);
            REQUIRE(sceneAt != order.end());
            REQUIRE(transparentAt != order.end());
            CHECK(sceneAt < transparentAt);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("blended objects are sorted back to front without writing depth; priority overrides the distance (REN-32)")
    {
        FOR_EACH_BACKEND(backend)
        {
            TransparentScene scene(backend);
            REQUIRE(scene.gpu.device);
            const Color black{0, 0, 0};
            auto far = scene.quad(8.0f, {1, 0, 0}, 0.0f, -6.0f, "far");     // rouge, le plus loin
            auto mid = scene.quad(8.0f, {0, 1, 0}, 0.0f, -4.5f, "mid");     // vert
            auto near = scene.quad(8.0f, {0, 0, 1}, 0.0f, -3.0f, "near");    // bleu, le plus proche
            const rd::TransparentMaterial half{.mode = rd::BlendMode::kBlend, .alpha = 0.5f};

            // Déposés dans le désordre (proche, loin, milieu) : dessinés loin, milieu, proche.
            const TextureHandle sorted = scene.render(black, {{&near, half}, {&far, half}, {&mid, half}});
            CHECK(scene.feature->drawOrder() == std::vector<std::uint32_t>{1, 2, 0});
            CHECK(close(scene.at(sorted, 4, 4), byte(0.125f, 0.25f, 0.5f)));
            CHECK(scene.feature->stats().blended == 3);
            CHECK(scene.feature->stats().drawCalls == 3);

            // Priorité : le bleu, bien que le plus proche, est dessiné en premier ; le rouge et le vert, plus loin, restent visibles
            // à travers lui (aucune écriture de profondeur).
            rd::TransparentMaterial first = half;
            first.priority = -1;
            const TextureHandle prioritized = scene.render(black, {{&near, first}, {&far, half}, {&mid, half}});
            CHECK(scene.feature->drawOrder() == std::vector<std::uint32_t>{0, 1, 2});
            CHECK(close(scene.at(prioritized, 4, 4), byte(0.25f, 0.5f, 0.125f)));

            // Les additifs passent après les mélangés, même plus loin que ceux-ci et déposés avant.
            auto glow = scene.quad(8.0f, {0, 1, 0}, 0.0f, -8.0f, "glow");
            const TextureHandle mixed = scene.render(black, {{&glow, {.mode = rd::BlendMode::kAdditive, .alpha = 0.5f}}, {&near, {.mode = rd::BlendMode::kBlend, .alpha = 0.5f}}});
            CHECK(scene.feature->drawOrder() == std::vector<std::uint32_t>{1, 0});
            CHECK(close(scene.at(mixed, 4, 4), byte(0.0f, 0.5f, 0.5f)));
            CHECK(scene.feature->stats().additive == 1);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("opaque geometry of the scene hides transparents behind it and is seen through those in front (REN-32)")
    {
        FOR_EACH_BACKEND(backend)
        {
            TransparentScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto wall = scene.quad(4.0f, {0, 1, 0}, -2.0f, -4.0f, "wall"); // opaque : colonnes 0 à 3
            wall.setScale({1.0f, 2.0f, 1.0f});
            auto behind = scene.quad(8.0f, {1, 0, 0}, 0.0f, -6.0f, "behind");
            auto front = scene.quad(8.0f, {0, 0, 1}, 0.0f, -2.0f, "front");
            const rd::TransparentMaterial half{.mode = rd::BlendMode::kBlend, .alpha = 0.5f};
            const TextureHandle out = scene.render({0, 0, 0}, {{&behind, half}}, {&wall});
            CHECK(close(scene.at(out, 1, 1), byte(0, 1, 0)));       // derrière le mur : invisible
            CHECK(close(scene.at(out, 6, 1), byte(0.5f, 0, 0)));    // à côté du mur : visible sur le fond
            const TextureHandle through = scene.render({0, 0, 0}, {{&front, half}}, {&wall});
            CHECK(close(scene.at(through, 1, 1), byte(0, 0.5f, 0.5f))); // devant le mur : le mur se voit à travers
            CHECK(close(scene.at(through, 6, 1), byte(0, 0, 0.5f)));
            scene.gpu.expectNoValidationErrors();
        }
    }
}
