// REN-39 : ombres des objets transparents (TransparentShadowMode : aucune, pleine, masquée, tramée). Partie CPU (décision du shader,
// motif de Bayer, mode conseillé par BlendMode) sans GPU ; partie GPU sur chaque backend : ombre sous un caster transparent, proportion
// de texels d'ombre ~ opacité, invalidation du cache par une opacité animée, couches de validation à 0.
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/renderer/transparent_shadow.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }


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


using TS = rd::TransparentShadowMode;

// Part moyenne d'ombre sous le caster (pixels intérieurs), normalisée par les images entièrement éclairée et entièrement sombre.
double shadowShare(ShadowScene &scene, const std::vector<int> &lit, const std::vector<int> &dark)
{
    double sum = 0.0;
    int n = 0;
    for (int py = 7; py < 25; ++py)
    {
        for (int px = 7; px < 25; ++px)
        {
            const int r = rgba(scene.gpu.pixel(scene.out, static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py)))[0];
            const std::size_t i = static_cast<std::size_t>(py) * kSize + static_cast<std::size_t>(px);
            const double span = std::max(1, lit[i] - dark[i]);
            sum += std::clamp(1.0 - (r - dark[i]) / span, 0.0, 1.0);
            ++n;
        }
    }
    return sum / n;
}

std::vector<int> redImage(ShadowScene &scene)
{
    std::vector<int> v(kSize * kSize);
    for (std::uint32_t y = 0; y < kSize; ++y)
    {
        for (std::uint32_t x = 0; x < kSize; ++x)
        {
            v[y * kSize + x] = rgba(scene.gpu.pixel(scene.out, x, y))[0];
        }
    }
    return v;
}

} // namespace

TEST_SUITE("renderer/transparent-shadow")
{
    TEST_CASE("TransparentShadow: CPU decision, Bayer pattern, coverage and advised mode (REN-39)")
    {
        CHECK(rd::bayerThreshold4(0, 0) == doctest::Approx(0.5f / 16.0f));
        CHECK(rd::bayerThreshold4(4, 4) == rd::bayerThreshold4(0, 0)); // motif 4x4
        float total = 0.0f;
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                total += rd::bayerThreshold4(x, y);
            }
        }
        CHECK(total == doctest::Approx(8.0f)); // 16 seuils (0.5 .. 15.5) / 16
        CHECK(rd::transparentShadowCoverage({TS::kNone, 1.0f, 0.5f}) == 0.0f);
        CHECK(rd::transparentShadowCoverage({TS::kFull, 0.0f, 0.5f}) == 1.0f);
        CHECK(rd::transparentShadowCoverage({TS::kMasked, 0.5f, 0.5f}) == 1.0f);  // alpha >= seuil : gardé
        CHECK(rd::transparentShadowCoverage({TS::kMasked, 0.49f, 0.5f}) == 0.0f);
        CHECK(rd::transparentShadowCoverage({TS::kDithered, 0.0f, 0.5f}) == 0.0f);
        CHECK(rd::transparentShadowCoverage({TS::kDithered, 1.0f, 0.5f}) == 1.0f);
        for (int k = 0; k <= 16; ++k)
        {
            const float alpha = static_cast<float>(k) / 16.0f;
            // Quantification de 1/16 : la part vaut alpha au plus à 1/16 près.
            CHECK(std::abs(rd::transparentShadowCoverage({TS::kDithered, alpha, 0.5f}) - alpha) <= 1.0f / 16.0f + 1e-6f);
        }
        // Monotone en alpha et stable (aucune dépendance au temps : même texel, même décision).
        float previous = 0.0f;
        for (int k = 0; k <= 32; ++k)
        {
            const float c = rd::transparentShadowCoverage({TS::kDithered, static_cast<float>(k) / 32.0f, 0.5f});
            CHECK(c >= previous);
            previous = c;
        }
        CHECK(rd::defaultTransparentShadowMode(rd::BlendMode::kOpaque) == TS::kFull);
        CHECK(rd::defaultTransparentShadowMode(rd::BlendMode::kMask) == TS::kMasked);
        CHECK(rd::defaultTransparentShadowMode(rd::BlendMode::kBlend) == TS::kDithered);
        CHECK(rd::defaultTransparentShadowMode(rd::BlendMode::kAdditive) == TS::kNone);
        for (const TS m : {TS::kNone, TS::kFull, TS::kMasked, TS::kDithered})
        {
            auto back = rd::transparentShadowModeFromName(rd::transparentShadowModeName(m));
            REQUIRE(back.has_value());
            CHECK(*back == m);
        }
        CHECK_FALSE(rd::transparentShadowModeFromName("partial").has_value());
    }

    TEST_CASE("ShadowFeature: shadow under a transparent caster: none = lit, full = dark, masked = dark above the cutoff, dithered ~ opacity (REN-39)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto big = rd::Mesh::create(*scene.gpu.device, quadData(-7, -7, 7, 7, 2.0f, 1.0f, "glass"));
            REQUIRE(big.has_value());
            rd::Mesh glass = std::move(*big);
            auto id = scene.shadows->addMeshCaster(glass, {{-7, 2, -7}, {7, 2, 7}}, rd::CasterMobility::kStatic);
            REQUIRE(id.has_value());
            scene.sun(rd::LightMobility::kDynamic, rd::ShadowMode::kDynamic);
            const auto set = [&](TS mode, float alpha, float cutoff = 0.5f)
            {
                REQUIRE(scene.shadows->setCasterTransparency(*id, {mode, alpha, cutoff}).has_value());
                scene.frame();
            };

            set(TS::kNone, 1.0f);
            const std::vector<int> lit = redImage(scene);
            CHECK(scene.red(0.0f, 0.0f) > kLit); // aucune ombre : éclairé
            set(TS::kFull, 0.1f);                // pleine : l'opacité est ignorée
            const std::vector<int> dark = redImage(scene);
            CHECK(near(static_cast<std::uint8_t>(scene.red(0.0f, 0.0f)), 26, 2));
            CHECK(scene.red(9.0f, 0.0f) > kLit); // hors du caster : éclairé

            set(TS::kMasked, 0.6f, 0.5f); // au-dessus du seuil : sombre
            CHECK(near(static_cast<std::uint8_t>(scene.red(0.0f, 0.0f)), 26, 2));
            CHECK(shadowShare(scene, lit, dark) > 0.97);
            set(TS::kMasked, 0.4f, 0.5f); // sous le seuil : éclairé
            CHECK(scene.red(0.0f, 0.0f) > kLit);
            CHECK(shadowShare(scene, lit, dark) < 0.03);

            // Tramé : part d'ombre ~ opacité, tolérance 0.12 (quantification 1/16 de la trame + lissage PCF + bords).
            for (const float alpha : {0.25f, 0.5f, 0.75f})
            {
                set(TS::kDithered, alpha);
                const double share = shadowShare(scene, lit, dark);
                INFO("alpha = " << alpha << ", share = " << share);
                CHECK(std::abs(share - static_cast<double>(alpha)) < 0.12);
            }
            set(TS::kDithered, 1.0f);
            CHECK(shadowShare(scene, lit, dark) > 0.97);
            set(TS::kDithered, 0.0f);
            CHECK(shadowShare(scene, lit, dark) < 0.03);
            // Stable dans le temps : deux images identiques, même résultat au pixel près.
            set(TS::kDithered, 0.5f);
            const std::vector<int> first = redImage(scene);
            scene.frame();
            CHECK(redImage(scene) == first);
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("ShadowFeature cache: an animated opacity of a transparent caster invalidates the cache (REN-39)")
    {
        FOR_EACH_BACKEND(backend)
        {
            ShadowScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto big = rd::Mesh::create(*scene.gpu.device, quadData(-7, -7, 7, 7, 2.0f, 1.0f, "glass"));
            REQUIRE(big.has_value());
            rd::Mesh glass = std::move(*big);
            for (const rd::CasterMobility mobility : {rd::CasterMobility::kStatic, rd::CasterMobility::kMovable})
            {
                INFO("mobility = " << (mobility == rd::CasterMobility::kStatic ? "static" : "movable"));
                scene.shadows->invalidate();
                auto id = scene.shadows->addMeshCaster(glass, {{-7, 2, -7}, {7, 2, 7}}, mobility);
                REQUIRE(id.has_value());
                scene.sun(rd::LightMobility::kStationary, rd::ShadowMode::kCached);
                REQUIRE(scene.shadows->setCasterTransparency(*id, {TS::kMasked, 1.0f, 0.5f}).has_value());
                scene.frame();
                CHECK(near(static_cast<std::uint8_t>(scene.red(0.0f, 0.0f)), 26, 2));
                rd::ShadowStats s = scene.frame(); // rien ne bouge : rien n'est redessiné
                CHECK(s.viewsUpdated == 0);
                CHECK(s.drawCalls == 0);

                // L'opacité s'anime sous le seuil : l'ombre disparaît, la vue est redessinée (cache ou recopie).
                REQUIRE(scene.shadows->setCasterTransparency(*id, {TS::kMasked, 0.2f, 0.5f}).has_value());
                s = scene.frame();
                CHECK(s.viewsUpdated > 0);
                CHECK((mobility == rd::CasterMobility::kStatic ? s.staticRedraws : s.movableRedraws) > 0);
                CHECK(scene.red(0.0f, 0.0f) > kLit);
                s = scene.frame();
                CHECK(s.viewsUpdated == 0);

                // Retour au mode plein : ombre de nouveau, sans changer l'opacité.
                REQUIRE(scene.shadows->setCasterTransparency(*id, {TS::kFull, 0.2f, 0.5f}).has_value());
                scene.frame();
                CHECK(near(static_cast<std::uint8_t>(scene.red(0.0f, 0.0f)), 26, 2));
                // Changer l'opacité d'un caster pleinement opaque ne coûte rien (alpha ignoré).
                REQUIRE(scene.shadows->setCasterTransparency(*id, {TS::kFull, 0.9f, 0.5f}).has_value());
                s = scene.frame();
                CHECK(s.viewsUpdated == 0);
                // Aucune ombre : le caster n'est plus dessiné.
                REQUIRE(scene.shadows->setCasterTransparency(*id, {TS::kNone, 0.9f, 0.5f}).has_value());
                scene.frame();
                CHECK(scene.red(0.0f, 0.0f) > kLit);
                scene.shadows->removeCaster(*id);
            }
            CHECK_FALSE(scene.shadows->setCasterTransparency(9999, {}).has_value());
            scene.gpu.expectNoValidationErrors();
        }
    }
}
