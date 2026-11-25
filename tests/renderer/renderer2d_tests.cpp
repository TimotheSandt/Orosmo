// R2D-01 (lots de sprites), R2D-02 (caméra 2D, pixel parfait), R2D-03 (formes) : `Camera2D` (sans GPU) et `Renderer2D` sur OpenGL
// et Vulkan. Les images sont de 16 x 16 pixels, vue de 16 x 16 unités (zoom 1, centre (8, 8), Y du monde vers le haut).
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/batch_arena.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/renderer2d.hpp>

#include <cmath>
#include <limits>
#include <numbers>

namespace rd = orosmo::renderer;
namespace fs = std::filesystem;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

using Px = std::array<std::uint8_t, 4>;
constexpr Px kRed{255, 0, 0, 255};
constexpr Px kGreen{0, 255, 0, 255};
constexpr Px kBlue{0, 0, 255, 255};
constexpr Px kWhite{255, 255, 255, 255};
constexpr Px kBlack{0, 0, 0, 255};

struct Scene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::Renderer2D> r2d;
    TextureHandle target;
    std::uint32_t size;

    explicit Scene(Backend backend, std::uint32_t pixels = 16) : gpu(backend), size(pixels)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::Renderer2D::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        r2d = std::move(*created);
        target = gpu.target(Format::kRGBA8Unorm, {size, size}, "r2d");
    }
    ~Scene() { r2d.reset(); }

    rd::Camera2D camera() const
    {
        rd::Camera2D c;
        c.setViewport(size, size);
        c.setCenter({size * 0.5f, size * 0.5f});
        return c;
    }
    orosmo::core::Result<> render(bool load = false, glm::vec4 clear = {0, 0, 0, 1})
    {
        auto encoder = gpu->createCommandEncoder();
        auto r = r2d->render(*encoder, target, Format::kRGBA8Unorm, {size, size}, load, clear);
        auto submitted = gpu->submit(std::move(encoder));
        return r ? submitted : r;
    }
    Px px(int x, int yTop) { return rgba(gpu.pixel(target, x, yTop)); }
    // Texture 2x2 : ligne du haut rouge, vert ; ligne du bas bleu, blanc (première ligne envoyée = haut).
    rd::Texture quadrants(bool linear = false)
    {
        const std::uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        rd::TextureInfo info;
        info.width = info.height = 2;
        info.filter = linear ? rd::FilterMode::kLinearNoMipmaps : rd::FilterMode::kNearest;
        info.address = AddressMode::kClampToEdge;
        auto t = rd::Texture::create(*gpu.device, info, std::as_bytes(std::span(texels)));
        REQUIRE(t.has_value());
        return std::move(*t);
    }
    rd::Texture solid(std::array<std::uint8_t, 4> c)
    {
        rd::TextureInfo info;
        info.filter = rd::FilterMode::kNearest;
        auto t = rd::Texture::create(*gpu.device, info, std::as_bytes(std::span(c)));
        REQUIRE(t.has_value());
        return std::move(*t);
    }
};

} // namespace

TEST_SUITE("renderer/camera2d")
{
    TEST_CASE("R2D-02: world to screen, Y up in the world and down on the screen")
    {
        rd::Camera2D c;
        c.setViewport(200, 100);
        c.setCenter({10, 20});
        c.setZoom(4);
        CHECK(c.worldToScreen({10, 20}) == glm::vec2(100, 50));
        CHECK(c.worldToScreen({11, 20}).x == doctest::Approx(104));
        CHECK(c.worldToScreen({10, 21}).y == doctest::Approx(46)); // plus haut dans le monde = plus haut à l'écran
        const glm::vec2 back = c.screenToWorld(c.worldToScreen({7.25f, 23.5f}));
        CHECK(back.x == doctest::Approx(7.25f));
        CHECK(back.y == doctest::Approx(23.5f));
        const auto [lo, hi] = c.visibleBounds();
        CHECK(lo.x == doctest::Approx(-15));
        CHECK(hi.x == doctest::Approx(35));
        CHECK(lo.y == doctest::Approx(7.5f));
        CHECK(hi.y == doctest::Approx(32.5f));
    }

    TEST_CASE("R2D-02: a rotated camera turns the world around its center")
    {
        rd::Camera2D c;
        c.setViewport(100, 100);
        c.setZoom(1);
        c.setRotation(std::numbers::pi_v<float> * 0.5f); // la vue tourne de 90 degrés : l'axe X du monde pointe vers le bas de l'écran
        const glm::vec2 s = c.worldToScreen({10, 0});
        CHECK(s.x == doctest::Approx(50).epsilon(0.001));
        CHECK(s.y == doctest::Approx(60).epsilon(0.001)); // rotation de la vue anti-horaire : le monde tourne dans le sens horaire
        const glm::vec2 w = c.screenToWorld(s);
        CHECK(w.x == doctest::Approx(10).epsilon(0.001));
        CHECK(w.y == doctest::Approx(0).epsilon(0.001));
    }

    TEST_CASE("R2D-02: pixel perfect uses an integer scale, an aligned center and no rotation")
    {
        rd::Camera2D c;
        c.setViewport(101, 64);
        c.setZoom(3.7f);
        c.setCenter({10.3f, 5.1f});
        c.setRotation(1.0f);
        CHECK(c.effectiveZoom() == doctest::Approx(3.7f));
        c.setPixelPerfect(true);
        CHECK(c.effectiveZoom() == 3.0f);
        CHECK(c.effectiveRotation() == 0.0f);
        c.setZoom(0.4f);
        CHECK(c.effectiveZoom() == 1.0f); // jamais sous 1
        c.setZoom(3.7f);
        // Un point du monde sur la grille 1/3 tombe sur un bord de pixel : sa position à l'écran est entière.
        const glm::vec2 s = c.worldToScreen({2.0f, 4.0f});
        CHECK(s.x == doctest::Approx(std::round(s.x)).epsilon(1e-4));
        CHECK(s.y == doctest::Approx(std::round(s.y)).epsilon(1e-4));
        // Largeur impaire : le milieu de la vue est un demi-pixel, un bord de pixel reste entier.
        CHECK(std::fmod(c.effectiveCenter().x * 3.0f, 1.0f) == doctest::Approx(0.5f).epsilon(1e-3));
    }
}

TEST_SUITE("renderer/renderer2d")
{
    TEST_CASE("R2D-01: a sprite shows its texture the right way up; uv rectangles pick an atlas cell")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture atlas = s.quadrants();
            auto m = s.r2d->material(atlas, true); // plus proche voisin : texels nets
            REQUIRE(m.has_value());
            s.r2d->begin(s.camera());
            REQUIRE(s.r2d->draw({.material = *m, .position = {0, 0}, .size = {8, 8}, .pivot = {0, 0}}).has_value()); // en bas à gauche
            // Cellule du bas-droite de l'atlas (blanc) étirée sur 4 x 4 en haut à droite.
            REQUIRE(s.r2d->draw({.material = *m, .position = {12, 12}, .size = {4, 4}, .pivot = {0, 0}, .uvMin = {0.5f, 0.5f}, .uvMax = {1, 1}}).has_value());
            REQUIRE(s.render().has_value());
            // Le sprite du bas : rouge en haut à gauche de SON image, vert en haut à droite, bleu en bas à gauche, blanc en bas à droite.
            CHECK(s.px(2, 10) == kRed);
            CHECK(s.px(6, 10) == kGreen);
            CHECK(s.px(2, 14) == kBlue);
            CHECK(s.px(6, 14) == kWhite);
            CHECK(s.px(14, 2) == kWhite);
            CHECK(s.px(10, 10) == kBlack);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: sprites of one material are ONE draw call; the layer z decides the order, not the submission order")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture red = s.solid({255, 0, 0, 255});
            rd::Texture blue = s.solid({0, 0, 255, 255});
            auto mr = s.r2d->material(red);
            auto mb = s.r2d->material(blue);
            REQUIRE((mr && mb));
            s.r2d->begin(s.camera());
            for (int i = 0; i < 64; ++i)
            {
                REQUIRE(s.r2d->draw({.material = *mr, .position = {float(i % 8) * 2, float(i / 8) * 2}, .size = {2, 2}, .pivot = {0, 0}, .z = 0}).has_value());
            }
            REQUIRE(s.render().has_value());
            CHECK(s.r2d->stats().sprites == 64);
            CHECK(s.r2d->stats().drawCalls == 1);
            CHECK(s.px(15, 0) == kRed);
            CHECK(s.px(0, 15) == kRed);

            // Bleu déposé en premier mais z plus grand : devant ; alternance rouge/bleu à z égal : deux lots seulement.
            s.r2d->begin(s.camera());
            REQUIRE(s.r2d->draw({.material = *mb, .position = {0, 0}, .size = {16, 16}, .pivot = {0, 0}, .z = 1}).has_value());
            REQUIRE(s.r2d->draw({.material = *mr, .position = {0, 0}, .size = {8, 16}, .pivot = {0, 0}, .z = 0}).has_value());
            REQUIRE(s.render().has_value());
            CHECK(s.px(2, 8) == kBlue);
            CHECK(s.r2d->stats().drawCalls == 2);

            s.r2d->begin(s.camera());
            for (int i = 0; i < 8; ++i)
            {
                REQUIRE(s.r2d->draw({.material = (i % 2 ? *mb : *mr), .position = {float(i * 2), 0}, .size = {2, 2}, .pivot = {0, 0}, .z = 0}).has_value());
            }
            REQUIRE(s.render().has_value());
            CHECK(s.r2d->stats().drawCalls == 2); // tri par matériau à z égal : un lot par matériau
            CHECK(s.px(1, 15) == kRed);
            CHECK(s.px(3, 15) == kBlue);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: tint, alpha blending, rotation around the pivot, unknown material refused")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture white = s.solid({255, 255, 255, 255});
            auto m = s.r2d->material(white);
            REQUIRE(m.has_value());
            CHECK(!s.r2d->material(rd::Texture{}).has_value());
            s.r2d->begin(s.camera());
            CHECK(!s.r2d->draw({.material = {}, .size = {1, 1}}).has_value());
            CHECK(!s.r2d->draw({.material = {99}, .size = {1, 1}}).has_value());
            REQUIRE(s.r2d->draw({.material = *m, .position = {4, 4}, .size = {4, 4}, .pivot = {0, 0}, .color = {1, 0, 0, 0.5f}}).has_value());
            // Carré de 8 tourné de 45 degrés autour de son centre (8, 8) : couvre le centre, pas les coins de la boîte.
            REQUIRE(s.r2d->draw({.material = *m, .position = {12, 12}, .size = {8, 8}, .rotation = std::numbers::pi_v<float> * 0.25f, .color = {0, 1, 0, 1}, .z = 1}).has_value());
            REQUIRE(s.render().has_value());
            const Px half = s.px(5, 10); // rouge à moitié sur noir
            CHECK(near(half[0], 128, 2));
            CHECK(half[1] == 0);
            CHECK(s.px(12, 4) == kGreen);   // centre du carré tourné (12, 12) -> pixel (12, 4)
            CHECK(s.px(15, 0) == Px{0, 0, 0, 255}); // le coin de la boîte englobante est vide
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-02: pixel perfect: integer scale, nearest filtering, sprites snapped on pixel edges")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture linear = s.quadrants(true); // texture à filtrage linéaire : le mode pixel parfait force le plus proche
            auto m = s.r2d->material(linear);
            REQUIRE(m.has_value());
            rd::Camera2D cam;
            cam.setViewport(16, 16);
            cam.setCenter({0, 0});
            cam.setZoom(3.9f); // échelle 3
            cam.setPixelPerfect(true);
            s.r2d->begin(cam);
            // Sprite de 2 x 2 unités = 6 x 6 pixels ; position fractionnaire (alignée sur la grille par le dessinateur).
            REQUIRE(s.r2d->draw({.material = *m, .position = {-0.9f, 0.9f}, .size = {1.97f, 2.03f}, .pivot = {0, 1}}).has_value());
            REQUIRE(s.render().has_value());
            // Origine de la vue au pixel (8, 8) ; le sprite occupe x de 8 + round(-0.9*3) = 5 à 11, y du haut de 8 - 3 = 5 à 11.
            CHECK(s.px(5, 5) == kRed);
            CHECK(s.px(7, 7) == kRed);   // 3 pixels de rouge : 5, 6, 7 (aucun mélange avec le vert)
            CHECK(s.px(8, 5) == kGreen);
            CHECK(s.px(10, 7) == kGreen);
            CHECK(s.px(5, 8) == kBlue);
            CHECK(s.px(10, 10) == kWhite);
            CHECK(s.px(4, 5) == kBlack);
            CHECK(s.px(11, 5) == kBlack);
            CHECK(s.px(5, 11) == kBlack);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-03: rectangles, outlines, lines, circles and rings")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend, 32);
            REQUIRE(s.gpu.device);
            s.r2d->begin(s.camera());
            s.r2d->rect({1, 1}, {6, 4}, {1, 0, 0, 1});
            s.r2d->rectOutline({10, 1}, {8, 8}, 2, {0, 1, 0, 1});
            s.r2d->line({0, 20}, {12, 20}, 2, {0, 0, 1, 1});
            s.r2d->circle({24, 24}, 6, {1, 1, 1, 1});
            s.r2d->circleOutline({8, 28}, 3, 1, {1, 1, 0, 1});
            REQUIRE(s.render().has_value());
            CHECK(s.r2d->stats().shapes == 5);
            CHECK(s.r2d->stats().drawCalls == 1); // toutes les formes : un lot
            CHECK(s.px(3, 30) == kRed);
            CHECK(s.px(7, 30) == kBlack);                // hors du rectangle
            CHECK(s.px(11, 30) == kGreen);               // bande gauche du contour
            CHECK(s.px(14, 26) == kBlack);               // intérieur du contour vide : y monde 5 -> ligne 27, centre
            CHECK(s.px(6, 11) == kBlue);                 // ligne y = 20 épaisse de 2 : lignes du haut 11 et 12
            CHECK(s.px(6, 8) == kBlack);
            CHECK(s.px(24, 8) == kWhite);                // centre du disque : y monde 24 -> ligne 8
            CHECK(s.px(24, 1) == kBlack);                // au-dessus du disque (rayon 6)
            CHECK(s.px(5, 4) == Px{255, 255, 0, 255});   // anneau (rayon 3, épaisseur 2) : côté gauche, x de 4 à 6
            CHECK(s.px(8, 4) == kBlack);                 // centre de l'anneau vide
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-03: polygons, convex or concave, either winding; degenerate polygons are ignored")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            // Forme en L (concave), sens trigonométrique : le coin (8..16, 8..16) manque.
            const std::array<glm::vec2, 6> ell{{{0, 0}, {16, 0}, {16, 8}, {8, 8}, {8, 16}, {0, 16}}};
            s.r2d->begin(s.camera());
            s.r2d->polygon(ell, {1, 0, 0, 1});
            REQUIRE(s.render().has_value());
            CHECK(s.px(2, 2) == kRed);   // monde (2, 14)
            CHECK(s.px(2, 13) == kRed);  // monde (2, 3)
            CHECK(s.px(13, 13) == kRed); // monde (13, 3)
            CHECK(s.px(13, 2) == kBlack); // le creux du L : monde (13, 14)
            CHECK(s.px(10, 5) == kBlack);

            // Même polygone dans l'autre sens, plus un triangle dégénéré (points alignés) et deux points : rien de plus.
            std::array<glm::vec2, 6> reversed = ell;
            std::ranges::reverse(reversed);
            const std::array<glm::vec2, 3> flat{{{0, 0}, {4, 4}, {8, 8}}};
            const std::array<glm::vec2, 2> two{{{0, 0}, {4, 4}}};
            s.r2d->begin(s.camera());
            s.r2d->polygon(reversed, {0, 1, 0, 1});
            s.r2d->polygon(flat, {1, 1, 1, 1});
            s.r2d->polygon(two, {1, 1, 1, 1});
            CHECK(true);
            REQUIRE(s.render().has_value());
            CHECK(s.r2d->stats().shapes == 1);
            CHECK(s.px(2, 13) == kGreen);
            CHECK(s.px(13, 2) == kBlack);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-03: shapes and sprites share the z order; a polyline closes its corners")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture blue = s.solid({0, 0, 255, 255});
            auto m = s.r2d->material(blue);
            REQUIRE(m.has_value());
            s.r2d->begin(s.camera());
            s.r2d->rect({0, 0}, {16, 16}, {1, 0, 0, 1}, 0.0f, 2.0f); // devant
            REQUIRE(s.r2d->draw({.material = *m, .position = {0, 0}, .size = {16, 16}, .pivot = {0, 0}, .z = 1}).has_value());
            s.r2d->rect({4, 4}, {4, 4}, {0, 1, 0, 1}, 0.0f, 0.0f); // derrière
            REQUIRE(s.render().has_value());
            CHECK(s.px(6, 10) == kRed);
            CHECK(s.r2d->stats().drawCalls == 3);

            const std::array<glm::vec2, 3> corner{{{2, 2}, {2, 14}, {14, 14}}};
            s.r2d->begin(s.camera());
            s.r2d->polyline(corner, 4, {1, 1, 1, 1});
            REQUIRE(s.render().has_value());
            CHECK(s.px(1, 1) == kWhite); // le coude (2, 14) -> pixel (2, 2) : extérieur arrondi couvert
            CHECK(s.px(8, 1) == kWhite);
            CHECK(s.px(8, 8) == kBlack);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: two render() calls on one encoder before submit keep the vertices of both")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            auto encoder = s.gpu->createCommandEncoder();
            s.r2d->begin(s.camera());
            s.r2d->rect({0, 0}, {8, 16}, {1, 0, 0, 1});
            REQUIRE(s.r2d->render(*encoder, s.target, Format::kRGBA8Unorm, {16, 16}, false, {0, 0, 0, 1}).has_value());
            s.r2d->begin(s.camera());
            s.r2d->rect({8, 0}, {8, 16}, {0, 0, 1, 1});
            REQUIRE(s.r2d->render(*encoder, s.target, Format::kRGBA8Unorm, {16, 16}, true, {0, 0, 0, 1}).has_value());
            REQUIRE(s.gpu->submit(std::move(encoder)).has_value());
            CHECK(s.px(3, 8) == kRed);
            CHECK(s.px(12, 8) == kBlue);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: the batch arena starts over with each new encoder, even one allocated at the address of the previous one")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            rd::BatchArena arena;
            const std::vector<std::byte> data(1000, std::byte{7});
            std::uint64_t firstCapacity = 0;
            std::uint64_t previousId = 0;
            for (int frame = 0; frame < 300; ++frame) // 300 images x 3 envois de 1 Ko : 900 Ko sans remise à zéro
            {
                auto encoder = gpu->createCommandEncoder();
                CHECK(encoder->id() != previousId); // identifiant jamais réutilisé, même si l'adresse l'est
                previousId = encoder->id();
                for (int call = 0; call < 3; ++call)
                {
                    auto range = arena.upload(*gpu.device, encoder->id(), data, "test.arena");
                    REQUIRE(range.has_value());
                    CHECK(range->offset == static_cast<std::uint64_t>(call) * 1024U); // plages distinctes dans un encodeur
                }
                REQUIRE(gpu->submit(std::move(encoder)).has_value());
                if (frame == 0)
                    firstCapacity = arena.capacity();
            }
            CHECK(arena.capacity() == firstCapacity); // le tampon ne grandit pas d'image en image
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-03: a translucent polyline blends each pixel once: collinear points and corners leave no overlap")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            const std::array<glm::vec2, 3> aligned{{{1, 8}, {8, 8}, {15, 8}}};
            s.r2d->begin(s.camera());
            s.r2d->polyline(aligned, 4, {1, 0, 0, 0.5f});
            REQUIRE(s.render().has_value());
            const Px side = s.px(4, 8);
            CHECK(near(side[0], 128, 2));
            CHECK(s.px(8, 8) == side); // au point du milieu : pas de disque superposé
            CHECK(s.px(8, 7) == side);

            const std::array<glm::vec2, 3> corner{{{2, 2}, {2, 14}, {14, 14}}};
            s.r2d->begin(s.camera());
            s.r2d->polyline(corner, 4, {1, 0, 0, 0.5f});
            REQUIRE(s.render().has_value());
            const Px arm = s.px(2, 8);
            CHECK(near(arm[0], 128, 2));
            CHECK(s.px(3, 3) == arm);  // intérieur du coude (2, 14) -> pixel (2, 2) : pas de double mélange
            CHECK(s.px(1, 1) == arm);  // extérieur arrondi
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: a material cleared after a deposit refuses the render cleanly; NaN z is ordered")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            rd::Texture white = s.solid({255, 255, 255, 255});
            auto m = s.r2d->material(white);
            REQUIRE(m.has_value());
            s.r2d->begin(s.camera());
            REQUIRE(s.r2d->draw({.material = *m, .size = {4, 4}}).has_value());
            s.r2d->clearMaterials();
            auto r = s.render();
            CHECK_FALSE(r.has_value());
            CHECK((r.error().code == orosmo::core::ErrorCode::kInvalidArgument));

            s.r2d->begin(s.camera());
            const float nan = std::numeric_limits<float>::quiet_NaN();
            s.r2d->rect({0, 0}, {8, 16}, {1, 0, 0, 1}, 0.0f, nan);
            s.r2d->rect({8, 0}, {8, 16}, {0, 0, 1, 1}, 0.0f, 1.0f);
            s.r2d->rect({0, 0}, {4, 4}, {0, 1, 0, 1}, 0.0f, 0.0f);
            REQUIRE(s.render().has_value());
            CHECK(s.px(12, 8) == kBlue);
            s.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-03: layering contract: a CanvasRenderer list (UI, text) drawn with loadExisting after render() keeps the 2D picture")
    {
        FOR_EACH_BACKEND(backend)
        {
            Scene s(backend);
            REQUIRE(s.gpu.device);
            auto canvas = rd::CanvasRenderer::create(*s.gpu.device, *s.library, *s.pipelines, shaderDir());
            REQUIRE(canvas.has_value());
            s.r2d->begin(s.camera());
            s.r2d->rect({0, 0}, {16, 16}, {1, 0, 0, 1});
            REQUIRE(s.render().has_value());
            rd::DrawList list;
            list.logicalSize = {16, 16};
            list.rect({4, 4}, {4, 4}, {0, 0, 1, 1}); // écran : origine en haut ; la liste du texte ou de l'interface s'ajoute ainsi
            auto encoder = s.gpu->createCommandEncoder();
            REQUIRE((*canvas)->render(*encoder, list, {s.target, Format::kRGBA8Unorm, {16, 16}}, true).has_value());
            REQUIRE(s.gpu->submit(std::move(encoder)).has_value());
            CHECK(s.px(5, 5) == kBlue);
            CHECK(s.px(10, 10) == kRed);
            s.gpu.expectNoValidationErrors();
        }
    }
}
