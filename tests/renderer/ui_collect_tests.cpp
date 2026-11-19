// UI-02 (lot B5) : l'interface produit une liste de dessin, sans aucun appel graphique. Tests sans GPU. Fichier séparé du RHI : les
// en-têtes de l'interface tirent des noms globaux hérités qui entrent en conflit avec `using namespace engine::rhi`.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "renderer/repo_paths.hpp"

#include "UI/Manager.h"
#include "UI/Widgets.h"

#include <filesystem>

using namespace UI;
namespace rd = engine::renderer;
namespace fs = std::filesystem;

namespace
{

// Les chemins de ressources de l'interface sont relatifs au dossier courant (`res/fonts/...`) : on se place à la racine du dépôt.
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

template <typename T> const T *firstOf(const rd::DrawList &list)
{
    for (const rd::DrawCommand &c : list.commands)
    {
        if (const T *p = std::get_if<T>(&c))
        {
            return p;
        }
    }
    return nullptr;
}

} // namespace

TEST_SUITE("UI/collect")
{
    TEST_CASE("boxes in a container become rectangles at their layout position, in order")
    {
        auto red = CreateBox(Bounds(40_px, 20_px), {1.0f, 0.0f, 0.0f, 1.0f});
        auto blue = CreateBox(Bounds(10_px, 10_px, Anchor::BOTTOM_RIGHT), {0.0f, 0.0f, 1.0f, 0.5f});
        auto root = CreateContainer(Bounds(100_px, 100_px), {red, blue});
        root->Initialize();
        root->Update();
        rd::DrawList list;
        list.logicalSize = {100, 100};
        root->Collect(list, {100, 100}, {7, 9});
        REQUIRE(list.count<rd::DrawRect>() == 2); // le conteneur transparent ne dessine rien
        const auto &first = std::get<rd::DrawRect>(list.commands[0]);
        const glm::vec4 redBounds = red->GetCachedBoundsInParent();
        CHECK(first.position == glm::vec2(7 + redBounds.x, 9 + redBounds.y)); // décalage du parent + position dans le parent
        CHECK(first.size == glm::vec2(40, 20));
        CHECK(first.color == glm::vec4(1, 0, 0, 1));
        const auto &second = std::get<rd::DrawRect>(list.commands[1]);
        const glm::vec4 blueBounds = blue->GetCachedBoundsInParent();
        CHECK(second.position == glm::vec2(7 + blueBounds.x, 9 + blueBounds.y));
        CHECK(second.size == glm::vec2(10, 10));
        CHECK(second.color.a == 0.5f);
        CHECK(second.position != first.position); // l'ancrage donne bien une autre position
        // Aucun appel graphique : l'arbre se collecte sans runtime graphique hérité (ce test n'en crée aucun).
    }

    TEST_CASE("overflow hidden pushes a clip around the children; a transparent background draws no rectangle")
    {
        auto root = CreateContainer(Bounds(80_px, 60_px), {CreateBox(Bounds(200_px, 200_px), {0.0f, 1.0f, 0.0f, 1.0f})});
        root->SetOverflowMode(OverflowMode::HIDDEN);
        root->Initialize();
        root->Update();
        rd::DrawList list;
        root->Collect(list, {80, 60}, {5, 7});
        REQUIRE(list.size() == 3);
        const auto *clip = std::get_if<rd::PushClip>(&list.commands[0]);
        REQUIRE(clip != nullptr);
        CHECK(clip->position == glm::vec2(5, 7));
        CHECK(clip->size == glm::vec2(80, 60));
        CHECK(std::holds_alternative<rd::DrawRect>(list.commands[1]));
        CHECK(std::holds_alternative<rd::PopClip>(list.commands[2]));
    }

    TEST_CASE("a scrolled container shifts its children by the scroll offset")
    {
        auto root = CreateContainer(Bounds(50_px, 50_px), {CreateBox(Bounds(10_px, 10_px), {1, 1, 1, 1})});
        root->Initialize();
        root->Update();
        rd::DrawList still;
        root->Collect(still, {50, 50});
        root->SetScrollOffset({3.0f, 4.0f});
        rd::DrawList scrolled;
        root->Collect(scrolled, {50, 50});
        const auto &a = std::get<rd::DrawRect>(still.commands[0]);
        const auto &b = std::get<rd::DrawRect>(scrolled.commands[0]);
        CHECK(b.position == a.position - glm::vec2(3, 4));
    }

    TEST_CASE("render-to-texture containers emit a layer once, then only its image until something changes")
    {
        auto child = CreateBox(Bounds(20_px, 20_px), {1.0f, 0.0f, 0.0f, 1.0f});
        auto root = CreateContainer(Bounds(64_px, 64_px), {child});
        root->SetRenderToTexture(true);
        root->SetScrollOffset({2.0f, 0.0f});
        root->Initialize();
        root->Update();

        rd::DrawList first;
        first.logicalSize = {64, 64};
        root->Collect(first, {64, 64}, {10, 20});
        REQUIRE(first.count<rd::BeginLayer>() == 1);
        CHECK(first.count<rd::EndLayer>() == 1);
        CHECK(first.count<rd::DrawLayerImage>() == 1);
        const auto *begin = firstOf<rd::BeginLayer>(first);
        CHECK(begin->clear);
        const auto *image = firstOf<rd::DrawLayerImage>(first);
        CHECK(image->layerId == begin->id);
        CHECK(image->position == glm::vec2(10, 20));
        CHECK(image->scrollOffset == glm::vec2(2, 0));
        CHECK(image->size == glm::vec2(64, 64));

        rd::DrawList quiet; // rien n'a changé : la couche garde son contenu
        quiet.logicalSize = {64, 64};
        root->Collect(quiet, {64, 64}, {10, 20});
        CHECK(quiet.count<rd::BeginLayer>() == 0);
        CHECK(quiet.count<rd::DrawLayerImage>() == 1);

        child->SetColor(0.0f, 1.0f, 0.0f, 1.0f); // changement d'apparence : mise à jour partielle
        root->Update();
        rd::DrawList partial;
        partial.logicalSize = {64, 64};
        root->Collect(partial, {64, 64}, {10, 20});
        REQUIRE(partial.count<rd::BeginLayer>() == 1);
        CHECK_FALSE(firstOf<rd::BeginLayer>(partial)->clear);
        CHECK(partial.count<rd::DrawClearRect>() == 1);
        CHECK(partial.count<rd::DrawRect>() == 1);

        root->SetSize({32, 32}); // taille changée : couche recréée, tout redessiné
        root->Update();
        rd::DrawList resized;
        resized.logicalSize = {64, 64};
        root->Collect(resized, {64, 64}, {10, 20});
        REQUIRE(resized.count<rd::BeginLayer>() == 1);
        CHECK(firstOf<rd::BeginLayer>(resized)->clear);
        CHECK(firstOf<rd::BeginLayer>(resized)->size == glm::vec2(32, 32));
    }

    TEST_CASE("a partial redraw keeps the drawing order: an overlapping later child is redrawn over the changed one, under a clip (G99)")
    {
        auto back = CreateBox(Bounds(20_px, 20_px), {1.0f, 0.0f, 0.0f, 1.0f});
        auto front = CreateBox(Bounds(20_px, 20_px), {0.0f, 0.0f, 1.0f, 1.0f}); // au même endroit, devant
        auto root = CreateContainer(Bounds(64_px, 64_px), {back, front});
        root->SetRenderToTexture(true);
        root->Initialize();
        root->Update();
        rd::DrawList first;
        first.logicalSize = {64, 64};
        root->Collect(first, {64, 64});

        back->SetColor(0.0f, 1.0f, 0.0f, 1.0f); // seul celui de derrière change
        root->Update();
        rd::DrawList partial;
        partial.logicalSize = {64, 64};
        root->Collect(partial, {64, 64});
        REQUIRE(partial.count<rd::BeginLayer>() == 1);
        CHECK_FALSE(firstOf<rd::BeginLayer>(partial)->clear);
        CHECK(partial.count<rd::PushClip>() == 1);
        CHECK(partial.count<rd::DrawClearRect>() == 1);
        REQUIRE(partial.count<rd::DrawRect>() == 2); // les deux enfants qui touchent la zone, dans l'ordre
        std::vector<glm::vec4> colors;
        for (const auto &command : partial.commands)
        {
            if (const auto *r = std::get_if<rd::DrawRect>(&command))
            {
                colors.push_back(r->color);
            }
        }
        CHECK(colors[0] == glm::vec4(0, 1, 0, 1)); // derrière (modifié) d'abord
        CHECK(colors[1] == glm::vec4(0, 0, 1, 1)); // puis celui de devant : il reste devant
    }

    TEST_CASE("Manager::InvalidateLayers makes the next list redraw every cached layer in full (G99)")
    {
        auto child = CreateBox(Bounds(20_px, 20_px), {1.0f, 0.0f, 0.0f, 1.0f});
        auto root = CreateContainer(Bounds(64_px, 64_px), {child});
        root->SetRenderToTexture(true);
        root->Initialize();
        root->Update();
        // Le point d'entrée réel : Manager (racine injectée, sans Init, donc sans police).
        Manager &manager = Manager::Instance();
        manager.SetRootContainer(root);
        rd::DrawList first;
        manager.Collect(first, 64, 64);
        REQUIRE(first.count<rd::BeginLayer>() == 1);
        rd::DrawList quiet;
        manager.Collect(quiet, 64, 64);
        REQUIRE(quiet.count<rd::BeginLayer>() == 0);
        manager.InvalidateLayers();
        rd::DrawList again;
        manager.Collect(again, 64, 64);
        REQUIRE(again.count<rd::BeginLayer>() == 1);
        CHECK(firstOf<rd::BeginLayer>(again)->clear);
        manager.Shutdown();
    }

    TEST_CASE("a partial redraw also redraws a descendant that overflows its container into the cleared zone (G99)")
    {
        auto back = CreateBox(Bounds(20_px, 20_px, Anchor::TOP_RIGHT), {1.0f, 0.0f, 0.0f, 1.0f}); // x = 44..64
        auto wide = CreateBox(Bounds(64_px, 20_px), {0.0f, 0.0f, 1.0f, 1.0f});                   // déborde de son conteneur
        auto narrow = CreateContainer(Bounds(20_px, 20_px), {wide});                             // x = 0..20
        auto root = CreateContainer(Bounds(64_px, 64_px), {back, narrow});
        root->SetRenderToTexture(true);
        root->Initialize();
        root->Update();
        rd::DrawList first;
        first.logicalSize = {64, 64};
        root->Collect(first, {64, 64});
        back->SetColor(0.0f, 1.0f, 0.0f, 1.0f);
        root->Update();
        rd::DrawList partial;
        partial.logicalSize = {64, 64};
        root->Collect(partial, {64, 64});
        std::vector<glm::vec4> colors;
        for (const auto &command : partial.commands)
        {
            if (const auto *r = std::get_if<rd::DrawRect>(&command))
            {
                colors.push_back(r->color);
            }
        }
        REQUIRE(colors.size() == 2); // le modifié, puis le descendant débordant du conteneur suivant (bornes du conteneur hors zone)
        CHECK(colors[0] == glm::vec4(0, 1, 0, 1));
        CHECK(colors[1] == glm::vec4(0, 0, 1, 1));
    }

    TEST_CASE("text widgets produce glyph runs through the text renderer (atlas shared, vertices in logical pixels)")
    {
        const CurrentDirectory cwd(tests::repoRoot(__FILE__));
        Manager &manager = Manager::Instance();
        manager.Init(400, 300);
        auto text = CreateText(Bounds(), "Hi", 0.5f);
        auto root = CreateContainer(Bounds(400_px, 300_px), {text});
        root->Initialize();
        root->Update();
        rd::DrawList list;
        list.logicalSize = {400, 300};
        root->Collect(list, {400, 300});
        const auto *run = firstOf<rd::DrawGlyphs>(list);
        REQUIRE(run != nullptr);
        CHECK(run->vertices.size() == 2 * 6 * 4); // deux glyphes, 6 sommets de 4 flottants
        REQUIRE(run->atlas != nullptr);
        CHECK(run->atlas->width > 0);
        CHECK(run->atlas->rgba.size() == static_cast<std::size_t>(run->atlas->width) * run->atlas->height * 4);
        CHECK(run->msdfPixelRange == 8.0f);
        for (std::size_t i = 0; i < run->vertices.size(); i += 4)
        {
            CHECK(run->vertices[i] >= 0.0f);
            CHECK(run->vertices[i] <= 400.0f);
            CHECK(run->vertices[i + 1] >= 0.0f);
            CHECK(run->vertices[i + 1] <= 300.0f);
        }
        // Un second passage réutilise le même atlas (même identifiant) : le dessinateur garde sa texture.
        rd::DrawList again;
        again.logicalSize = {400, 300};
        text->MarkFullDirty();
        root->Collect(again, {400, 300});
        REQUIRE(firstOf<rd::DrawGlyphs>(again) != nullptr);
        CHECK(firstOf<rd::DrawGlyphs>(again)->atlas->id == run->atlas->id);
        manager.Shutdown();
    }

    TEST_CASE("Manager::Collect fills the logical size and the default UI")
    {
        const CurrentDirectory cwd(tests::repoRoot(__FILE__));
        Manager &manager = Manager::Instance();
        manager.Init(1280, 720);
        manager.Update(0.016f, 1280, 720);
        rd::DrawList list;
        manager.Collect(list, 1280, 720);
        CHECK(list.logicalSize == glm::vec2(1280, 720));
        CHECK(list.count<rd::DrawRect>() == 3); // le fond bleu de la boîte verticale et ses deux boîtes
        manager.Shutdown();
    }
}
