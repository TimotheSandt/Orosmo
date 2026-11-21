// Options de lancement de la démo : choix de l'API graphique (--api ou --backend, repli ou --strict-backend), mode d'interface, thème, onglet, mode sans
// fenêtre, capture et dimensions de vue.
#pragma once

#include <orosmo/core/error.hpp>
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

enum class UiMode : std::uint8_t
{
    kOff,
    kHud,
    kFull,
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
};

// Nom affiché : « OpenGL », « Vulkan », « Metal ».
std::string_view backendDisplayName(orosmo::rhi::Backend api) noexcept;
// « opengl » / « gl », « vulkan » / « vk », « metal » (casse indifférente).
std::optional<orosmo::rhi::Backend> parseBackend(std::string_view value) noexcept;
// « off », « hud », « full » (casse indifférente).
std::optional<UiMode> parseUiMode(std::string_view value) noexcept;

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
