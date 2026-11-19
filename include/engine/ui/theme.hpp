// UI-07 : thèmes en JSON. Un thème est un fichier de données : des JETONS nommés (`color.primary`, `radius.medium`,
// `font.size.body`, `space.medium`, `shadow.size`...) et, par type de widget, des propriétés d'apparence qui renvoient à ces
// jetons, avec des variantes par ÉTAT (normal, survol, appuyé, désactivé, focus). Les widgets lisent leur `Style` ici : aucune
// couleur ni taille en dur dans un widget. Un thème peut en étendre un autre (`"extends": "base.json"`) : les variantes claire
// et sombre partagent la structure et ne changent que les jetons. `ThemeHost` recharge le thème à chaud (FileWatcher, CORE-13)
// et garde le dernier thème valide si le fichier modifié est invalide. Format complet : docs/UI_LANGUAGE.md (« Thèmes »).
#pragma once

#include "engine/core/error.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine::core
{
class FileWatcher;
}

namespace engine::ui
{

using Color = glm::vec4;

enum class WidgetState : std::uint8_t
{
    kNormal,
    kHover,
    kPressed,
    kDisabled,
    kFocus,
};
inline constexpr std::size_t kWidgetStateCount = 5;
// "normal", "hover", "pressed", "disabled", "focus" (noms du fichier de thème).
std::string_view stateName(WidgetState state) noexcept;

// Propriétés d'apparence (noms du fichier de thème entre parenthèses).
enum class StyleProperty : std::uint8_t
{
    kBackground,   // "background" : fond
    kForeground,   // "foreground" : texte, icônes
    kBorder,       // "border" : couleur de bordure
    kAccent,       // "accent" : partie active (remplissage d'un curseur, coche, onglet choisi)
    kMuted,        // "muted" : partie inactive (piste d'un curseur, texte secondaire)
    kShadowColor,  // "shadowColor"
    kBorderWidth,  // "borderWidth" (px)
    kRadius,       // "radius" (px)
    kPadding,      // "padding" (px, marge intérieure)
    kSpacing,      // "spacing" (px, entre enfants)
    kFontSize,     // "fontSize" (px)
    kShadowSize,   // "shadowSize" (px, flou de l'ombre portée)
    kShadowOffset, // "shadowOffset" (px, décalage vertical de l'ombre)
    kWidth,        // "width" (px, taille de référence : piste d'un curseur, case à cocher, barre de défilement)
};
inline constexpr std::size_t kStylePropertyCount = 14;
std::string_view propertyName(StyleProperty property) noexcept;
std::optional<StyleProperty> propertyFromName(std::string_view name) noexcept;
bool isColorProperty(StyleProperty property) noexcept;

// Apparence résolue d'un widget dans un état.
struct Style
{
    Color background{0.0f};
    Color foreground{0.0f, 0.0f, 0.0f, 1.0f};
    Color border{0.0f};
    Color accent{0.0f};
    Color muted{0.0f};
    Color shadowColor{0.0f};
    float borderWidth = 0.0f;
    float radius = 0.0f;
    float padding = 0.0f;
    float spacing = 0.0f;
    float fontSize = 16.0f;
    float shadowSize = 0.0f;
    float shadowOffset = 0.0f;
    float width = 0.0f;

    // Écrit une propriété (couleur pour les propriétés de couleur, nombre sinon ; un mauvais type est ignoré).
    void set(StyleProperty property, const std::variant<Color, float> &value) noexcept;
};

using TokenValue = std::variant<Color, float>;

// Lit une couleur "#rrggbb" ou "#rrggbbaa" (composantes sRGB 0..255 → 0..1, sans conversion de profil).
std::optional<Color> parseColor(std::string_view text) noexcept;

class Theme
{
  public:
    // Lit `path` et la chaîne de ses parents (`extends`, chemin relatif au fichier qui l'écrit). Erreurs (kIo, kParse) : fichier
    // illisible, JSON invalide, propriété ou état inconnu, jeton absent, type de valeur faux, cycle d'extension. Le message donne
    // le fichier et le chemin JSON (`widgets.button.states.hover.background`).
    static core::Result<Theme> load(const std::filesystem::path &path);
    // Même chose depuis un texte (`baseDirectory` : dossier où chercher les parents).
    static core::Result<Theme> parse(std::string_view json, const std::filesystem::path &baseDirectory = {},
                                     std::string_view sourceName = "<theme>");
    // Thème minimal intégré (tests, repli quand aucun fichier n'est lisible) : quelques jetons gris, lisible mais neutre.
    static Theme fallback();

    const std::string &name() const noexcept { return name_; }
    std::optional<TokenValue> token(std::string_view name) const;
    // Résout une valeur écrite dans un thème ou dans une surcharge locale : nom de jeton, couleur "#..." ou nombre.
    core::Result<TokenValue> resolve(std::string_view value) const;
    // Style d'un type de widget dans un état. Type sans entrée dans le thème : style de "default".
    const Style &style(std::string_view widget, WidgetState state) const;
    bool hasWidget(std::string_view widget) const;
    // Fichiers lus (le thème, puis ses parents) : ceux que le rechargement à chaud observe.
    const std::vector<std::filesystem::path> &sources() const noexcept { return sources_; }

  private:
    using StyleSet = std::array<Style, kWidgetStateCount>;
    std::string name_;
    std::map<std::string, TokenValue, std::less<>> tokens_;
    std::map<std::string, StyleSet, std::less<>> styles_; // contient toujours "default"
    std::vector<std::filesystem::path> sources_;

    friend struct ThemeBuilder;
};

// Thème courant d'une interface, rechargé à chaud.
class ThemeHost
{
  public:
    ThemeHost();
    ~ThemeHost();
    ThemeHost(const ThemeHost &) = delete;
    ThemeHost &operator=(const ThemeHost &) = delete;

    // Charge et remplace le thème courant ; en cas d'erreur, le thème courant est gardé et l'erreur renvoyée.
    core::Result<> load(const std::filesystem::path &path);
    void set(Theme theme);
    // Observe les fichiers du thème (le fichier et ses parents). Une modification signalée par `watcher.poll()` est appliquée au
    // prochain `reloadIfChanged()` (appelé par `Ui::update`) : un fichier invalide laisse le thème précédent en place, l'erreur
    // est journalisée et disponible par `lastError()`.
    void watch(core::FileWatcher &watcher);
    void unwatch();
    // Relit le thème si un de ses fichiers a changé depuis le dernier appel ; vrai si un nouveau thème est installé.
    bool reloadIfChanged();

    const Theme &current() const noexcept { return theme_; }
    // Incrémenté à chaque thème installé (chargement ou rechargement réussi) : les widgets refont leur mise en page.
    std::uint64_t revision() const noexcept { return revision_; }
    const std::optional<core::Error> &lastError() const noexcept { return lastError_; }
    const std::filesystem::path &path() const noexcept { return path_; }

  private:
    void rewatch();

    Theme theme_;
    std::filesystem::path path_;
    std::uint64_t revision_ = 0;
    std::optional<core::Error> lastError_;
    core::FileWatcher *watcher_ = nullptr;
    std::vector<std::uint64_t> watchIds_;
    bool pending_ = false;
};

} // namespace engine::ui
