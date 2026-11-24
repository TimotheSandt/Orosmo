// Perf M5 (option B) : données par image dans des anneaux de tampons kUpload, un par image en vol. Compteurs observables : nombre de
// `updateBuffer` par image, zone (tampon) écrite par chaque image, aucune zone réécrite tant que son image peut être en vol.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/counting_device.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/batch_arena.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/debug_draw.hpp>
#include <orosmo/renderer/frame_ring.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>

#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

rd::MeshData redTriangle()
{
    rd::MeshData d;
    d.vertices = {-3, -3, -1, 0, 0, 1, 1, 0, 0, //
                  3,  -3, -1, 0, 0, 1, 1, 0, 0, //
                  0,  3,  -1, 0, 0, 1, 1, 0, 0};
    d.indices = {0, 1, 2};
    d.attributeSizes = {3, 3, 3};
    d.name = "ring.triangle";
    return d;
}

// Rend `frames` images (deux dessins du même maillage, des lignes de debug) et rend les écritures de chaque image.
std::vector<std::vector<tests::CountingDevice::Write>> renderFrames(Gpu &gpu, std::uint32_t framesInFlight, int frames, bool &drewRed)
{
    tests::CountingDevice counting(*gpu.device);
    rd::ShaderLibrary library(counting);
    rd::PipelineCache pipelines(counting);
    auto created = rd::SceneRenderer::create(counting, library, pipelines, {.shaderDirectory = shaderDir(), .framesInFlight = framesInFlight});
    INFO((created ? std::string() : created.error().message));
    REQUIRE(created.has_value());
    auto renderer = std::move(*created);
    CHECK(renderer->framesInFlight() == framesInFlight);
    auto program = library.loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
    REQUIRE(program.has_value());
    auto mesh = rd::Mesh::create(counting, redTriangle());
    REQUIRE(mesh.has_value());
    mesh->setProgram(*program);
    auto lights = rd::LightManager::create(counting);
    REQUIRE(lights.has_value());
    lights->setAmbientLight({1, 1, 1}, 1.0f); // éclairage 1 : couleur du sommet inchangée
    REQUIRE(lights->upload().has_value());
    renderer->setLights(&*lights);
    rd::DebugDraw debug;
    debug.setEnabled(true);
    debug.line({0, 0, -1}, {1, 1, -1}, {0, 1, 0, 1});
    rd::Camera camera;
    camera.setAspect(8, 8);
    camera.setFarPlane(100.0f);
    renderer->setOutputResolution(8, 8);
    const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {8, 8}, "output");
    std::vector<std::vector<tests::CountingDevice::Write>> perFrame;
    for (int f = 0; f < frames; ++f)
    {
        counting.writes.clear();
        mesh->setPosition({0.0f, 0.0f, 0.001f * static_cast<float>(f)}); // transformation qui change à chaque image
        auto encoder = counting.createCommandEncoder();
        REQUIRE(renderer->beginFrame(*encoder).has_value());
        REQUIRE(renderer->setCamera(camera).has_value());
        REQUIRE(renderer->drawMesh(*mesh, camera).has_value());
        REQUIRE(renderer->drawMesh(*mesh, camera).has_value());
        REQUIRE(renderer->drawDebug(debug).has_value());
        REQUIRE(renderer->endFrame({out, Format::kRGBA8Unorm, {8, 8}}).has_value());
        REQUIRE(counting.submit(std::move(encoder)).has_value());
        for (const auto &w : counting.writes)
        {
            CHECK(counting.memory[tests::CountingDevice::key(w.buffer)] == MemoryKind::kUpload);
            CHECK(w.buffer == renderer->frameRingBuffer());
        }
        perFrame.push_back(counting.writes);
    }
    drewRed = rgba(gpu.pixel(out, 4, 4))[0] > 200;
    CHECK(counting.validationErrorCount() == 0);
    return perFrame;
}

} // namespace

TEST_SUITE("renderer/frame-ring")
{
    TEST_CASE("SceneRenderer: one updateBuffer per frame (FrameData, camera, model slices, debug lines) into its own ring zone")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            bool red = false;
            const auto frames = renderFrames(gpu, 2, 6, red);
            CHECK(red);
            for (std::size_t f = 0; f < frames.size(); ++f)
            {
                INFO("frame ", f);
                // Avant : caméra, FrameData, tranches, lignes de debug et un uploadTransform par dessin (6 écritures ici).
                REQUIRE(frames[f].size() == 1);
                CHECK(frames[f][0].offset == 0);
                if (f >= 1)
                {
                    CHECK_FALSE(frames[f][0].buffer == frames[f - 1][0].buffer); // l'image précédente (en vol) garde sa zone
                }
                if (f >= 2)
                {
                    CHECK(frames[f][0].buffer == frames[f - 2][0].buffer); // zone réutilisée après framesInFlight images
                }
            }
        }
    }

    TEST_CASE("SceneRenderer: a deeper ring (framesInFlight = 3) rewrites a zone only three frames later")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            bool red = false;
            const auto frames = renderFrames(gpu, 3, 7, red);
            CHECK(red);
            for (std::size_t f = 0; f < frames.size(); ++f)
            {
                INFO("frame ", f);
                REQUIRE(frames[f].size() == 1);
                for (std::size_t back = 1; back < 3 && back <= f; ++back)
                {
                    CHECK_FALSE(frames[f][0].buffer == frames[f - back][0].buffer);
                }
                if (f >= 3)
                {
                    CHECK(frames[f][0].buffer == frames[f - 3][0].buffer);
                }
            }
        }
    }

    TEST_CASE("BatchArena: distinct ranges within an encoder, a new encoder takes the next zone, a zone comes back after framesInFlight encoders")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::CountingDevice counting(*gpu.device);
            rd::BatchArena arena;
            CHECK(arena.framesInFlight() == rd::kDefaultFramesInFlight);
            const std::vector<float> data(64, 1.0f);
            std::vector<BufferHandle> zone;
            for (std::uint64_t encoder = 10; encoder < 14; ++encoder)
            {
                auto a = arena.upload(counting, encoder, bytesOf(data), "test.batch");
                auto b = arena.upload(counting, encoder, bytesOf(data), "test.batch");
                REQUIRE(a.has_value());
                REQUIRE(b.has_value());
                CHECK(a->buffer == b->buffer);
                CHECK(b->offset >= a->offset + data.size() * sizeof(float)); // deux envois du même encodeur ne se recouvrent pas
                CHECK(a->offset == 0);                                       // un nouvel encodeur repart du début de SA zone
                CHECK(counting.memory[tests::CountingDevice::key(a->buffer)] == MemoryKind::kUpload);
                zone.push_back(a->buffer);
            }
            CHECK_FALSE(zone[1] == zone[0]); // la zone de l'encodeur précédent n'est pas réécrite
            CHECK(zone[2] == zone[0]);
            CHECK(zone[3] == zone[1]);
            CHECK(counting.writes.size() == 8);
        }
    }

    TEST_CASE("BatchArena: an encoder that comes back keeps its zone; one whose zone was taken gets kBusy")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::BatchArena arena(2);
            const std::vector<float> data(64, 1.0f);
            auto a = arena.upload(*gpu.device, 21, bytesOf(data), "test.batch"); // E1
            auto b = arena.upload(*gpu.device, 22, bytesOf(data), "test.batch"); // E2
            auto c = arena.upload(*gpu.device, 21, bytesOf(data), "test.batch"); // E1 revient
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            REQUIRE(c.has_value());
            CHECK(c->buffer == a->buffer);
            CHECK(c->offset >= a->offset + data.size() * sizeof(float)); // A n'est pas réécrit
            CHECK_FALSE(b->buffer == a->buffer);
            // N zones, N changements d'encodeur : E3 reprend la zone de E1, qui ne peut plus écrire.
            auto d = arena.upload(*gpu.device, 23, bytesOf(data), "test.batch");
            REQUIRE(d.has_value());
            CHECK(d->buffer == a->buffer);
            CHECK(d->offset == 0);
            auto late = arena.upload(*gpu.device, 21, bytesOf(data), "test.batch");
            REQUIRE_FALSE(late.has_value());
            CHECK((late.error().code == orosmo::core::ErrorCode::kBusy));
            auto e2 = arena.upload(*gpu.device, 22, bytesOf(data), "test.batch"); // E2 garde la sienne
            REQUIRE(e2.has_value());
            CHECK(e2->buffer == b->buffer);
            CHECK(e2->offset > b->offset);
        }
    }

    TEST_CASE("BatchArena: the first upload of an older encoder after a newer one gets a zone, not kBusy")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::BatchArena arena(2);
            const std::vector<float> data(64, 1.0f);
            auto newer = arena.upload(*gpu.device, 32, bytesOf(data), "test.batch");
            auto older = arena.upload(*gpu.device, 31, bytesOf(data), "test.batch"); // jamais servi : pas de kBusy
            REQUIRE(newer.has_value());
            REQUIRE(older.has_value());
            CHECK_FALSE(older->buffer == newer->buffer);
            auto third = arena.upload(*gpu.device, 33, bytesOf(data), "test.batch"); // reprend la zone de 32
            REQUIRE(third.has_value());
            auto late = arena.upload(*gpu.device, 32, bytesOf(data), "test.batch");
            REQUIRE_FALSE(late.has_value());
            CHECK((late.error().code == orosmo::core::ErrorCode::kBusy));
        }
    }

    TEST_CASE("Mesh: prepareTransform writes nothing; uploadTransform writes the mesh's own block only when it is stale")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::CountingDevice counting(*gpu.device);
            auto mesh = rd::Mesh::create(counting, redTriangle());
            REQUIRE(mesh.has_value());
            counting.writes.clear();
            REQUIRE(mesh->uploadTransform().has_value());
            CHECK(counting.writes.empty()); // rien n'a changé depuis create
            mesh->setPosition({1, 2, 3});
            REQUIRE(mesh->prepareTransform({0.5f, 0.0f, 0.0f}).has_value());
            CHECK(counting.writes.empty());
            const auto block = mesh->modelBlock();
            float tx = 0.0f;
            std::memcpy(&tx, block.data() + 12 * sizeof(float), sizeof tx); // model[3].x
            CHECK(tx == doctest::Approx(0.5f));
            REQUIRE(mesh->uploadTransform({0.5f, 0.0f, 0.0f}).has_value()); // même bloc, mais jamais envoyé : écrit une fois
            CHECK(counting.writes.size() == 1);
            REQUIRE(mesh->uploadTransform({0.5f, 0.0f, 0.0f}).has_value());
            CHECK(counting.writes.size() == 1);
            CHECK(counting.validationErrorCount() == 0);
        }
    }
}
