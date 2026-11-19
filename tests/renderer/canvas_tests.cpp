// UI-02 (lot B5) : exécution d'une liste de dessin 2D sur le RHI : rectangles, mélange alpha, découpages, glyphes, couches hors écran.
// Repère de lecture du RHI : première ligne = bas ; les listes de dessin ont l'origine en haut (une position y logique petite = haut).
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <engine/renderer/canvas_renderer.hpp>

#include <limits>

namespace rd = engine::renderer;
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

    explicit Canvas(std::uint32_t size = 16)
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

    engine::core::Result<> render(const rd::DrawList &list, bool load = true)
    {
        auto encoder = gpu->createCommandEncoder();
        auto r = canvas->render(*encoder, list, canvasTarget(), load);
        auto submitted = gpu->submit(std::move(encoder));
        return r ? submitted : r;
    }
    // Texel en coordonnées de l'interface (origine en haut).
    std::array<std::uint8_t, 4> px(int x, int yTop, std::uint32_t size = 16) { return rgba(gpu.pixel(target, x, static_cast<int>(size) - 1 - yTop)); }
};

rd::DrawList listOf(float size = 16.0f)
{
    rd::DrawList l;
    l.logicalSize = {size, size};
    return l;
}

using Px = std::array<std::uint8_t, 4>;

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
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("the logical size is independent of the target size (HiDPI, render scale)")
    {
        Canvas c(32);
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
        rd::DrawList l = listOf(16.0f); // 16x16 logiques sur 32x32 pixels
        l.rect({0, 0}, {8, 8}, {1, 0, 0, 1});
        auto encoder = c.gpu->createCommandEncoder();
        REQUIRE(c.canvas->render(*encoder, l, c.canvasTarget(32), false).has_value());
        REQUIRE(c.gpu->submit(std::move(encoder)).has_value());
        CHECK(c.px(15, 15, 32) == Px{255, 0, 0, 255}); // 8 logiques = 16 pixels
        CHECK(c.px(17, 17, 32) == Px{0, 0, 0, 0});
    }

    TEST_CASE("clip rectangles intersect and an empty clip draws nothing")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("glyph runs sample the atlas; atlases are cached by id")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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
        CHECK((c.render(bad).error().code == ErrorCode::kInvalidArgument));
        c.gpu.expectNoValidationErrors();
    }

    TEST_CASE("layers: drawn off screen, shown with scrolling, kept between lists, cleared in part, released when unused")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("malformed lists are reported and leave no pass open")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
        rd::DrawList endWithout = listOf();
        endWithout.endLayer();
        CHECK((c.render(endWithout).error().code == ErrorCode::kInvalidArgument));
        rd::DrawList popWithout = listOf();
        popWithout.popClip();
        CHECK((c.render(popWithout).error().code == ErrorCode::kInvalidArgument));
        rd::DrawList open = listOf();
        open.beginLayer(3, {4, 4});
        CHECK((c.render(open).error().code == ErrorCode::kInvalidArgument));
        rd::DrawList noSize;
        CHECK((c.render(noSize).error().code == ErrorCode::kInvalidArgument));
        // Une liste valide fonctionne toujours après les erreurs (aucune passe n'est restée ouverte).
        rd::DrawList ok = listOf();
        ok.rect({0, 0}, {4, 4}, {1, 0, 0, 1});
        REQUIRE(c.render(ok).has_value());
        CHECK(c.px(1, 1) == Px{255, 0, 0, 255});
        c.gpu.expectNoValidationErrors();
    }

    TEST_CASE("layers hold premultiplied color: a 50 % layer composes like the same rectangle drawn directly (no double alpha) (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("an opaque layer background stays opaque under half-transparent content (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("a layer whose image is clipped out is kept, so it can reappear without being redrawn (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("a layer drawn inside a cached layer is kept as its dependency (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
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

    TEST_CASE("an image of a missing layer is reported (kNotFound) after the rest of the list is drawn (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
        rd::DrawList l = listOf();
        l.layerImage({.layerId = 99, .position = {0, 0}, .size = {8, 8}, .contentSize = {8, 8}});
        l.rect({8, 8}, {4, 4}, {1, 0, 0, 1});
        auto r = c.render(l, false);
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kNotFound));
        CHECK(c.px(9, 9) == Px{255, 0, 0, 255});
        c.gpu.expectNoValidationErrors();
    }

    TEST_CASE("non-finite or huge sizes and clips are refused or emptied before any integer conversion (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
        for (const float size : {std::numeric_limits<float>::quiet_NaN(), 4294967296.0f})
        {
            rd::DrawList l = listOf();
            l.beginLayer(3, {size, 4});
            l.endLayer();
            auto r = c.render(l);
            REQUIRE_FALSE(r.has_value());
            CHECK((r.error().code == ErrorCode::kOutOfRange));
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

    TEST_CASE("an encoder error during the render is returned by render, not only by submit (G99)")
    {
        Canvas c;
        SKIP_TEST_IF_NULL(c.gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle stale = c.gpu.target(Format::kRGBA8Unorm, {4, 4}, "stale");
        c.gpu->destroy(stale);
        auto encoder = c.gpu->createCommandEncoder();
        auto r = c.canvas->render(*encoder, listOf(4), {stale, Format::kRGBA8Unorm, {4, 4}});
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kNotFound));
        CHECK_FALSE(c.gpu->submit(std::move(encoder)).has_value());
    }
}
