// UI-07 : thèmes en JSON. Un thème est un fichier de données : des JETONS nommés (`color.primary`, `radius.medium`,
// `font.size.body`, `space.medium`, `shadow.size`...) et, par type de widget, des propriétés d'apparence qui renvoient à ces
// jetons, avec des variantes par ÉTAT (normal, survol, appuyé, désactivé, focus). Les widgets lisent leur `Style` ici : aucune
// couleur ni taille en dur dans un widget. Un thème peut en étendre un autre (`"extends": "base.json"`) : les variantes claire
// et sombre partagent la structure et ne changent que les jetons. `ThemeHost` recharge le thème à chaud (FileWatcher, CORE-13)
// et garde le dernier thème valide si le fichier modifié est invalide. Format complet : docs/UI_LANGUAGE.md (« Thèmes »).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace orosmo::core
{
class FileWatcher;
}

namespace orosmo::ui
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
    kImage,        // "image" : fond en neuf parties (nom d'une entrée de "images", ou "none")
    kImageTint,    // "imageTint" : couleur multipliée par l'image
    kFont,         // "font" : police (nom d'une entrée de "fonts", ou "default")
};
inline constexpr std::size_t kStylePropertyCount = 17;
std::string_view propertyName(StyleProperty property) noexcept;
std::optional<StyleProperty> propertyFromName(std::string_view name) noexcept;
bool isColorProperty(StyleProperty property) noexcept;

// Comment une bande (bord ou centre) d'une image en neuf parties remplit la place disponible.
enum class SliceFill : std::uint8_t
{
    kStretch, // étirée
    kRepeat,  // répétée à sa taille (dernière répétition coupée)
    kNone,    // supprimée (rien n'est dessiné à cet endroit)
};

// Image en neuf parties (UI-07) : quatre coins à taille fixe, quatre bords et un centre qui s'adaptent à la taille du widget
// (répétés, étirés ou supprimés). Une seule image sert à toutes les tailles, sans rien déformer dans les coins.
struct ImageStyle
{
    std::string name;
    std::shared_ptr<const renderer::AtlasImage> image; // RGBA8, ligne 0 = haut
    float left = 0.0f;  // largeur du coin gauche (texels)
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    SliceFill edges = SliceFill::kStretch;
    SliceFill center = SliceFill::kStretch;
    float scale = 1.0f; // pixels logiques par texel (coins et motifs répétés)
};
using ImageRef = std::shared_ptr<const ImageStyle>;

// Police d'un thème (section "fonts") : un fichier TrueType / OpenType. Nulle dans un style : la police par défaut de l'interface.
struct FontFace
{
    std::string name;
    std::filesystem::path path;
    std::int64_t stamp = 0; // date de modification du fichier au chargement du thème : un fichier remplacé est relu
};
using FontRef = std::shared_ptr<const FontFace>;

using TokenValue = std::variant<Color, float, ImageRef, FontRef>;

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
    ImageRef image;          // fond en neuf parties, dessiné par-dessus `background` (nul : aucun)
    Color imageTint{1.0f};
    FontRef font;            // police du texte (nulle : celle par défaut)

    // Écrit une propriété (couleur, nombre ou image selon la propriété ; un mauvais type est ignoré).
    void set(StyleProperty property, const TokenValue &value) noexcept;
};

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
    // Résout une valeur écrite dans un thème ou dans une surcharge locale : nom de jeton, couleur "#...", nombre, nom d'image
    // (section "images") ou "none" (aucune image).
    core::Result<TokenValue> resolve(std::string_view value) const;
    ImageRef image(std::string_view name) const;
    FontRef font(std::string_view name) const;
    // Style d'un type de widget dans un état. Type sans entrée dans le thème : style de "default".
    const Style &style(std::string_view widget, WidgetState state) const;
    bool hasWidget(std::string_view widget) const;
    // Fichiers lus (le thème, puis ses parents) : ceux que le rechargement à chaud observe.
    const std::vector<std::filesystem::path> &sources() const noexcept { return sources_; }

  private:
    Theme() = default; // seulement par ThemeBuilder (load, parse, fallback) : un thème a toujours un style "default"
    using StyleSet = std::array<Style, kWidgetStateCount>;
    std::string name_;
    std::map<std::string, TokenValue, std::less<>> tokens_;
    std::map<std::string, StyleSet, std::less<>> styles_; // contient toujours "default"
    std::map<std::string, ImageRef, std::less<>> images_;
    std::map<std::string, FontRef, std::less<>> fonts_;
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

    // Charge et remplace le thème courant ; en cas d'erreur, le thème courant est gardé et l'erreur renvoyée, mais `path` devient
    // le thème demandé : ses fichiers (même ceux qui manquent encore) sont observés et le corriger l'installe.
    core::Result<> load(const std::filesystem::path &path);
    // Installe un thème construit ailleurs, sans fichier à recharger (la provenance précédente est oubliée).
    void set(Theme theme);
    // Observe les fichiers du thème (le fichier et ses parents). Une modification signalée par `watcher.poll()` est appliquée au
    // prochain `reloadIfChanged()` (appelé par `Ui::update`) : un fichier invalide laisse le thème précédent en place, l'erreur
    // est journalisée et disponible par `lastError()`.
    // `watcher` doit vivre plus longtemps que ce ThemeHost (ou `unwatch()` être appelé avant sa destruction).
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
    void install(Theme theme);
    void rewatch();

    Theme theme_;
    std::vector<std::filesystem::path> attempted_; // fichiers lus par le dernier chargement ÉCHOUÉ (observés en plus)
    std::filesystem::path path_;
    std::uint64_t revision_ = 0;
    std::optional<core::Error> lastError_;
    core::FileWatcher *watcher_ = nullptr;
    std::vector<std::uint64_t> watchIds_;
    bool pending_ = false;
};

} // namespace orosmo::ui
