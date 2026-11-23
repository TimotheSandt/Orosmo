// UI-02 (lot B5) : exécution d'une liste de dessin 2D sur le RHI : rectangles, mélange alpha, découpages, glyphes, couches hors écran.
// Repère du RHI (A26) : première ligne = haut, comme les listes de dessin (une position y logique petite = haut).
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/canvas_renderer.hpp>

#include <cstdlib>
#include <limits>

namespace rd = orosmo::renderer;
namespace fs = std::filesystem;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

struct Canvas
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::CanvasRenderer> canvas;
    TextureHandle target;

    explicit Canvas(Backend backend, std::uint32_t size = 16) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::CanvasRenderer::create(*gpu.device, *library, *pipelines, shaderDir());
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        canvas = std::move(*created);
        target = gpu.target(Format::kRGBA8Unorm, {size, size}, "canvas");
        // Le contenu initial d'une texture est indéfini : on l'efface en vert opaque avant les tests qui conservent le contenu.
        gpu.run([&](CommandEncoder &e) { e.beginRenderPass(passOn(target, {0, 1, 0, 1})); e.endRenderPass(); });
    }
    ~Canvas() { canvas.reset(); }

    rd::CanvasTarget canvasTarget(std::uint32_t size = 16) const { return {target, Format::kRGBA8Unorm, {size, size}}; }

    orosmo::core::Result<> render(const rd::DrawList &list, bool load = true)
    {
        auto encoder = gpu->createCommandEncoder();
        auto r = canvas->render(*encoder, list, canvasTarget(), load);
        auto submitted = gpu->submit(std::move(encoder));
        return r ? submitted : r;
    }
    // Texel en coordonnées de l'interface (origine en haut) : depuis A26, c'est aussi le repère du RHI (ligne 0 = haut).
    std::array<std::uint8_t, 4> px(int x, int yTop, std::uint32_t size = 16)
    {
        (void)size;
        return rgba(gpu.pixel(target, x, yTop));
    }
};

rd::DrawList listOf(float size = 16.0f)
{
    rd::DrawList l;
    l.logicalSize = {size, size};
    return l;
}

using Px = std::array<std::uint8_t, 4>;

// Atlas 2x2 de couverture pleine et série de glyphes : un carré [x0, x1] x [y0, y1] de la couleur donnée.
std::shared_ptr<rd::AtlasImage> solidAtlas()
{
    auto atlas = std::make_shared<rd::AtlasImage>();
    atlas->id = rd::newAtlasId();
    atlas->width = 2;
    atlas->height = 2;
    atlas->rgba.assign(16, 255);
    return atlas;
}
rd::DrawGlyphs squareRun(const std::shared_ptr<rd::AtlasImage> &atlas, float x0, float y0, float x1, float y1, const glm::vec3 &color)
{
    rd::DrawGlyphs run;
    run.atlas = atlas;
    run.color = color;
    run.vertices = {x0, y0, 0, 0, x0, y1, 0, 1, x1, y1, 1, 1, x0, y0, 0, 0, x1, y1, 1, 1, x1, y0, 1, 0};
    return run;
}

} // namespace

TEST_SUITE("renderer/canvas")
{
    TEST_CASE("DrawList records commands in order and counts them")
    {
        rd::DrawList l = listOf();
        l.rect({0, 0}, {1, 1}, {1, 0, 0, 1});
        l.pushClip({0, 0}, {4, 4});
        l.beginLayer(1, {4, 4});
        l.clearRect({0, 0}, {2, 2});
        l.endLayer();
        l.popClip();
        CHECK(l.size() == 6);
        CHECK(l.count<rd::DrawRect>() == 1);
        CHECK(l.count<rd::PushClip>() == 1);
        CHECK(l.count<rd::DrawClearRect>() == 1);
        CHECK(std::holds_alternative<rd::DrawRect>(l.commands.front()));
        l.clear();
        CHECK(l.empty());
    }

    TEST_CASE("rectangles: top-left origin, alpha blending over the existing content, loadExisting=false clears to transparent")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.rect({0, 0}, {4, 4}, {1, 0, 0, 1});     // coin haut gauche, rouge opaque
            l.rect({8, 8}, {4, 4}, {1, 0, 0, 0.5f});  // rouge à moitié transparent sur le vert
            REQUIRE(c.render(l).has_value());
            CHECK(c.px(1, 1) == Px{255, 0, 0, 255});
            CHECK(c.px(1, 14) == Px{0, 255, 0, 255}); // le bas gauche n'est pas touché
            const Px blended = c.px(9, 9);
            CHECK(near(blended[0], 128));
            CHECK(near(blended[1], 128));
            CHECK(blended[2] == 0);
            CHECK(blended[3] == 255);
            REQUIRE(c.render(l, false).has_value());
            CHECK(c.px(1, 14) == Px{0, 0, 0, 0}); // effacé en transparent
            CHECK(c.px(1, 1) == Px{255, 0, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("two render() calls on one encoder before submit keep the instances of both")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList first = listOf();
            first.rect({0, 0}, {8, 16}, {1, 0, 0, 1});
            rd::DrawList second = listOf();
            second.rect({8, 0}, {8, 16}, {0, 0, 1, 1});
            auto encoder = c.gpu->createCommandEncoder();
            REQUIRE(c.canvas->render(*encoder, first, c.canvasTarget(), false).has_value());
            REQUIRE(c.canvas->render(*encoder, second, c.canvasTarget(), true).has_value());
            REQUIRE(c.gpu->submit(std::move(encoder)).has_value());
            CHECK(c.px(3, 8) == Px{255, 0, 0, 255});
            CHECK(c.px(12, 8) == Px{0, 0, 255, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("UI-07: rounded corners, inner border and soft edge (shadow); a plain rectangle stays exact")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend, 32);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf(32.0f);
            // Rectangle 16x16 en (0,0), coins de rayon 5, bordure bleue de 2 px, intérieur rouge.
            l.rect(rd::DrawRect{.position = {0, 0}, .size = {16, 16}, .color = {1, 0, 0, 1}, .radius = 5.0f, .borderWidth = 2.0f, .borderColor = {0, 0, 1, 1}});
            // Bord adouci sur 8 px (ombre) en (16,16) : plein au centre, transparent au bord.
            l.rect(rd::DrawRect{.position = {16, 16}, .size = {16, 16}, .color = {0, 0, 0, 1}, .softness = 8.0f});
            auto encoder = c.gpu->createCommandEncoder();
            REQUIRE(c.canvas->render(*encoder, l, c.canvasTarget(32), false).has_value());
            REQUIRE(c.gpu->submit(std::move(encoder)).has_value());
            CHECK(c.px(0, 0, 32) == Px{0, 0, 0, 0});       // coin arrondi : hors de la forme
            CHECK(c.px(8, 8, 32) == Px{255, 0, 0, 255});   // centre : intérieur
            CHECK(c.px(8, 0, 32) == Px{0, 0, 255, 255});   // haut, au milieu : bordure
            CHECK(c.px(0, 8, 32) == Px{0, 0, 255, 255});   // gauche, au milieu : bordure
            CHECK(c.px(24, 24, 32)[3] > 245);              // centre de l'ombre : opaque (à l'arrondi de smoothstep près)
            CHECK(c.px(16, 24, 32)[3] < 40);               // bord de l'ombre : presque transparent
            const auto mid = c.px(20, 24, 32)[3];          // à mi-chemin : partiel
            CHECK(mid > 40);
            CHECK(mid < 255);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("UI-07: a border over a transparent interior blends in premultiplied space (white stays white at the transition)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend, 32);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf(32.0f);
            // Intérieur transparent, bordure blanche opaque de 1,5 px : le pixel x = 1 est à moitié dans la bordure.
            l.rect(rd::DrawRect{.position = {0, 0}, .size = {16, 16}, .color = {0, 0, 0, 0}, .borderWidth = 1.5f, .borderColor = {1, 1, 1, 1}});
            auto encoder = c.gpu->createCommandEncoder();
            REQUIRE(c.canvas->render(*encoder, l, c.canvasTarget(32), false).has_value());
            REQUIRE(c.gpu->submit(std::move(encoder)).has_value());
            const Px edge = c.px(1, 8, 32);
            INFO("pixel de transition : ", int(edge[0]), ",", int(edge[1]), ",", int(edge[2]), ",", int(edge[3]));
            CHECK(edge[3] > 100); // à moitié couvert
            CHECK(edge[3] < 160);
            // Blanc à moitié couvert, posé sur du transparent : rouge = alpha (avant : la moitié, la couleur mélangée au noir
            // transparent était multipliée une seconde fois par l'alpha).
            CHECK(std::abs(int(edge[0]) - int(edge[3])) <= 8);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("UI-07: images are sampled and multiplied by the tint; one command holds several quads")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend, 16);
            REQUIRE(c.gpu.device);
            auto image = std::make_shared<rd::AtlasImage>();
            image->id = rd::newAtlasId();
            image->width = 2;
            image->height = 1;
            image->rgba = {255, 0, 0, 255, 0, 0, 255, 255}; // rouge | bleu
            rd::DrawImage cmd;
            cmd.image = image;
            cmd.tint = {1, 1, 1, 1};
            // Deux quadrilatères : la moitié gauche de l'image (rouge) en (0,0), la moitié droite (bleu) en (8,8), au centre des texels.
            const float q[] = {0, 0, 0.25f, 0.5f, 0, 8, 0.25f, 0.5f, 8, 8, 0.25f, 0.5f, 0, 0, 0.25f, 0.5f, 8, 8, 0.25f, 0.5f, 8, 0, 0.25f, 0.5f,
                               8, 8, 0.75f, 0.5f, 8, 16, 0.75f, 0.5f, 16, 16, 0.75f, 0.5f, 8, 8, 0.75f, 0.5f, 16, 16, 0.75f, 0.5f, 16, 8, 0.75f, 0.5f};
            cmd.vertices.assign(std::begin(q), std::end(q));
            rd::DrawList l = listOf();
            l.image(cmd);
            cmd.tint = {0, 1, 0, 1}; // teinte verte sur le rouge : noir
            cmd.vertices.resize(24);
            for (std::size_t i = 0; i < 24; i += 4)
                cmd.vertices[i + 1] += 8.0f; // en (0, 8)
            l.image(cmd);
            REQUIRE(c.render(l, false).has_value());
            CHECK(c.px(3, 3) == Px{255, 0, 0, 255});
            CHECK(c.px(12, 12) == Px{0, 0, 255, 255});
            CHECK(c.px(3, 12) == Px{0, 0, 0, 255});
            CHECK(c.px(12, 3) == Px{0, 0, 0, 0});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("the logical size is independent of the target size (HiDPI, render scale)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend, 32);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf(16.0f); // 16x16 logiques sur 32x32 pixels
            l.rect({0, 0}, {8, 8}, {1, 0, 0, 1});
            auto encoder = c.gpu->createCommandEncoder();
            REQUIRE(c.canvas->render(*encoder, l, c.canvasTarget(32), false).has_value());
            REQUIRE(c.gpu->submit(std::move(encoder)).has_value());
            CHECK(c.px(15, 15, 32) == Px{255, 0, 0, 255}); // 8 logiques = 16 pixels
            CHECK(c.px(17, 17, 32) == Px{0, 0, 0, 0});
        }
    }

    TEST_CASE("clip rectangles intersect and an empty clip draws nothing")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.pushClip({0, 0}, {8, 16});
            l.pushClip({4, 0}, {12, 8}); // intersection : x de 4 à 8, y de 0 à 8
            l.rect({0, 0}, {16, 16}, {1, 0, 0, 1});
            l.popClip();
            l.rect({0, 12}, {16, 4}, {0, 0, 1, 1}); // sous le premier clip seul : moitié gauche
            l.popClip();
            l.pushClip({20, 20}, {4, 4}); // hors surface : vide
            l.rect({0, 0}, {16, 16}, {1, 1, 1, 1});
            l.popClip();
            REQUIRE(c.render(l).has_value());
            CHECK(c.px(5, 3) == Px{255, 0, 0, 255});
            CHECK(c.px(2, 3) == Px{0, 255, 0, 255});  // hors du second clip
            CHECK(c.px(9, 3) == Px{0, 255, 0, 255});  // hors du premier clip
            CHECK(c.px(5, 12) == Px{0, 0, 255, 255});
            CHECK(c.px(12, 12) == Px{0, 255, 0, 255}); // le bleu s'arrête au clip de gauche
            CHECK(c.px(1, 1) == Px{0, 255, 0, 255});  // le clip vide a tout rejeté
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("glyph runs sample the atlas; atlases are cached by id")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            auto atlas = std::make_shared<rd::AtlasImage>();
            atlas->id = 11;
            atlas->width = 2;
            atlas->height = 2;
            atlas->rgba.assign(16, 255); // distance maximale partout : alpha 1
            rd::DrawGlyphs run;
            run.atlas = atlas;
            run.color = {0.0f, 0.0f, 1.0f};
            // un quad (x, y, u, v) de 6 sommets : pixels logiques (4, 4) à (12, 12)
            run.vertices = {4, 4, 0, 0, 4, 12, 0, 1, 12, 12, 1, 1, 4, 4, 0, 0, 12, 12, 1, 1, 12, 4, 1, 0};
            rd::DrawList l = listOf();
            l.glyphs(run);
            REQUIRE(c.render(l).has_value());
            CHECK(c.px(8, 8) == Px{0, 0, 255, 255});
            CHECK(c.px(2, 2) == Px{0, 255, 0, 255});
            CHECK(c.canvas->atlasCount() == 1);
            REQUIRE(c.render(l).has_value());
            CHECK(c.canvas->atlasCount() == 1); // même id : texture réutilisée
            auto other = std::make_shared<rd::AtlasImage>(*atlas);
            other->id = 12;
            run.atlas = other;
            rd::DrawList l2 = listOf();
            l2.glyphs(run);
            l2.glyphs(run); // deux séries : décalages distincts dans le tampon
            REQUIRE(c.render(l2).has_value());
            CHECK(c.canvas->atlasCount() == 2);
            auto empty = std::make_shared<rd::AtlasImage>(); // atlas invalide : taille nulle
            empty->id = 13;
            run.atlas = empty;
            rd::DrawList bad = listOf();
            bad.glyphs(run);
            CHECK((tests::errorOf(c.render(bad)) == ErrorCode::kInvalidArgument));
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("glyph runs keep their alpha up to the shader; without alpha they stay opaque")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            auto atlas = std::make_shared<rd::AtlasImage>();
            atlas->id = rd::newAtlasId();
            atlas->width = 2;
            atlas->height = 2;
            atlas->rgba.assign(16, 255); // couverture 1 partout
            rd::DrawGlyphs run;
            run.atlas = atlas;
            run.color = {0.0f, 0.0f, 1.0f};
            CHECK(run.alpha == 1.0f); // défaut : une couleur RGB seule est opaque
            run.vertices = {4, 4, 0, 0, 4, 12, 0, 1, 12, 12, 1, 1, 4, 4, 0, 0, 12, 12, 1, 1, 12, 4, 1, 0};
            run.alpha = 0.5f;
            rd::DrawList l = listOf();
            l.glyphs(run);
            REQUIRE(c.render(l).has_value());
            const Px half = c.px(8, 8); // bleu à moitié sur le vert : (0, ~128, ~128) ; avant, le bleu était opaque
            CHECK(half[0] == 0);
            CHECK(std::abs(int(half[1]) - 128) <= 2);
            CHECK(std::abs(int(half[2]) - 128) <= 2);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: N rectangles of the same style are one draw call; stats report both counts")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            for (int i = 0; i < 8; ++i)
            {
                l.rect({float(i * 2), 0}, {2, 2}, {1, 0, 0, 1});
            }
            l.rect(rd::DrawRect{.position = {0, 4}, .size = {16, 2}, .color = {0, 0, 1, 1}, .radius = 0.5f}); // couleur et forme différentes : même lot
            REQUIRE(c.render(l).has_value());
            CHECK(c.canvas->stats().drawCommands == 9);
            CHECK(c.canvas->stats().batchedCommands == 9);
            CHECK(c.canvas->stats().drawCalls == 1);
            CHECK(c.px(15, 1) == Px{255, 0, 0, 255});
            CHECK(c.px(15, 5) == Px{0, 0, 255, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: a clip change, an atlas change or another kind starts a new batch; the same atlas with other colors does not")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList clip = listOf();
            clip.rect({0, 0}, {2, 2}, {1, 0, 0, 1});
            clip.pushClip({0, 0}, {8, 8});
            clip.rect({2, 0}, {2, 2}, {1, 0, 0, 1});
            clip.popClip();
            clip.rect({4, 0}, {2, 2}, {1, 0, 0, 1});
            REQUIRE(c.render(clip).has_value());
            CHECK(c.canvas->stats().drawCalls == 3);

            auto a = solidAtlas();
            auto b = solidAtlas();
            rd::DrawList same = listOf();
            same.glyphs(squareRun(a, 0, 8, 4, 12, {1, 0, 0}));
            same.glyphs(squareRun(a, 8, 8, 12, 12, {0, 0, 1})); // même atlas, autre couleur : un seul dessin
            REQUIRE(c.render(same).has_value());
            CHECK(c.canvas->stats().drawCalls == 1);
            CHECK(c.px(2, 10) == Px{255, 0, 0, 255});
            CHECK(c.px(10, 10) == Px{0, 0, 255, 255});

            rd::DrawList other = listOf();
            other.glyphs(squareRun(a, 0, 8, 4, 12, {1, 0, 0}));
            other.glyphs(squareRun(b, 8, 8, 12, 12, {0, 0, 1})); // autre atlas : nouveau lot
            REQUIRE(c.render(other, false).has_value()); // cible remise à zéro : aucun pixel résiduel ne masque une erreur
            CHECK(c.canvas->stats().drawCalls == 2);
            CHECK(c.px(2, 10) == Px{255, 0, 0, 255});
            CHECK(c.px(10, 10) == Px{0, 0, 255, 255});
            CHECK(c.px(14, 2) == Px{0, 0, 0, 0});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("R2D-01: draw order is kept when batches overlap; disjoint draws may be merged without changing the picture")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            auto atlas = solidAtlas();
            // rect rouge, glyphes bleus par-dessus, rect vert par-dessus les glyphes ET le rouge : on ne peut pas fusionner les rects.
            rd::DrawList l = listOf();
            l.rect({0, 0}, {8, 8}, {1, 0, 0, 1});
            l.glyphs(squareRun(atlas, 4, 4, 12, 12, {0, 0, 1}));
            l.rect({6, 6}, {8, 8}, {0, 1, 0, 1});
            REQUIRE(c.render(l, false).has_value());
            CHECK(c.canvas->stats().drawCalls == 3);
            CHECK(c.px(2, 2) == Px{255, 0, 0, 255});
            CHECK(c.px(5, 5) == Px{0, 0, 255, 255}); // le bleu est au-dessus du rouge
            CHECK(c.px(7, 7) == Px{0, 255, 0, 255}); // le vert est au-dessus du bleu et du rouge
            CHECK(c.px(13, 13) == Px{0, 255, 0, 255});

            // Le deuxième rectangle ne touche ni le premier ni les glyphes : il rejoint le premier lot (2 dessins, même image).
            rd::DrawList apart = listOf();
            apart.rect({0, 0}, {2, 2}, {1, 0, 0, 1});
            apart.glyphs(squareRun(atlas, 4, 4, 8, 8, {0, 0, 1}));
            apart.rect({12, 12}, {2, 2}, {0, 1, 0, 1});
            REQUIRE(c.render(apart, false).has_value());
            CHECK(c.canvas->stats().drawCalls == 2);
            CHECK(c.px(14, 2) == Px{0, 0, 0, 0});
            CHECK(c.px(1, 1) == Px{255, 0, 0, 255});
            CHECK(c.px(6, 6) == Px{0, 0, 255, 255});
            CHECK(c.px(13, 13) == Px{0, 255, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("layers: drawn off screen, shown with scrolling, kept between lists, cleared in part, released when unused")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.beginLayer(7, {8, 8});
            l.rect({0, 0}, {4, 8}, {0, 1, 0, 1});  // moitié gauche verte
            l.rect({4, 0}, {4, 8}, {1, 0, 0, 1});  // moitié droite rouge
            l.endLayer();
            rd::DrawLayerImage image;
            image.layerId = 7;
            image.position = {0, 0};
            image.size = {8, 8};
            image.contentSize = {8, 8};
            l.layerImage(image);
            image.position = {8, 8};
            image.size = {4, 8};
            image.scrollOffset = {4, 0}; // ne montre que la moitié droite du contenu
            l.layerImage(image);
            REQUIRE(c.render(l, false).has_value());
            CHECK(c.canvas->layerCount() == 1);
            CHECK(c.px(1, 1) == Px{0, 255, 0, 255});  // contenu complet : gauche verte
            CHECK(c.px(6, 1) == Px{255, 0, 0, 255});  // ... droite rouge
            CHECK(c.px(9, 9) == Px{255, 0, 0, 255});  // défilement : on ne voit que le rouge
            CHECK(c.px(1, 12) == Px{0, 0, 0, 0});     // rien en dehors

            // Liste suivante sans BeginLayer : la couche est conservée et réaffichée.
            rd::DrawList again = listOf();
            again.layerImage({.layerId = 7, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(again, false).has_value());
            CHECK(c.px(1, 1) == Px{0, 255, 0, 255});

            // Mise à jour partielle : `clear = false` garde le contenu, clearRect efface une zone.
            rd::DrawList partial = listOf();
            partial.beginLayer(7, {8, 8}, false);
            partial.clearRect({0, 0}, {4, 4});
            partial.endLayer();
            partial.layerImage({.layerId = 7, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(partial, false).has_value());
            CHECK(c.px(1, 1) == Px{0, 0, 0, 0});      // zone effacée (transparent)
            CHECK(c.px(1, 6) == Px{0, 255, 0, 255});  // reste conservé
            CHECK(c.px(6, 1) == Px{255, 0, 0, 255});

            // Une taille différente recrée la couche (contenu perdu, effacée).
            rd::DrawList resized = listOf();
            resized.beginLayer(7, {4, 4}, false);
            resized.endLayer();
            resized.layerImage({.layerId = 7, .position = {0, 0}, .size = {4, 4}, .contentSize = {4, 4}});
            REQUIRE(c.render(resized, false).has_value());
            CHECK(c.canvas->layerCount() == 1);
            CHECK(c.px(1, 1) == Px{0, 0, 0, 0});

            // Plus référencée : libérée.
            REQUIRE(c.render(listOf(), false).has_value());
            CHECK(c.canvas->layerCount() == 0);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("malformed lists are reported and leave no pass open")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList endWithout = listOf();
            endWithout.endLayer();
            CHECK((tests::errorOf(c.render(endWithout)) == ErrorCode::kInvalidArgument));
            rd::DrawList popWithout = listOf();
            popWithout.popClip();
            CHECK((tests::errorOf(c.render(popWithout)) == ErrorCode::kInvalidArgument));
            rd::DrawList open = listOf();
            open.beginLayer(3, {4, 4});
            CHECK((tests::errorOf(c.render(open)) == ErrorCode::kInvalidArgument));
            rd::DrawList noSize;
            CHECK((tests::errorOf(c.render(noSize)) == ErrorCode::kInvalidArgument));
            // Une liste valide fonctionne toujours après les erreurs (aucune passe n'est restée ouverte).
            rd::DrawList ok = listOf();
            ok.rect({0, 0}, {4, 4}, {1, 0, 0, 1});
            REQUIRE(c.render(ok).has_value());
            CHECK(c.px(1, 1) == Px{255, 0, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("layers hold premultiplied color: a 50 % layer composes like the same rectangle drawn directly (no double alpha) (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.rect({0, 0}, {16, 16}, {0, 0, 0, 1}); // fond noir opaque
            l.beginLayer(5, {8, 8});
            l.rect({0, 0}, {8, 8}, {1, 0, 0, 0.5f}); // rouge à 50 % sur une couche transparente
            l.endLayer();
            l.layerImage({.layerId = 5, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            l.rect({8, 8}, {8, 8}, {1, 0, 0, 0.5f}); // le même rectangle, dessiné directement
            REQUIRE(c.render(l).has_value());
            const Px viaLayer = c.px(2, 2);
            const Px direct = c.px(12, 12);
            CHECK(near(viaLayer[0], 128, 2));
            CHECK(near(viaLayer[0], direct[0], 2));
            CHECK(viaLayer[3] == 255);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("an opaque layer background stays opaque under half-transparent content (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.beginLayer(6, {8, 8});
            l.rect({0, 0}, {8, 8}, {1, 0, 0, 0.5f});
            l.endLayer();
            l.layerImage({.layerId = 6, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}, .color = {0, 0, 1, 1}});
            REQUIRE(c.render(l, false).has_value()); // cible transparente : l'alpha lu est celui de la couche composée
            const Px px = c.px(2, 2);
            CHECK(near(px[0], 128, 2));
            CHECK(near(px[2], 128, 2));
            CHECK(px[3] == 255);
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a layer whose image is clipped out is kept, so it can reappear without being redrawn (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList first = listOf();
            first.beginLayer(7, {8, 8});
            first.rect({0, 0}, {8, 8}, {1, 0, 0, 1});
            first.endLayer();
            first.layerImage({.layerId = 7, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(first, false).has_value());
            rd::DrawList hidden = listOf();
            hidden.pushClip({0, 0}, {0, 0}); // défilée hors de la vue
            hidden.layerImage({.layerId = 7, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            hidden.popClip();
            REQUIRE(c.render(hidden, false).has_value());
            CHECK(c.canvas->layerCount() == 1);
            rd::DrawList back = listOf();
            back.layerImage({.layerId = 7, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(back, false).has_value());
            CHECK(c.px(2, 2) == Px{255, 0, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a layer drawn inside a cached layer is kept as its dependency (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList first = listOf();
            first.beginLayer(1, {8, 8});
            first.beginLayer(2, {4, 4});
            first.rect({0, 0}, {4, 4}, {0, 1, 0, 1});
            first.endLayer();
            first.layerImage({.layerId = 2, .position = {0, 0}, .size = {4, 4}, .contentSize = {4, 4}});
            first.endLayer();
            first.layerImage({.layerId = 1, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(first, false).has_value());
            rd::DrawList quiet = listOf(); // rien n'a changé : seule l'image de la couche 1 est demandée
            quiet.layerImage({.layerId = 1, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(quiet, false).has_value());
            CHECK(c.canvas->layerCount() == 2); // la couche 2 vit dans le contenu de la 1 : gardée
            rd::DrawList partial = listOf(); // la 1 se redessine en partie et réaffiche la 2 sans la redessiner
            partial.beginLayer(1, {8, 8}, false);
            partial.clearRect({0, 0}, {4, 4});
            partial.layerImage({.layerId = 2, .position = {0, 0}, .size = {4, 4}, .contentSize = {4, 4}, .color = {0, 0, 1, 1}});
            partial.endLayer();
            partial.layerImage({.layerId = 1, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(partial, false).has_value());
            CHECK(c.px(1, 1) == Px{0, 255, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("an image of a missing layer is reported (kNotFound) after the rest of the list is drawn (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.layerImage({.layerId = 99, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            l.rect({8, 8}, {4, 4}, {1, 0, 0, 1});
            auto r = c.render(l, false);
            REQUIRE_FALSE(r.has_value());
            CHECK((tests::errorOf(r) == ErrorCode::kNotFound));
            CHECK(c.px(9, 9) == Px{255, 0, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("non-finite or huge sizes and clips are refused or emptied before any integer conversion (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            for (const float size : {std::numeric_limits<float>::quiet_NaN(), 4294967296.0f})
            {
                rd::DrawList l = listOf();
                l.beginLayer(3, {size, 4});
                l.endLayer();
                auto r = c.render(l);
                REQUIRE_FALSE(r.has_value());
                CHECK((tests::errorOf(r) == ErrorCode::kOutOfRange));
            }
            rd::DrawList far = listOf();
            far.pushClip({3e9f, 0}, {10, 10}); // hors de la surface : découpage vide, sans dépassement d'entier (UBSan)
            far.rect({0, 0}, {16, 16}, {1, 0, 0, 1});
            far.popClip();
            far.pushClip({std::numeric_limits<float>::infinity(), 0}, {1, 1});
            far.rect({0, 0}, {16, 16}, {1, 0, 0, 1});
            far.popClip();
            REQUIRE(c.render(far, false).has_value());
            CHECK(c.px(4, 4) == Px{0, 0, 0, 0});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("an encoder error during the render is returned by render, not only by submit (G99)")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            const TextureHandle stale = c.gpu.target(Format::kRGBA8Unorm, {4, 4}, "stale");
            c.gpu->destroy(stale);
            auto encoder = c.gpu->createCommandEncoder();
            auto r = c.canvas->render(*encoder, listOf(4), {stale, Format::kRGBA8Unorm, {4, 4}});
            REQUIRE_FALSE(r.has_value());
            CHECK((tests::errorOf(r) == ErrorCode::kNotFound));
            CHECK_FALSE(c.gpu->submit(std::move(encoder)).has_value());
        }
    }

    TEST_CASE("a replacement color written into a layer is premultiplied like the rest of its content")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.rect({0, 0}, {16, 16}, {0, 0, 0, 1});
            l.beginLayer(4, {8, 8});
            l.commands.emplace_back(rd::DrawClearRect{{0, 0}, {8, 8}, {1, 0, 0, 0.5f}}); // remplacement par une couleur non transparente
            l.endLayer();
            l.layerImage({.layerId = 4, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(l).has_value());
            CHECK(near(c.px(2, 2)[0], 128, 2)); // et non 255 : la couche est lue comme prémultipliée
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a clip inside an empty (inverted) clip stays empty, without calling std::clamp with inverted bounds")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList l = listOf();
            l.pushClip({10, 0}, {-5, 16}); // largeur négative : vide
            l.pushClip({0, 0}, {16, 16});
            l.rect({0, 0}, {16, 16}, {1, 0, 0, 1});
            l.popClip();
            l.popClip();
            REQUIRE(c.render(l, false).has_value());
            CHECK(c.px(4, 4) == Px{0, 0, 0, 0});
            c.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a list interrupted by an error evicts no layer: the next valid list can still show it")
    {
        FOR_EACH_BACKEND(backend)
        {
            Canvas c(backend);
            REQUIRE(c.gpu.device);
            rd::DrawList first = listOf();
            first.beginLayer(8, {8, 8});
            first.rect({0, 0}, {8, 8}, {0, 1, 0, 1});
            first.endLayer();
            first.layerImage({.layerId = 8, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(first, false).has_value());
            rd::DrawList broken = listOf();
            broken.beginLayer(9, {std::numeric_limits<float>::quiet_NaN(), 4}); // erreur avant de référencer la couche 8
            broken.endLayer();
            broken.layerImage({.layerId = 8, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            CHECK_FALSE(c.render(broken, false).has_value());
            CHECK(c.canvas->layerCount() == 1);
            rd::DrawList again = listOf();
            again.layerImage({.layerId = 8, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
            REQUIRE(c.render(again, false).has_value());
            CHECK(c.px(2, 2) == Px{0, 255, 0, 255});
            c.gpu.expectNoValidationErrors();
        }
    }
}
