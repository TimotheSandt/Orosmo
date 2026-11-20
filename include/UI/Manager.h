#pragma once
#include "Core/Container.h"
#include "engine/ui/text_renderer.hpp"

// Transition : le rendu de texte est dans engine::ui (text_renderer.hpp).
namespace UI
{
using engine::ui::TextAnchor;
using engine::ui::TextRenderer;
} // namespace UI

class Window;

namespace UI
{

class Manager
{
    std::shared_ptr<Container> rootContainer;
    std::shared_ptr<TextRenderer> textRenderer;
    const Window *window = nullptr;
    bool active = true;

    int lastWidth = 0;
    int lastHeight = 0;

  public:
    static Manager &Instance();

    void Init(int w, int h, const Window *window = nullptr);
    void Shutdown();
    void CreateUI(int w, int h, const Window *window = nullptr);

    void Update(float dt, int w, int h);
    // UI-02 : produit la liste de dessin de l'interface (aucun appel graphique) ; `CanvasRenderer` l'exécute.
    void Collect(engine::renderer::DrawList &list, int w, int h);
    // Le contenu des couches mis en cache par `Collect` n'a pas été rendu (rendu en échec, cible abandonnée, couche perdue :
    // `CanvasRenderer::render` renvoie kNotFound) : la prochaine liste redessine tout.
    void InvalidateLayers();

    void SetRootContainer(std::shared_ptr<Container> root) { rootContainer = std::move(root); }
    std::shared_ptr<Container> GetRootContainer() { return rootContainer; }

    std::shared_ptr<TextRenderer> GetTextRenderer() { return textRenderer; }

    void SetActive(bool a) { active = a; }
    bool IsActive() const { return active; }
};

} // namespace UI
