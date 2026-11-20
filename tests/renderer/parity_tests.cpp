// RHI-02 (lot B4) : parité avec le rendu hérité. La scène de terrain (graine 1) dessinée par la nouvelle couche doit donner l'image
// de référence produite par l'ancien rendu (docs/BASELINE/reference/). Fichier séparé : il inclut les en-têtes hérités (Grid, Noise),
// dont les noms globaux (`ShaderStage`, `BufferDesc`...) entrent en conflit avec `using namespace engine::rhi`.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"

#include <engine/renderer/canvas_renderer.hpp>
#include <engine/renderer/image.hpp>
#include <engine/renderer/light.hpp>
#include <engine/renderer/mesh.hpp>
#include <engine/renderer/scene_renderer.hpp>

#include "Noise.h"
#include "ProceduralGeneration/Grid.h"
#include "UI/Layout.h"
#include "UI/Manager.h"
#include "UI/Widgets.h"

#include <filesystem>

namespace fs = std::filesystem;
namespace rd = engine::renderer;
namespace rhi = engine::rhi;

namespace
{
struct CurrentDirectory
{
    fs::path previous;
    explicit CurrentDirectory(const fs::path &dir)
    {
        std::error_code ec;
        previous = fs::current_path(ec);
        fs::current_path(dir, ec);
    }
    ~CurrentDirectory()
    {
        std::error_code ec;
        fs::current_path(previous, ec);
    }
};

fs::path repoRoot() { return tests::repoRoot(__FILE__); }
fs::path shaderDir() { return repoRoot() / "shaders" / "rhi"; }
} // namespace


namespace
{

// Périphérique hors écran, bibliothèque de shaders, cache de pipelines, rendu de scène et texture de sortie 1280x720.
struct Setup
{
    std::unique_ptr<rhi::Device> device; // déclaré en premier : détruit en dernier
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    std::unique_ptr<rd::CanvasRenderer> canvas;
    rhi::TextureHandle out;

    ~Setup()
    {
        canvas.reset();
        renderer.reset();
    }

    bool create()
    {
        auto created = rhi::createDevice({.surface = {.size = {1280, 720}}});
        if (!created)
        {
            WARN_MESSAGE(false, "Skipped (no driver): OpenGL 4.3 context");
            return false;
        }
        device = std::move(*created);
        library = std::make_unique<rd::ShaderLibrary>(*device);
        pipelines = std::make_unique<rd::PipelineCache>(*device);
        auto rendererCreated = rd::SceneRenderer::create(*device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        REQUIRE(rendererCreated.has_value());
        renderer = std::move(*rendererCreated);
        auto canvasCreated = rd::CanvasRenderer::create(*device, *library, *pipelines, shaderDir());
        REQUIRE(canvasCreated.has_value());
        canvas = std::move(*canvasCreated);
        rhi::TextureDesc od;
        od.size = {1280, 720, 1};
        od.usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopySrc;
        od.debugName = "output";
        auto outCreated = device->createTexture(od);
        REQUIRE(outCreated.has_value());
        out = *outCreated;
        renderer->setOutputResolution(1280, 720);
        return true;
    }
};

// Même terrain que la démo (World::Init, graine 1) : sans passer par l'ancien `Mesh`, qui exige l'ancien runtime graphique.
// Dessine la scène dans `setup.out` (comme `Game::render` avant l'interface) dans l'encodeur donné.
void drawTerrainScene(Setup &setup, rhi::CommandEncoder &e)
{
    Grid grid;
    grid.init(500.0f, 500.0f, 500, 500);
    Noise noise(1);
    grid.TransformPoints(
        [&](Vertex &vertex, unsigned int)
        {
            const float r = noise.FractalNoise(vertex.Position.x, vertex.Position.z, 0.01f, 10, 0.5f, 2.0f);
            vertex.Position.y = r * 50.0f;
            vertex.Color = glm::vec3(r, 0.0f, -r);
        });
    rd::MeshData data;
    data.name = "terrain";
    for (const Vertex &v : grid.GetPoints())
    {
        for (const float f : {v.Position.x, v.Position.y, v.Position.z, v.Normal.x, v.Normal.y, v.Normal.z, v.Color.x, v.Color.y, v.Color.z})
        {
            data.vertices.push_back(f);
        }
    }
    for (const auto &t : grid.GetTriangles())
    {
        data.indices.insert(data.indices.end(), t.begin(), t.end());
    }
    data.attributeSizes = {3, 3, 3};
    auto mesh = rd::Mesh::create(*setup.device, data);
    REQUIRE(mesh.has_value());
    auto program = setup.library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
    REQUIRE(program.has_value());
    mesh->setProgram(*program);

    auto lights = rd::LightManager::create(*setup.device);
    REQUIRE(lights.has_value());
    lights->setAmbientLight({1.0f, 1.0f, 1.0f}, 0.2f);
    lights->addLight(rd::directionalLight({2.0f, -3.0f, 0.5f}, {0.99f, 0.76f, 0.81f}, 1.0f));
    REQUIRE(lights->upload().has_value());
    setup.renderer->setLights(&*lights);

    rd::Camera camera; // réglages de Game::init
    camera.setPosition({0.0f, 1.0f, 0.0f});
    camera.setFovDegrees(75.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(1000.0f);
    camera.setAspect(1280, 720);

    REQUIRE(setup.renderer->beginFrame(e, {.clearColor = {0.07f, 0.13f, 0.17f, 1.0f}}).has_value());
    REQUIRE(setup.renderer->setCamera(camera).has_value());
    REQUIRE(setup.renderer->drawMesh(*mesh, camera).has_value());
    REQUIRE(setup.renderer->endFrame({setup.out, rhi::Format::kRGBA8Unorm, {1280, 720}}).has_value());
    setup.renderer->setLights(nullptr);
}

} // namespace

TEST_SUITE("renderer/parity")
{
    TEST_CASE("the seed-1 terrain scene rendered through the RHI matches the legacy reference image (docs/BASELINE/reference)")
    {
        Setup setup;
        if (!setup.create())
        {
            return;
        }
        auto encoder = setup.device->createCommandEncoder();
        drawTerrainScene(setup, *encoder);
        REQUIRE(setup.device->submit(std::move(encoder)).has_value());

        auto image = rd::readImage(*setup.device, setup.renderer->sceneTarget().color(0));
        REQUIRE(image.has_value());
        const fs::path reference = repoRoot() / "docs" / "BASELINE" / "reference" / "scene-opengl-seed1-1280x720.png";
        auto expected = rd::loadPng(reference);
        INFO((expected ? std::string() : expected.error().message));
        REQUIRE(expected.has_value());
        auto diff = rd::compareImages(*image, *expected, 4);
        REQUIRE(diff.has_value());
        MESSAGE("pixels hors tolérance : ", diff->outOfTolerance, " / ", diff->pixels, ", écart max ", static_cast<int>(diff->maxDelta), ", moyen ", diff->meanDelta);
        if (diff->outOfTolerance > diff->pixels / 1000)
        {
            (void)rd::savePng(*image, fs::temp_directory_path() / "engine_scene_parity_actual.png");
        }
        // Intel Arc (référence) : 0 pixel d'écart à la tolérance 0. Un autre pilote (Mesa llvmpipe en CI, G30) peut différer de quelques
        // pixels de bord : tolérance de 4 par canal et au plus 0,1 % de pixels au-delà.
        CHECK(diff->outOfTolerance <= diff->pixels / 1000);
        CHECK(setup.device->validationErrorCount() == 0);
    }

    TEST_CASE("the legacy UI capture (ui.png: scene + panel + boxes, fixed text) is reproduced by draw list + CanvasRenderer")
    {
        Setup setup;
        if (!setup.create())
        {
            return;
        }
        const CurrentDirectory cwd(repoRoot()); // les polices se cherchent en `res/fonts/...` depuis le dossier courant
        UI::Manager &manager = UI::Manager::Instance();
        manager.Init(1280, 720);
        // Le panneau de performances de la démo (PerformanceView.cui) avec le texte figé de la référence.
        using namespace UI;
        auto panel = CreateVBox(Bounds(Value{260.0, ValueType::PIXEL}, Value{175.0, ValueType::PIXEL}, Anchor::TOP_LEFT));
        panel->SetPadding(12.0f);
        panel->SetSpacing(4.0f);
        panel->SetColor(0.05f, 0.05f, 0.08f, 0.65f);
        panel->AddChild(CreateText(Bounds(), TextContent("FPS: 60.0"))->SetTextScale(0.5f));
        for (const char *line : {"Render: 1.234 ms", "Render World: 0.987 ms", "Upscale: 0.123 ms", "UI Upscale: 0.045 ms", "Swap Buffers: 2.500 ms"})
        {
            panel->AddChild(CreateText(Bounds(), TextContent(line))->SetTextScale(0.3f));
        }
        auto view = CreateContainer(Bounds(Value{260.0, ValueType::PIXEL}, Value{175.0, ValueType::PIXEL}, Anchor::TOP_LEFT), {panel});
        view->SetIdentifierKind(IdentifierKind::TRANSPARENT);
        manager.GetRootContainer()->AddChild(view);
        for (int i = 0; i < 3; ++i)
        {
            manager.Update(0.016f, 1280, 720);
        }

        rd::DrawList list;
        manager.Collect(list, 1280, 720);
        REQUIRE(list.count<rd::DrawGlyphs>() == 6);

        auto encoder = setup.device->createCommandEncoder();
        drawTerrainScene(setup, *encoder);
        auto rendered = setup.canvas->render(*encoder, list, {setup.out, rhi::Format::kRGBA8Unorm, {1280, 720}}, true);
        INFO((rendered ? std::string() : rendered.error().message));
        REQUIRE(rendered.has_value());
        REQUIRE(setup.device->submit(std::move(encoder)).has_value());
        manager.Shutdown();

        auto image = rd::readImage(*setup.device, setup.out);
        REQUIRE(image.has_value());
        auto expected = rd::loadPng(repoRoot() / "docs" / "BASELINE" / "reference" / "ui-opengl-fixedtext-1280x720.png");
        INFO((expected ? std::string() : expected.error().message));
        REQUIRE(expected.has_value());
        auto diff = rd::compareImages(*image, *expected, 4);
        REQUIRE(diff.has_value());
        MESSAGE("pixels hors tolérance : ", diff->outOfTolerance, " / ", diff->pixels, ", écart max ", static_cast<int>(diff->maxDelta), ", moyen ", diff->meanDelta);
        if (diff->outOfTolerance > diff->pixels / 1000)
        {
            (void)rd::savePng(*image, fs::temp_directory_path() / "engine_ui_parity_actual.png");
        }
        CHECK(diff->outOfTolerance <= diff->pixels / 1000);
        CHECK(setup.device->validationErrorCount() == 0);
    }
}
