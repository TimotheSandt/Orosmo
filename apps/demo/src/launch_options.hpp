// Options de lancement de la démo : choix de l'API graphique (--api ou --backend, repli ou --strict-backend), mode d'interface, thème, onglet, mode sans
// fenêtre, capture et dimensions de vue.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/renderer/debug_view.hpp>
#include <orosmo/rhi/types.hpp>

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace demo
{

enum class BackendAvailability : std::uint8_t
{
    kAvailable,
    kNotBuilt,
    kUnsupportedPlatform,
};

// --lighting forward|forwardplus (REN-04) : forward (toutes les lumières par fragment, défaut : captures de référence inchangées)
// ou forward+ clusterisé (pré-passe de profondeur, clusters remplis en compute ; repli sur forward si le GPU n'a pas le compute).
enum class LightingMode : std::uint8_t
{
    kForward,
    kForwardPlus,
};

enum class UiMode : std::uint8_t
{
    kOff,
    kHud,
    kFull,
};

// --post none|hdr|bloom|full (REN-17/18) : post-traitements de la scène.
enum class PostMode : std::uint8_t
{
    kNone,  // aucun : scène LDR, image identique à celle d'avant les post-traitements
    kHdr,   // scène HDR + tonemapping ACES (sRGB en sortie)
    kBloom, // kHdr + bloom
    kFull,  // kBloom + exposition automatique, étalonnage par LUT, vignettage
};

struct LaunchOptions
{
    orosmo::rhi::Backend api = orosmo::rhi::Backend::kOpenGL;
    bool showHelp = false;
    bool listApis = false;
    bool chooseApiInteractively = false;
    bool apiExplicitlyRequested = false;
    // --strict-backend (RHI-04) : pas de repli sur OpenGL si le backend demandé (Vulkan) est indisponible ; sortie en erreur.
    bool strictBackend = false;
    // --capture <dir> [--capture-frames N] : N images rendues, puis <dir>/scene.png et ui.png écrites, puis sortie (TOOL-03).
    std::string captureDirectory;
    std::uint32_t captureFrames = 10;
    // --headless (PLAT-04) : aucune fenêtre (périphérique hors écran, vue fixe, pas de temps fixe, aucune entrée). La démo
    // rend --capture-frames N images (10 par défaut), enregistre les images si --capture est donné, puis quitte.
    bool headless = false;
    // --vsync : présentation synchronisée sur le rafraîchissement de l'écran (désactivée par défaut ; sans effet en headless).
    bool vsync = false;
    // --ui off|hud|full (défaut : hud).
    UiMode uiMode = UiMode::kHud;
    // --theme dark|light|framed (défaut : dark).
    std::string theme = "dark";
    // --tab scene|widgets|controls|color|windows|layout|themes (défaut : scene).
    std::string tab = "scene";
    // --hover <id> / --click <id> : pointeur simulé pendant --capture (--click exige --capture-frames >= 3, --hover >= 2).
    std::string hoverId;
    std::string clickId;
    // --size WxH (défaut : 1280x720, minimum : 640x400).
    int width = 1280;
    int height = 720;
    // --instances N (REN-28/30/31) : N objets instanciés autour de la cour (culling, LOD, un draw call par lot). 0 (défaut) : aucun,
    // la scène et les captures de référence sont inchangées.
    std::uint32_t instances = 0;
    // --debug-view none|normals|albedo|overdraw|lod|wireframe|motion (REN-35). none (défaut) : image et cibles inchangées.
    orosmo::renderer::DebugView debugView = orosmo::renderer::DebugView::kNone;
    // --freeze-frustum (REN-35) : fige le frustum de culling des objets instanciés dès la première image et le dessine.
    bool freezeFrustum = false;
    // --post none|hdr|bloom|full (défaut : none, captures de référence inchangées).
    PostMode post = PostMode::kNone;
    LightingMode lighting = LightingMode::kForward;
    // --lights N (REN-04/05) : N lumières ponctuelles déterministes au-dessus de la cour, en plus du soleil. 0 (défaut) : aucune.
    std::uint32_t pointLights = 0;
    // --scene default|mixed (REN-32, R2D-05) : « mixed » ajoute des transparents triés et du 2D dans le monde. Défaut : scène inchangée.
    bool mixedScene = false;
};

// Nom affiché : « OpenGL », « Vulkan », « Metal ».
std::string_view backendDisplayName(orosmo::rhi::Backend api) noexcept;
// « opengl » / « gl », « vulkan » / « vk », « metal » (casse indifférente).
std::optional<orosmo::rhi::Backend> parseBackend(std::string_view value) noexcept;
// « off », « hud », « full » (casse indifférente).
std::optional<UiMode> parseUiMode(std::string_view value) noexcept;
// « none », « hdr », « bloom », « full » (casse indifférente).
std::optional<PostMode> parsePostMode(std::string_view value) noexcept;
// « forward », « forwardplus » / « forward+ » (casse indifférente).
std::optional<LightingMode> parseLightingMode(std::string_view value) noexcept;

// Argument inconnu ou valeur invalide : erreur kInvalidArgument (message prêt à afficher).
orosmo::core::Result<LaunchOptions> parseLaunchOptions(int argc, const char *const *argv);
void printUsage(std::ostream &out);
// Menu interactif (--choose-api) : faux si l'entrée est fermée ou le choix inconnu (`selected` inchangé).
bool promptForBackend(std::istream &input, std::ostream &output, orosmo::rhi::Backend &selected);

// Suit les backends compilés dans le RHI (RHI-04) ; Metal n'existe que sur les plateformes Apple.
BackendAvailability backendAvailability(orosmo::rhi::Backend api) noexcept;
bool isBackendAvailable(orosmo::rhi::Backend api) noexcept;
std::string backendAvailabilityMessage(orosmo::rhi::Backend api);

// Décision de sélection du backend (RHI-04), pure : aucune création de périphérique, testable sans GPU.
struct BackendDecision
{
    orosmo::rhi::Backend backend = orosmo::rhi::Backend::kOpenGL; // backend à utiliser (sans objet si `refused`)
    bool fellBack = false;                                       // faux : le backend demandé est utilisé
    bool refused = false;                                        // vrai : aucun backend utilisable, la démo s'arrête
    std::string message;                                         // raison à afficher (vide si le backend demandé est utilisé)
};

// `availability` : état du backend demandé dans cette construction. `failureReason` : raison renvoyée par `createDevice`
// si la création a déjà échoué (vide avant la tentative). Seul Vulkan retombe sur OpenGL, sauf `strict` ; Metal et OpenGL
// indisponibles sont refusés.
BackendDecision resolveBackend(orosmo::rhi::Backend requested, bool strict, BackendAvailability availability, std::string_view failureReason = {});

} // namespace demo
