// REN-06, REN-07, REN-08 : ombres. Partie CPU (comportement de chaque combinaison mobilité × mode, cascades stables, atlas, matrices)
// sans GPU ; partie GPU (plugin ShadowFeature dans le graphe de SceneRenderer) sur chaque backend, couches de validation à 0.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/forward_plus.hpp>
#include <orosmo/renderer/instance_renderer.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/object_set.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/renderer/shadows.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

std::string readText(const fs::path &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

rd::CameraShape perspectiveShape()
{
    rd::Camera camera;
    camera.setPosition({3.0f, 4.0f, 5.0f});
    camera.setOrientation(glm::normalize(glm::vec3(0.3f, -0.2f, -1.0f)));
    camera.setFovDegrees(60.0f);
    camera.setAspect(16.0f / 9.0f);
    camera.setNearPlane(0.25f);
    return rd::cameraShapeFrom(camera.viewMatrix(), camera.projectionMatrix(), glm::vec3(0.0f));
}

} // namespace

TEST_SUITE("renderer/shadows")
{
    TEST_CASE("resolveShadowBehavior: documented behavior of every (mobility, shadow mode) combination (REN-06)")
    {
        using M = rd::LightMobility;
        using S = rd::ShadowMode;
        using St = rd::ShadowStatus;
        struct Row
        {
            M mobility;
            S mode;
            bool realtime;
            bool cache;
            St status;
        };
        const Row rows[] = {
            {M::kStatic, S::kNone, false, false, St::kNone},
            {M::kStatic, S::kBaked, true, true, St::kBakedFallback},
            {M::kStatic, S::kDynamic, true, false, St::kRealtime},
            {M::kStatic, S::kCached, true, true, St::kRealtime},
            {M::kStationary, S::kNone, false, false, St::kNone},
            {M::kStationary, S::kBaked, true, true, St::kBakedFallback},
            {M::kStationary, S::kDynamic, true, false, St::kRealtime},
            {M::kStationary, S::kCached, true, true, St::kRealtime},
            {M::kDynamic, S::kNone, false, false, St::kNone},
            {M::kDynamic, S::kBaked, true, false, St::kInvalidCombination},
            {M::kDynamic, S::kDynamic, true, false, St::kRealtime},
            {M::kDynamic, S::kCached, true, true, St::kRealtime},
        };
        for (const Row &row : rows)
        {
            const rd::ShadowBehavior b = rd::resolveShadowBehavior(row.mobility, row.mode);
            INFO("mobility ", int(row.mobility), " mode ", int(row.mode));
            CHECK(b.realtime == row.realtime);
            CHECK(b.cacheStatic == row.cache);
            CHECK(b.status == row.status);
            CHECK(b.mayMove == (row.mobility == M::kDynamic));
            CHECK(b.mayRecolor == (row.mobility != M::kStatic));
        }
        rd::Light defaults;
        CHECK(defaults.mobility == M::kDynamic);
        CHECK(defaults.shadowMode == S::kNone); // une lumière existante n'a pas d'ombre sans le demander
    }

    TEST_CASE("cascadeSplits and cameraShapeFrom (REN-07)")
    {
        const auto splits = rd::cascadeSplits(0.1f, 100.0f, 4, 0.75f);
        REQUIRE(splits.size() == 4);
        CHECK(splits.back() == 100.0f);
        for (std::size_t i = 1; i < splits.size(); ++i)
        {
            CHECK(splits[i] > splits[i - 1]);
        }
        const auto uniform = rd::cascadeSplits(0.0f + 1.0f, 9.0f, 4, 0.0f);
        CHECK(uniform[0] == doctest::Approx(3.0f));
        CHECK(uniform[1] == doctest::Approx(5.0f));
        CHECK(rd::cascadeSplits(1.0f, 2.0f, 0, 0.5f).empty());

        // Perspective infinie, perspective finie, orthographique : plan proche, champ et position retrouvés depuis les matrices.
        rd::Camera camera;
        camera.setPosition({1.0f, 2.0f, 3.0f});
        camera.setFovDegrees(70.0f);
        camera.setAspect(2.0f);
        camera.setNearPlane(0.3f);
        camera.setJitter({0.01f, -0.02f}); // ignoré
        for (int kind = 0; kind < 3; ++kind)
        {
            if (kind == 1)
            {
                camera.setFarPlane(500.0f);
            }
            if (kind == 2)
            {
                camera.setOrthographic(10.0f);
            }
            const rd::CameraShape s = rd::cameraShapeFrom(camera.relativeViewMatrix(), camera.projectionMatrix(), camera.position());
            INFO("kind ", kind);
            CHECK(s.nearPlane == doctest::Approx(0.3f).epsilon(1e-3));
            CHECK(glm::length(s.position - camera.position()) < 1e-4f);
            CHECK(glm::length(s.forward - camera.orientation()) < 1e-4f);
            CHECK(s.orthographic == (kind == 2));
            if (kind == 2)
            {
                CHECK(s.halfY == doctest::Approx(5.0f));
                CHECK(s.halfX == doctest::Approx(10.0f));
            }
            else
            {
                CHECK(s.halfY == doctest::Approx(std::tan(glm::radians(35.0f))));
                CHECK(s.halfX == doctest::Approx(2.0f * std::tan(glm::radians(35.0f))));
            }
        }
    }

    TEST_CASE("fitCascade: the slice is inside the cascade; translation moves it by whole texels; rotation keeps its size (REN-07)")
    {
        const glm::vec3 toLight = glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f));
        const std::uint32_t resolution = 1024;
        rd::CameraShape camera = perspectiveShape();
        const rd::ShadowViewFit fit = rd::fitCascade(camera, 2.0f, 15.0f, toLight, resolution, 20.0f);
        CHECK(fit.texelWorld == doctest::Approx(2.0f * fit.radius / resolution));
        for (const glm::vec3 &corner : rd::sliceCorners(camera, 2.0f, 15.0f))
        {
            const glm::vec4 clip = fit.viewProjection * glm::vec4(corner, 1.0f);
            CHECK(std::abs(clip.x) <= 1.0f);
            CHECK(std::abs(clip.y) <= 1.0f);
            CHECK(clip.z >= 0.0f);
            CHECK(clip.z <= 1.0f);
        }
        // Un point de lumière vers la sphère (20 m de plus) reste dans la profondeur : casters hors de la tranche.
        const glm::vec4 caster = fit.viewProjection * glm::vec4(fit.center + toLight * (fit.radius + 15.0f), 1.0f);
        CHECK(caster.z >= 0.0f);

        // Stabilité : déplacements quelconques (sous-texel compris) de la caméra. Un point fixe du monde se projette au même
        // endroit à un nombre ENTIER de texels près : la grille de l'ombre ne glisse pas, les bords ne scintillent pas.
        const glm::vec3 probe(5.0f, 1.0f, -7.0f);
        int identical = 0;
        for (int step = 1; step <= 40; ++step)
        {
            rd::CameraShape moved = camera;
            moved.position += glm::vec3(0.013f * static_cast<float>(step), -0.007f * static_cast<float>(step), 0.021f * static_cast<float>(step));
            const rd::ShadowViewFit other = rd::fitCascade(moved, 2.0f, 15.0f, toLight, resolution, 20.0f);
            CHECK(other.radius == fit.radius);
            const glm::vec2 a = glm::vec2(fit.viewProjection * glm::vec4(probe, 1.0f)) * (0.5f * static_cast<float>(resolution));
            const glm::vec2 b = glm::vec2(other.viewProjection * glm::vec4(probe, 1.0f)) * (0.5f * static_cast<float>(resolution));
            const glm::vec2 shift = a - b;
            CHECK(std::abs(shift.x - std::round(shift.x)) < 2e-2f);
            CHECK(std::abs(shift.y - std::round(shift.y)) < 2e-2f);
            identical += other.viewProjection == fit.viewProjection ? 1 : 0;
        }
        CHECK(identical > 0); // un déplacement sous-texel ne change pas la matrice du tout (le cache reste valide)

        // Rotation : même rayon (la taille de la projection ne « respire » pas quand la caméra tourne).
        rd::CameraShape turned = camera;
        turned.forward = glm::normalize(glm::vec3(-0.8f, -0.1f, -0.4f));
        turned.right = glm::normalize(glm::cross(turned.forward, glm::vec3(0, 1, 0)));
        turned.up = glm::cross(turned.right, turned.forward);
        CHECK(rd::fitCascade(turned, 2.0f, 15.0f, toLight, resolution, 20.0f).radius == fit.radius);

        // Orthographique aussi, et lumière verticale (vecteur haut de secours).
        camera.orthographic = true;
        camera.halfX = 8.0f;
        camera.halfY = 4.5f;
        const rd::ShadowViewFit ortho = rd::fitCascade(camera, 0.0f, 30.0f, glm::vec3(0, 1, 0), 512, 10.0f);
        for (const glm::vec3 &corner : rd::sliceCorners(camera, 0.0f, 30.0f))
        {
            const glm::vec4 clip = ortho.viewProjection * glm::vec4(corner, 1.0f);
            CHECK(std::abs(clip.x) <= 1.0f);
            CHECK(std::abs(clip.y) <= 1.0f);
            CHECK(std::isfinite(clip.z));
        }
    }

    TEST_CASE("fitSpot and atlasTileMatrix: receiver depth formula, tile mapping with v = 0 at the top (REN-08)")
    {
        const rd::ShadowViewFit spot = rd::fitSpot({1, 8, 2}, {0, -1, 0}, glm::radians(30.0f), 0.5f, 20.0f, 512);
        const glm::vec4 onAxis = spot.viewProjection * glm::vec4(1, 0, 2, 1); // 8 m sous le projecteur
        CHECK(onAxis.x / onAxis.w == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(onAxis.y / onAxis.w == doctest::Approx(0.0f).epsilon(1e-5));
        const float d = 8.0f, n = 0.5f, f = 20.0f;
        CHECK(onAxis.z / onAxis.w == doctest::Approx(f * (d - n) / (d * (f - n))).epsilon(1e-5)); // formule de shadow_receiver.glsl
        CHECK(spot.texelWorld == doctest::Approx(2.0f * std::tan(glm::radians(30.0f)) / 512.0f));

        const rd::AtlasTile tile{256, 512, 256};
        const glm::mat4 m = rd::atlasTileMatrix(tile, 1024);
        const glm::vec4 topLeft = m * glm::vec4(-1, 1, 0.25f, 1);
        const glm::vec4 bottomRight = m * glm::vec4(1, -1, 0.75f, 1);
        CHECK(topLeft.x == doctest::Approx(0.25f));
        CHECK(topLeft.y == doctest::Approx(0.5f));
        CHECK(topLeft.z == doctest::Approx(0.25f));
        CHECK(bottomRight.x == doctest::Approx(0.5f));
        CHECK(bottomRight.y == doctest::Approx(0.75f));
    }

    TEST_CASE("ShadowAtlasAllocator: power-of-two tiles, deterministic placement, buddy merge, full atlas (REN-08)")
    {
        rd::ShadowAtlasAllocator atlas(2048, 64);
        auto a = atlas.allocate(1024);
        auto b = atlas.allocate(1000); // arrondi à 1024
        auto c = atlas.allocate(1024);
        auto d = atlas.allocate(1024);
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(c);
        REQUIRE(d);
        CHECK(*a == rd::AtlasTile{0, 0, 1024});
        CHECK(*b == rd::AtlasTile{1024, 0, 1024});
        CHECK(*c == rd::AtlasTile{0, 1024, 1024});
        CHECK(*d == rd::AtlasTile{1024, 1024, 1024});
        CHECK_FALSE(atlas.allocate(64)); // plein
        CHECK(atlas.freeArea() == 0);
        atlas.release(*b);
        auto small = atlas.allocate(10); // au moins minTile
        REQUIRE(small);
        CHECK(*small == rd::AtlasTile{1024, 0, 64});
        atlas.release(*small);
        atlas.release(*a);
        atlas.release(*c);
        atlas.release(*d);
        atlas.release(*d); // double libération : sans effet
        CHECK(atlas.freeArea() == 2048ull * 2048ull);
        auto whole = atlas.allocate(2048); // les quatre quarts ont fusionné
        REQUIRE(whole);
        CHECK(*whole == rd::AtlasTile{0, 0, 2048});
        CHECK_FALSE(atlas.allocate(4096)); // plus grand que l'atlas

        rd::ShadowHash h1, h2, h3;
        h1.value(1.0f);
        h2.value(1.0f);
        h3.value(2.0f);
        CHECK(h1.digest() == h2.digest());
        CHECK(h1.digest() != h3.digest());
    }
}

// ── GPU ────────────────────────────────────────────────────────────────────────────────────────────────────────────
namespace
{

constexpr std::uint32_t kSize = 32;
// Sol gris 0.5 : à l'ombre, l'ambiante seule (0.5 × 0.2 → 26) ; éclairé, ambiante + diffuse + spéculaire (au moins 0.5 × 1.2,
// la spéculaire dépend du point : la caméra orthographique est traitée comme un point par mesh.frag).
constexpr int kLit = 140;

// Carré horizontal (normale +Y) à la hauteur y, couleur des sommets donnée.
rd::MeshData quadData(float x0, float z0, float x1, float z1, float y, float gray, const char *name)
{
    rd::MeshData d;
    d.vertices = {x0, y, z0, 0, 1, 0, gray, gray, gray, //
                  x1, y, z0, 0, 1, 0, gray, gray, gray, //
                  x1, y, z1, 0, 1, 0, gray, gray, gray, //
                  x0, y, z1, 0, 1, 0, gray, gray, gray};
    d.indices = {0, 1, 2, 0, 2, 3};
    d.attributeSizes = {3, 3, 3};
    d.name = name;
    return d;
}

// Pixel de l'image (32 × 32, caméra orthographique de 20 m vue d'en haut) au-dessus du point (x, 0, z) du sol.
std::pair<int, int> pixelOf(float x, float z)
{
    return {static_cast<int>((x + 10.0f) * kSize / 20.0f), static_cast<int>((z + 10.0f) * kSize / 20.0f)};
}

struct ShadowScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    rd::ShadowFeature *shadows = nullptr;
    rd::LightManager lights;
    rd::Camera camera;
    rd::Mesh ground;
    rd::Mesh caster;
    TextureHandle out;

    explicit ShadowScene(Backend backend, rd::ShadowSettings settings = smallSettings()) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        rd::ShaderLibraryConfig config;
        config.includeRoots = {shaderDir()};
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device, config);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        auto feature = rd::ShadowFeature::create(*gpu.device, *library, *pipelines, shaderDir(), settings);
        INFO((feature ? std::string() : feature.error().message));
        REQUIRE(feature.has_value());
        auto added = renderer->addFeature(std::move(*feature));
        REQUIRE(added.has_value());
        shadows = static_cast<rd::ShadowFeature *>(*added);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::move(*l);
        lights.setAmbientLight({1, 1, 1}, 0.2f);
        renderer->setLights(&lights);
        shadows->setLights(&lights);
        renderer->setOutputResolution(kSize, kSize);
        camera.setPosition({0.0f, 10.0f, 0.0f});
        camera.setOrientation({0.0f, -1.0f, 0.0f});
        camera.setUp({0.0f, 0.0f, -1.0f}); // haut de l'image : -Z ; droite : +X
        camera.setAspect(kSize, kSize);
        camera.setNearPlane(0.1f);
        camera.setFarPlane(50.0f);
        camera.setOrthographic(20.0f);

        auto g = rd::Mesh::create(*gpu.device, quadData(-10, -10, 10, 10, 0.0f, 0.5f, "ground"));
        auto c = rd::Mesh::create(*gpu.device, quadData(-6, -2, -2, 2, 2.0f, 1.0f, "caster"));
        REQUIRE(g.has_value());
        REQUIRE(c.has_value());
        ground = std::move(*g);
        caster = std::move(*c);
        ground.setProgram(shadows->receiverProgram());
        ground.setMaterialGroup(shadows->receiverGroup());
        out = gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output");
    }
    ~ShadowScene()
    {
        renderer.reset();
    }

    static rd::ShadowSettings smallSettings()
    {
        rd::ShadowSettings s;
        s.atlasSize = 512;
        s.cascadeCount = 2;
        s.cascadeResolution = 256;
        s.maxDistance = 20.0f;
        s.cascadeUpdateInterval = {1, 1, 1, 1};
        return s;
    }

    void sun(rd::LightMobility mobility, rd::ShadowMode mode, glm::vec3 direction = {0, 1, 0}, glm::vec3 color = {1, 1, 1})
    {
        rd::Light light = rd::directionalLight(direction, color, 1.0f);
        light.mobility = mobility;
        light.shadowMode = mode;
        if (lights.lights().empty())
        {
            lights.addLight(light);
        }
        else
        {
            REQUIRE(lights.setLight(0, light).has_value());
        }
        REQUIRE(lights.upload().has_value());
    }

    rd::ShadowStats frame(rd::Mesh *receiver = nullptr)
    {
        gpu.run(
            [&](CommandEncoder &e)
            {
                REQUIRE(renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                REQUIRE(renderer->setCamera(camera).has_value());
                REQUIRE(renderer->drawMesh(receiver != nullptr ? *receiver : ground, camera).has_value());
                auto r = renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                INFO((r ? std::string() : r.error().message));
                REQUIRE(r.has_value());
            });
        return shadows->stats();
    }

    int red(float x, float z)
    {
        const auto [px, py] = pixelOf(x, z);
        return rgba(gpu.pixel(out, px, py))[0];
    }
};

} // namespace

TEST_SUITE("renderer/shadows-gpu")
{
    TEST_CASE("ShadowFeature: cascaded shadow under a caster, lit elsewhere, same image in camera-relative rendering (REN-07)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kStatic).has_value());
            scene.sun(rd::LightMobility::kDynamic, rd::ShadowMode::kDynamic);
            const rd::ShadowStats stats = scene.frame();
            CHECK(stats.views == 2);
            CHECK(stats.viewsUpdated == 2);
            CHECK(stats.fullRedraws == 2);
            CHECK(stats.renderPasses == 2); // effacement initial du cache + atlas
            CHECK(scene.shadows->lightStatus(0) == rd::ShadowStatus::kRealtime);
            // Lumière verticale : ombre du carré (-6..-2, -2..2) juste dessous.
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2));
            CHECK(scene.red(4.0f, 0.0f) > kLit);
            CHECK(scene.red(-4.0f, 6.0f) > kLit);
            const auto order = scene.renderer->renderGraph().executionOrder();
            const auto at = [&](const char *name) { return std::find(order.begin(), order.end(), name) - order.begin(); };
            REQUIRE(at("scene") < static_cast<std::ptrdiff_t>(order.size()));
            CHECK(at("shadow.cache") < at("shadow.atlas"));
            CHECK(at("shadow.atlas") < at("scene")); // passes d'ombre avant la scène (plugin, cœur du graphe inchangé)

            // Rendu relatif à la caméra : même image, caméra et monde déplacés ensemble.
            const auto before = scene.gpu.pixel(scene.out, 5, 16);
            scene.renderer->setCameraRelative(true);
            scene.frame();
            CHECK(rgba(scene.gpu.pixel(scene.out, 5, 16)) == rgba(before));
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2));
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("ShadowFeature cache: nothing redrawn when nothing moves; a movable caster redraws over the cached static geometry (REN-08)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::Mesh wall;
            auto w = rd::Mesh::create(*scene.gpu.device, quadData(2, -8, 6, -4, 3.0f, 1.0f, "static"));
            REQUIRE(w.has_value());
            wall = std::move(*w);
            REQUIRE(scene.shadows->addMeshCaster(wall, {{2, 3, -8}, {6, 3, -4}}, rd::CasterMobility::kStatic).has_value());
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kMovable).has_value());
            scene.sun(rd::LightMobility::kStationary, rd::ShadowMode::kCached);

            rd::ShadowStats s = scene.frame();
            CHECK(s.staticRedraws == 2);
            CHECK(s.cacheCopies == 2);
            CHECK(s.movableRedraws == 2);
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2)); // mobile
            CHECK(near(static_cast<std::uint8_t>(scene.red(4.0f, -6.0f)), 26, 2)); // fixe (recopiée depuis le cache)

            s = scene.frame(); // image statique
            CHECK(s.viewsUpdated == 0);
            CHECK(s.cacheHits == 2);
            CHECK(s.renderPasses == 0);
            CHECK(s.drawCalls == 0);
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2));

            // Le caster mobile bouge : recopie du cache + caster mobile seulement (la géométrie fixe n'est pas redessinée).
            scene.caster.setPosition({0.0f, 0.0f, 6.0f});
            s = scene.frame();
            CHECK(s.staticRedraws == 0);
            CHECK(s.cacheCopies == 2);
            CHECK(s.movableRedraws == 2);
            CHECK(s.renderPasses == 1);
            CHECK(s.drawCalls == 2);
            CHECK(scene.red(-4.0f, 0.0f) > kLit); // l'ancienne ombre a disparu
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 6.0f)), 26, 2));  // la nouvelle est là
            CHECK(near(static_cast<std::uint8_t>(scene.red(4.0f, -6.0f)), 26, 2));  // la fixe est restée

            // La géométrie fixe bouge : le cache est invalidé et redessiné.
            wall.setPosition({0.0f, 0.0f, 10.0f});
            s = scene.frame();
            CHECK(s.staticRedraws == 2);
            CHECK(scene.red(4.0f, -6.0f) > kLit);
            CHECK(near(static_cast<std::uint8_t>(scene.red(4.0f, 4.0f)), 26, 2));

            // La caméra bouge de plus d'un texel : la vue change, tout est redessiné ; puis plus rien.
            scene.camera.setPosition({1.0f, 10.0f, 0.0f});
            s = scene.frame();
            CHECK(s.staticRedraws >= 1);
            s = scene.frame();
            CHECK(s.viewsUpdated == 0);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("ShadowFeature: every (mobility, mode) combination on a static scene; mobility violations (REN-06)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kStatic).has_value());
            for (const auto mobility : {rd::LightMobility::kStatic, rd::LightMobility::kStationary, rd::LightMobility::kDynamic})
            {
                for (const auto mode : {rd::ShadowMode::kNone, rd::ShadowMode::kBaked, rd::ShadowMode::kDynamic, rd::ShadowMode::kCached})
                {
                    const rd::ShadowBehavior expected = rd::resolveShadowBehavior(mobility, mode);
                    INFO("mobility ", int(mobility), " mode ", int(mode));
                    scene.sun(mobility, mode);
                    scene.shadows->invalidate();
                    scene.frame();
                    scene.frame();
                    const rd::ShadowStats third = scene.frame(); // image statique
                    CHECK(scene.shadows->lightStatus(0) == expected.status);
                    if (!expected.realtime)
                    {
                        CHECK(third.views == 0);
                        CHECK(scene.red(-4.0f, 0.0f) > kLit); // pas d'ombre
                        continue;
                    }
                    CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2));
                    if (expected.cacheStatic)
                    {
                        CHECK(third.viewsUpdated == 0); // cache : rien ne bouge, rien n'est redessiné
                        CHECK(third.cacheHits == 2);
                    }
                    else
                    {
                        CHECK(third.fullRedraws == 2); // Dynamic : redessinée à chaque image
                    }
                }
            }
            // Violations : une lumière Static qui tourne ou change de couleur, une Stationary qui tourne. Recolorer une
            // Stationary et déplacer une Dynamic sont permis.
            const auto violations = [&] { return scene.shadows->stats().mobilityViolations; };
            scene.sun(rd::LightMobility::kStatic, rd::ShadowMode::kCached);
            scene.frame();
            std::uint32_t base = violations();
            scene.sun(rd::LightMobility::kStatic, rd::ShadowMode::kCached, glm::normalize(glm::vec3(0.1f, 1, 0)));
            rd::ShadowStats s = scene.frame();
            CHECK(violations() == base + 1);
            CHECK(s.staticRedraws == 2); // la vue suit quand même la lumière
            scene.sun(rd::LightMobility::kStatic, rd::ShadowMode::kCached, glm::normalize(glm::vec3(0.1f, 1, 0)), {1, 0.5f, 0.5f});
            scene.frame();
            CHECK(violations() == base + 2);
            scene.sun(rd::LightMobility::kStationary, rd::ShadowMode::kCached, glm::normalize(glm::vec3(0.1f, 1, 0)), {1, 1, 1});
            scene.frame();
            CHECK(violations() == base + 2);
            scene.sun(rd::LightMobility::kStationary, rd::ShadowMode::kCached, glm::vec3(0, 1, 0));
            scene.frame();
            CHECK(violations() == base + 3);
            scene.sun(rd::LightMobility::kDynamic, rd::ShadowMode::kCached, glm::normalize(glm::vec3(-0.1f, 1, 0)));
            s = scene.frame();
            CHECK(violations() == base + 3);
            CHECK(s.staticRedraws == 2); // lumière mobile en Cached : le cache suit la lumière
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("ShadowFeature: update budget, amortized cascades, atlas priority (REN-08)")
    {
        FOR_EACH_BACKEND(backend)
        {
            rd::ShadowSettings settings = ShadowScene::smallSettings();
            settings.maxViewUpdatesPerFrame = 1;
            ShadowScene scene(backend, settings);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kStatic).has_value());
            scene.sun(rd::LightMobility::kDynamic, rd::ShadowMode::kDynamic);
            rd::ShadowStats s = scene.frame();
            CHECK(s.viewsUpdated == 1);
            CHECK(s.deferred == 1);
            s = scene.frame(); // la vue jamais dessinée passe avant (pas de famine)
            CHECK(s.viewsUpdated == 1);
            CHECK(scene.shadows->gpuData().cascadeParams[0].z == 1.0f);
            CHECK(scene.shadows->gpuData().cascadeParams[1].z == 1.0f);
            s = scene.frame();
            CHECK(s.viewsUpdated == 1); // puis à tour de rôle
            CHECK(s.deferred == 1);

            // Amortissement : la cascade 1 une image sur trois.
            settings.maxViewUpdatesPerFrame = 8;
            settings.cascadeUpdateInterval = {1, 3, 1, 1};
            REQUIRE(scene.shadows->setSettings(settings).has_value());
            std::uint32_t updates = 0;
            for (int i = 0; i < 6; ++i)
            {
                updates += scene.frame().viewsUpdated;
            }
            CHECK(updates == 6 + 2);

            // Atlas plein : 4 cascades de 256 remplissent 512 ; un projecteur plus prioritaire prend la place de la dernière.
            settings.cascadeCount = 4;
            settings.cascadeUpdateInterval = {1, 1, 1, 1};
            REQUIRE(scene.shadows->setSettings(settings).has_value());
            rd::Light spot = rd::spotLight({0, 8, 0}, {0, -1, 0}, {1, 1, 1}, 1.0f, std::cos(glm::radians(30.0f)), 0.95f, 0.0f, 0.0f);
            spot.shadowMode = rd::ShadowMode::kCached;
            scene.lights.addLight(spot);
            REQUIRE(scene.lights.upload().has_value());
            scene.shadows->setLightParams(1, {.priority = 5, .maxResolution = 256});
            s = scene.frame();
            CHECK(s.views == 5);
            CHECK(s.dropped == 1);
            CHECK(scene.shadows->lightStatus(0) == rd::ShadowStatus::kRealtime); // trois cascades sur quatre
            CHECK(scene.shadows->lightStatus(1) == rd::ShadowStatus::kRealtime);
            CHECK(scene.shadows->gpuData().atlas.z == 1.0f);
            CHECK(scene.shadows->gpuData().cascadeParams[3].z == 0.0f);
            // Fondu : projecteur au-delà de sa distance de fondu (caméra à 2 m) : plus de vue.
            scene.shadows->setLightParams(1, {.priority = 5, .maxResolution = 256, .fadeStart = 0.5f, .fadeEnd = 1.0f});
            s = scene.frame();
            CHECK(scene.shadows->lightStatus(1) == rd::ShadowStatus::kFaded);
            CHECK(s.views == 4);
            CHECK(scene.shadows->gpuData().atlas.z == 0.0f);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("Spot shadow and exact visibility through shadow_receiver.glsl (REN-08)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kStatic).has_value());
            // Matériau de test : sort la visibilité de la lumière LIGHT (rouge) telle que la calcule shadow_receiver.glsl.
            const std::string fs = R"(#version 450
#define OROSMO_SHADOWS 1
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 crntPos;
layout(location = 2) in vec3 color;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0, std140) uniform CamBlock { vec3 position; mat4 matrix; } camera;
#include "shadow_receiver.glsl"
void main()
{
    float s = orosmoLightShadow(LIGHT, crntPos, normalize(normal), vec3(0.0, 1.0, 0.0), camera.position);
    FragColor = vec4(s, 0.0, 0.0, 1.0);
}
)";
            auto program = scene.library->graphicsFromSource("shadow-visibility", readText(shaderDir() / "mesh.vert"), fs, {{"LIGHT", "0"}});
            INFO((program ? std::string() : program.error().message));
            REQUIRE(program.has_value());
            auto g = rd::Mesh::create(*scene.gpu.device, quadData(-10, -10, 10, 10, 0.0f, 0.5f, "probe"));
            REQUIRE(g.has_value());
            rd::Mesh probe = std::move(*g);
            probe.setProgram(*program);
            probe.setMaterialGroup(scene.shadows->receiverGroup());

            rd::Light spot = rd::spotLight({-4, 8, 0}, {0, -1, 0}, {1, 1, 1}, 1.0f, std::cos(glm::radians(30.0f)), 0.95f, 0.0f, 0.0f);
            spot.mobility = rd::LightMobility::kStatic;
            spot.shadowMode = rd::ShadowMode::kCached;
            scene.lights.addLight(spot);
            REQUIRE(scene.lights.upload().has_value());
            scene.shadows->setLightParams(0, {.maxResolution = 256, .spotNear = 0.5f, .spotRange = 20.0f});
            const rd::ShadowStats s = scene.frame(&probe);
            CHECK(s.views == 1);
            CHECK(scene.shadows->lightStatus(0) == rd::ShadowStatus::kRealtime);
            // Sous le carré : ombre ; dans le cône hors de l'ombre portée (x de -6.7 à -1.3) : éclairé ; hors du cône : 1.
            CHECK(scene.red(-4.0f, 0.0f) == 0);
            CHECK(scene.red(-4.0f, -3.5f) == 255);
            CHECK(scene.red(0.0f, 0.0f) == 255);
            CHECK(scene.red(8.0f, 8.0f) == 255);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("Spot shadow in mesh.frag, forward and forward+ (with its depth pre-pass) (REN-08, REN-04)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            REQUIRE(scene.shadows->addMeshCaster(scene.caster, {{-6, 2, -2}, {-2, 2, 2}}, rd::CasterMobility::kStatic).has_value());
            scene.lights.setAmbientLight({1, 1, 1}, 0.0f); // seul le projecteur éclaire : à l'ombre, noir
            rd::Light spot = rd::spotLight({-4, 8, 0}, {0, -1, 0}, {1, 1, 1}, 1.0f, std::cos(glm::radians(30.0f)), std::cos(glm::radians(25.0f)),
                                           0.0f, 0.0f);
            spot.shadowMode = rd::ShadowMode::kCached;
            scene.lights.addLight(spot);
            REQUIRE(scene.lights.upload().has_value());
            scene.shadows->setLightParams(0, {.maxResolution = 256, .spotNear = 0.5f, .spotRange = 20.0f});
            for (const bool forwardPlus : {false, true})
            {
                INFO("forward+ ", forwardPlus);
                if (forwardPlus)
                {
                    auto fp = rd::enableForwardPlus(*scene.renderer, *scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
                    if (!fp)
                    {
                        MESSAGE("forward+ unavailable: ", fp.error().message);
                        break;
                    }
                }
                auto program = scene.shadows->receiverProgramWith(forwardPlus ? rd::forwardPlusDefines() : std::vector<rd::Define>{});
                INFO((program ? std::string() : program.error().message));
                REQUIRE(program.has_value());
                scene.ground.setProgram(*program);
                scene.shadows->invalidate();
                scene.frame();
                if (forwardPlus)
                {
                    const auto order = scene.renderer->renderGraph().executionOrder();
                    CHECK(std::find(order.begin(), order.end(), "depth-prepass") != order.end());
                }
                CHECK(scene.shadows->lightStatus(0) == rd::ShadowStatus::kRealtime);
                CHECK(scene.red(-4.0f, 0.0f) == 0);    // sous le carré : ombre du projecteur
                CHECK(scene.red(-4.0f, -3.5f) > 60);   // dans le cône, hors de l'ombre portée
                CHECK(scene.red(8.0f, 8.0f) == 0);     // hors du cône
            }
            rd::disableForwardPlus(*scene.renderer);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("ShadowFeature: instanced ObjectSet casters, culled per cascade, one instanced draw per batch (REN-07, REN-28)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto instances = rd::InstanceRenderer::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            REQUIRE(instances.has_value());
            auto unit = rd::Mesh::create(*scene.gpu.device, quadData(-1, -1, 1, 1, 0.0f, 1.0f, "unit"));
            REQUIRE(unit.has_value());
            auto meshId = (*instances)->addMesh(*unit);
            REQUIRE(meshId.has_value());
            rd::ObjectSet objects;
            const auto add = [&](glm::vec3 at)
            {
                rd::ObjectDesc desc;
                desc.lodMeshes = {*meshId};
                desc.localBounds = {{-1, 0, -1}, {1, 0, 1}};
                desc.transform = glm::translate(glm::mat4(1.0f), at);
                REQUIRE(objects.add(desc).has_value());
            };
            add({-4, 2, 0});
            add({-4, 2, 5});
            add({500, 2, 0}); // hors de toutes les cascades
            scene.shadows->addObjectCasters(objects, **instances, rd::CasterMobility::kMovable);
            scene.sun(rd::LightMobility::kDynamic, rd::ShadowMode::kDynamic);
            const rd::ShadowStats s = scene.frame();
            CHECK(s.drawCalls == 2);        // un lot par cascade
            CHECK(s.casterInstances == 4);  // deux objets visibles par cascade ; le lointain est écarté
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 0.0f)), 26, 2));
            CHECK(near(static_cast<std::uint8_t>(scene.red(-4.0f, 5.0f)), 26, 2));
            CHECK(scene.red(-4.0f, -5.0f) > kLit);
            scene.gpu.expectNoValidationErrors();
            scene.renderer.reset(); // le plugin référence les objets et les maillages : détruit avant eux
        }
    }
}
