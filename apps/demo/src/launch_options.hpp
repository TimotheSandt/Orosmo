// Options de lancement de la démo : choix de l'API graphique (backend du RHI), graine du terrain, mode sans fenêtre, capture.
// Propres à la démo (elles vivaient dans l'ancien module Graphics) ; seul OpenGL est disponible avant M4 (Vulkan) et M9 (Metal).
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

struct LaunchOptions
{
    orosmo::rhi::Backend api = orosmo::rhi::Backend::kOpenGL;
    bool showHelp = false;
    bool listApis = false;
    bool chooseApiInteractively = false;
    bool apiExplicitlyRequested = false;
    // --capture <dir> [--capture-frames N] : N images rendues, puis <dir>/scene.png et ui.png écrites, puis sortie (TOOL-03).
    std::string captureDirectory;
    std::uint32_t captureFrames = 10;
    // --seed N : graine du terrain (défaut : tirée au hasard par main() et journalisée). --capture implique la graine 1 sauf --seed.
    std::optional<std::uint32_t> seed;
    // --headless (PLAT-04) : aucune fenêtre (périphérique hors écran, vue fixe 1280x720, pas de temps fixe, aucune entrée). La démo
    // rend --capture-frames N images (10 par défaut), enregistre les images si --capture est donné, puis quitte.
    bool headless = false;
    // --vsync : présentation synchronisée sur le rafraîchissement de l'écran (désactivée par défaut ; sans effet en headless).
    bool vsync = false;
};

// Nom affiché : « OpenGL », « Vulkan », « Metal ».
std::string_view backendDisplayName(orosmo::rhi::Backend api) noexcept;
// « opengl » / « gl », « vulkan » / « vk », « metal » (casse indifférente).
std::optional<orosmo::rhi::Backend> parseBackend(std::string_view value) noexcept;

// Argument inconnu ou valeur invalide : erreur kInvalidArgument (message prêt à afficher).
orosmo::core::Result<LaunchOptions> parseLaunchOptions(int argc, const char *const *argv);
void printUsage(std::ostream &out);
// Menu interactif (--choose-api) : faux si l'entrée est fermée ou le choix inconnu (`selected` inchangé).
bool promptForBackend(std::istream &input, std::ostream &output, orosmo::rhi::Backend &selected);

// Suit les backends compilés dans le RHI (RHI-04) ; Metal n'existe que sur les plateformes Apple.
BackendAvailability backendAvailability(orosmo::rhi::Backend api) noexcept;
bool isBackendAvailable(orosmo::rhi::Backend api) noexcept;
std::string backendAvailabilityMessage(orosmo::rhi::Backend api);

} // namespace demo
