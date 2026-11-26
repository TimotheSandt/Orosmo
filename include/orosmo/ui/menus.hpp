// UI-03 (fenêtres et menus) : boîte de dialogue modale, barre de menus, menus et sous-menus, menu contextuel au clic droit,
// notifications temporaires, image fournie par le jeu.
//
//   Dialog (dialog)              fenêtre modale centrée sur un voile (modalBackdrop) ; ouverte tant que la liaison `open` vaut
//                                vrai ; Échap, le bouton de fermeture ou un clic sur le voile (si permis) la ferment
//   MenuBar (menuBar)            rangée de menus ; survoler un autre menu pendant qu'un menu est ouvert passe à celui-ci
//   Menu (menu)                  titre qui ouvre ses éléments (fenêtre surgissante) ; dans un menu : sous-menu à droite
//   MenuItem (menuItem)          élément cliquable : raccourci affiché, case cochée facultative (liaison)
//   ContextMenu                  enfant invisible d'un widget : un clic droit sur ce widget (ou ce qu'il contient) ouvre ses
//                                éléments au pointeur
//   Notifications (toast…)       pile des notifications de Ui::notify : nombre maximal et hauteur maximale (les plus anciennes
//                                partent), disparition progressive, clic pour fermer
//   ImageView (image)            image produite par le jeu (mini-carte, boussole, aperçu de carte de hauteur...) : DynamicImage
#pragma once

#include "orosmo/ui/layout.hpp"
#include "orosmo/ui/widgets.hpp"

#include <memory>
#include <string>
#include <vector>

namespace orosmo::ui
{

class Dialog : public Widget
{
  public:
    Dialog(std::string title, Binding<bool> open, bool dismissOnOutsideClick = true);
    VBox &body() noexcept { return *body_; }
    // La fenêtre elle-même (taille : .width / .height du langage s'y appliquent).
    VBox &panel() noexcept { return *panel_; }
    bool isOpen() const { return open_.value(); }
    void close(Ui &ui);

    Widget *detached() const noexcept override { return panel_.get(); }
    void onKey(Ui &ui, int key) override;
    void onOutsideClick(Ui &ui) override;

  protected:
    // Dans l'arbre, la boîte ne prend aucune place : ouverte, elle s'inscrit comme fenêtre modale de l'interface.
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void arrange(Ui &, const Rect &, const Style &) override {}
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}
    void drawContent(const Ui &, renderer::DrawList &, const Style &) const override {}

  private:
    Binding<bool> open_;
    bool dismiss_;
    std::unique_ptr<VBox> panel_;
    VBox *body_;
    Ui *ui_ = nullptr; // interface de la dernière mise en page (bouton de fermeture)
};

class MenuItem : public Widget
{
  public:
    // `shortcut` est affiché ET installé : la combinaison (normalizeShortcut) déclenche l'élément même menu fermé.
    explicit MenuItem(std::string text, std::string shortcut = {}, Binding<bool> checked = {});
    const std::string &text() const noexcept { return text_; }
    const std::string &shortcut() const noexcept { return shortcut_; }
    // Coche (si liée), événement onClick, fermeture des menus.
    void activate(Ui &ui);

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    bool focusOnClick() const override { return false; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;
    void onKey(Ui &ui, int key) override;
    bool usesArrowKey(int key, bool fromGamepad) const override;
    bool onShortcut(Ui &ui, std::string_view shortcut) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::string text_;
    std::string shortcut_;
    Binding<bool> checked_;
};

class Menu : public Widget
{
  public:
    explicit Menu(std::string title);
    // Éléments du menu (MenuItem, Menu pour un sous-menu, Separator).
    VBox &items() noexcept { return *items_; }
    // Ouvre le menu ; `focusFirst` (clavier) : le premier élément prend le focus.
    void open(Ui &ui, bool focusFirst = false);
    bool inBar() const;

    Widget *detached() const noexcept override { return items_.get(); }
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    bool focusOnClick() const override { return false; }
    void onPress(Ui &ui, glm::vec2 mouse) override;
    void onKey(Ui &ui, int key) override;
    bool usesArrowKey(int key, bool fromGamepad) const override;
    void onHoverChanged(Ui &ui, bool hovered) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::string title_;
    std::unique_ptr<VBox> items_;
};

// Clavier : Tab amène le focus sur un titre ; Bas / Entrée / Espace ouvrent le menu ; Haut / Bas parcourent les éléments ;
// Droite ouvre un sous-menu (ou passe au menu suivant de la barre), Gauche le ferme (ou passe au précédent) ; Entrée / Espace
// activent ; Échap ferme le menu du dessus et rend le focus à son titre.
class MenuBar : public HBox
{
  public:
    MenuBar() : HBox("menuBar") {}
};

class ContextMenu : public Widget
{
  public:
    ContextMenu();
    VBox &items() noexcept { return *items_; }
    Widget *detached() const noexcept override { return items_.get(); }
    bool openContextMenu(Ui &ui, glm::vec2 mouse) override;

  protected:
    glm::vec2 measureContent(Ui &, glm::vec2, const Style &) override { return {0.0f, 0.0f}; }
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}
    void drawContent(const Ui &, renderer::DrawList &, const Style &) const override {}

  private:
    std::unique_ptr<VBox> items_;
};

class Notifications : public Widget
{
  public:
    // `maxCount` : nombre affiché au plus ; `maxHeight` (pixels, 0 : sans limite) : hauteur de la pile ; au-delà, les plus anciennes
    // sont retirées. Les plus récentes sont en bas.
    Notifications(int maxCount = 5, float maxHeight = 0.0f, float width = 0.0f);
    bool interactive() const override { return true; }
    void onRelease(Ui &ui, glm::vec2 mouse, bool inside) override;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    struct Placed
    {
        std::uint64_t id;
        Rect rect;
    };
    int maxCount_;
    float maxHeight_;
    float width_;
    mutable std::vector<Placed> placed_; // rectangles du dernier dessin (clic = fermer)
};

// Image RGBA8 écrite par le jeu (une fois, ou à chaque image) et affichée par ImageView. `commit()` après avoir changé les pixels :
// la texture est mise à jour à la prochaine image (même taille : sans la recréer).
class DynamicImage
{
  public:
    DynamicImage(std::uint32_t width, std::uint32_t height);
    std::uint32_t width() const noexcept { return image_->width; }
    std::uint32_t height() const noexcept { return image_->height; }
    // Pixels RGBA, première ligne = haut.
    std::span<std::uint8_t> pixels() noexcept { return image_->rgba; }
    void setPixel(std::uint32_t x, std::uint32_t y, const Color &c) noexcept;
    void fill(const Color &c) noexcept;
    void commit() noexcept { ++image_->version; }
    // Nouvelle taille (pixels effacés) : une nouvelle texture.
    void resize(std::uint32_t width, std::uint32_t height);
    std::shared_ptr<const renderer::AtlasImage> image() const noexcept { return image_; }

  private:
    std::shared_ptr<renderer::AtlasImage> image_;
};

// Source d'une ImageView (langage d'interface : `ImageView(state.minimap)`).
inline std::shared_ptr<const renderer::AtlasImage> imageOf(const DynamicImage &image) { return image.image(); }
inline std::shared_ptr<const renderer::AtlasImage> imageOf(std::shared_ptr<const renderer::AtlasImage> image) { return image; }

enum class ImageFit : std::uint8_t
{
    kContain, // entière, proportions gardées (bandes vides)
    kCover,   // remplit le widget, proportions gardées (bords coupés)
    kStretch, // remplit le widget, déformée
};

class ImageView : public Widget
{
  public:
    // `source` est relue à chaque image (une DynamicImage du jeu, ou n'importe quelle AtlasImage).
    explicit ImageView(std::function<std::shared_ptr<const renderer::AtlasImage>()> source, ImageFit fit = ImageFit::kContain);
    ImageView &setTint(const Color &c) { tint_ = c; return *this; }
    ImageView &setFit(ImageFit fit) { fit_ = fit; return *this; }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override;
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override;

  private:
    std::function<std::shared_ptr<const renderer::AtlasImage>()> source_;
    ImageFit fit_;
    Color tint_{1.0f};
};

} // namespace orosmo::ui
