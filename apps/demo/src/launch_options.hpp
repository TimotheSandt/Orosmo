// Options de lancement de la démo : choix de l'API graphique (--api ou --backend, repli ou --strict-backend), mode d'interface, thème, onglet, mode sans
// fenêtre, capture et dimensions de vue.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/renderer/antialiasing.hpp>
#include <orosmo/renderer/debug_view.hpp>
#include <orosmo/renderer/frame_pacing.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/renderer/transparent_shadow.hpp>
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

// --shadow-quality low|medium|high (M8) : préréglage des ombres (ShadowSettings passé à ShadowFeature::create). high (défaut)
// est égal à ShadowSettings{}, les réglages du moteur : captures de référence inchangées.
enum class ShadowQuality : std::uint8_t
{
    kLow,
    kMedium,
    kHigh,
};

enum class UiMode : std::uint8_t
{
    kOff,
    kHud,
    kFull,
};

// --hud off|fps|full|graph : niveau du panneau de performances (mode d'interface « hud ») ; Ctrl+F passe au suivant.
enum class HudLevel : std::uint8_t
{
    kOff,   // rien d'affiché
    kFps,   // les FPS seulement
    kFull,  // toutes les mesures (défaut : le contenu du panneau d'avant, complété)
    kGraph, // toutes les mesures + l'historique des temps d'image, en bas à droite
};

// --post none|hdr|bloom|full (REN-17/18) : post-traitements de la scène.
enum class PostMode : std::uint8_t
{
    kNone,  // aucun : scène LDR, image identique à celle d'avant les post-traitements
    kHdr,   // scène HDR + tonemapping ACES (sRGB en sortie)
    kBloom, // kHdr + bloom
    kFull,  // kBloom + exposition automatique, étalonnage par LUT, vignettage
};

// --gpu-timings auto|on|off : minuteries GPU du Profiler. auto (défaut) : actives seulement quand un panneau de performances affiche
// des temps GPU (niveaux full et graph du HUD) ; on / off : forcées.
enum class GpuTimingsMode : std::uint8_t
{
    kAuto,
    kOn,
    kOff,
};

struct LaunchOptions
{
    orosmo::rhi::Backend api = orosmo::rhi::Backend::kVulkan; // défaut : Vulkan, repli sur OpenGL s'il ne démarre pas
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
    // --vsync [on|off] : présentation synchronisée sur le rafraîchissement de l'écran (activée par défaut ; « --vsync » seul = on ;
    // sans effet en headless). --fps-limit N (10 à 1000) : limite à N images par seconde, vsync coupée ; 0 : pas de limite.
    // --vsync on avec --fps-limit est refusé (deux cadences contradictoires).
    bool vsync = true;
    bool vsyncExplicit = false;
    std::uint32_t fpsLimit = 0;
    // --direct-present on|off (défaut : on) : Vulkan dessine directement dans l'image de la fenêtre (G185) au lieu de copier une
    // image hors écran ; peut scintiller à très haute cadence sans vsync (docs/GAPS.md). Sans effet sur OpenGL et en headless.
    bool directPresentation = true;
    // --gpu-timings auto|on|off (défaut : auto).
    GpuTimingsMode gpuTimings = GpuTimingsMode::kAuto;
    // --ui off|hud|full (défaut : hud).
    UiMode uiMode = UiMode::kHud;
    // --hud off|fps|full|graph (défaut : full, le panneau complet comme avant ; Ctrl+F fait défiler les niveaux).
    HudLevel hudLevel = HudLevel::kFull;
    // --theme dark|light|framed (défaut : dark).
    std::string theme = "dark";
    // --tab scene|widgets|controls|color|windows|layout|themes (défaut : scene).
    std::string tab = "scene";
    // --hover <id> / --click <id> : pointeur simulé pendant --capture (--click exige --capture-frames >= 3, --hover >= 2).
    std::string hoverId;
    std::string clickId;
    // --focus <id> (UI-09) : focus clavier sur ce widget pendant --capture, anneau de focus visible (--capture-frames >= 2).
    std::string focusId;
    // --size WxH (défaut : 1280x720, minimum : 640x400).
    int width = 1280;
    int height = 720;
    // --instances N (REN-28/30/31) : N objets instanciés autour de la cour (culling, LOD, un draw call par lot). 0 (défaut) : aucun,
    // la scène et les captures de référence sont inchangées.
    std::uint32_t instances = 0;
    // --gpu-driven off|on (REN-29) : les objets de --instances passent par le culling GPU (scène GPU persistante, multi-draw
    // indirect, occlusion Hi-Z). off (défaut) : culling CPU, captures de référence inchangées. Repli sur le CPU sans compute.
    bool gpuDriven = false;
    // --debug-view none|normals|albedo|overdraw|lod|wireframe|motion (REN-35). none (défaut) : image et cibles inchangées.
    orosmo::renderer::DebugView debugView = orosmo::renderer::DebugView::kNone;
    // --freeze-frustum (REN-35) : fige le frustum de culling des objets instanciés dès la première image et le dessine.
    bool freezeFrustum = false;
    // --shadows off|on (REN-06 à 08) : ombres en cascade avec cache. off (défaut) : captures de référence inchangées.
    bool shadows = false;
    // --shadow-quality low|medium|high (M8) : préréglage des ombres. high (défaut) : ShadowSettings{}, captures inchangées.
    ShadowQuality shadowQuality = ShadowQuality::kHigh;
    // --post none|hdr|bloom|full (défaut : none, captures de référence inchangées).
    PostMode post = PostMode::kNone;
    // --aa none|fxaa|smaa|taa (REN-19) : anticrénelage. none (défaut) : aucun plugin, captures de référence inchangées.
    orosmo::renderer::AntiAliasingMode antiAliasing = orosmo::renderer::AntiAliasingMode::kNone;
    LightingMode lighting = LightingMode::kForward;
    // --lights N (REN-04/05) : N lumières ponctuelles déterministes au-dessus de la cour, en plus du soleil. 0 (défaut) : aucune.
    std::uint32_t pointLights = 0;
    // --scene default|mixed|anim (REN-32, R2D-05, M6) : « mixed » ajoute des transparents triés et du 2D dans le monde. Défaut : scène inchangée.
    bool mixedScene = false;
    // --scene anim (M6) : personnage skinné animé (glTF généré puis importé, machine à états, skinning en compute) et foule de
    // mille agents (un draw call par primitive skinnée) ; texture rechargée à chaud. Défaut : scène inchangée.
    bool animScene = false;
    // --transparent-shadow none|full|masked|dithered (REN-39) : ombre des vitres de --scene mixed avec --shadows on. Absent (défaut) :
    // les vitres ne sont pas des casters (comportement d'avant REN-39, captures de référence inchangées).
    std::optional<orosmo::renderer::TransparentShadowMode> transparentShadow;
};

// Nom affiché : « OpenGL », « Vulkan », « Metal ».
std::string_view backendDisplayName(orosmo::rhi::Backend api) noexcept;
// « opengl » / « gl », « vulkan » / « vk », « metal » (casse indifférente).
std::optional<orosmo::rhi::Backend> parseBackend(std::string_view value) noexcept;
// « off », « hud », « full » (casse indifférente).
std::optional<UiMode> parseUiMode(std::string_view value) noexcept;
// « off », « fps », « full », « graph » (casse indifférente).
std::optional<HudLevel> parseHudLevel(std::string_view value) noexcept;
// Niveau suivant de Ctrl+F : off -> fps -> full -> graph -> off.
HudLevel nextHudLevel(HudLevel level) noexcept;
std::string_view hudLevelName(HudLevel level) noexcept;
// « auto », « on », « off » (casse indifférente).
std::optional<GpuTimingsMode> parseGpuTimingsMode(std::string_view value) noexcept;
std::string_view gpuTimingsModeName(GpuTimingsMode mode) noexcept;
// Minuteries GPU voulues : forcées par on / off ; en auto, seulement si le panneau de performances (HUD ou interface complète)
// affiche des temps GPU, c'est-à-dire pour les niveaux full et graph. Pure, testable sans GPU.
bool gpuTimingsWanted(GpuTimingsMode mode, UiMode uiMode, HudLevel level) noexcept;
// Cadence demandée par la ligne de commande : vsync (défaut), limite --fps-limit, ou sans limite (--vsync off).
orosmo::renderer::FramePacing framePacingFor(const LaunchOptions &options) noexcept;
// « none », « hdr », « bloom », « full » (casse indifférente).
std::optional<PostMode> parsePostMode(std::string_view value) noexcept;
std::string_view postModeName(PostMode mode) noexcept;
// « forward », « forwardplus » / « forward+ » (casse indifférente).
std::optional<LightingMode> parseLightingMode(std::string_view value) noexcept;

// « low », « medium », « high » (casse indifférente).
std::optional<ShadowQuality> parseShadowQuality(std::string_view value) noexcept;
std::string_view shadowQualityName(ShadowQuality quality) noexcept;
// Préréglage d'ombres (tableau de docs/05 M8) ; kHigh rend ShadowSettings{}. Les champs hors du tableau gardent les défauts du moteur.
orosmo::renderer::ShadowSettings shadowSettingsForQuality(ShadowQuality quality) noexcept;
// Égalité champ par champ (ShadowSettings n'a pas d'operator==, le moteur n'est pas modifié par cette option).
bool sameShadowSettings(const orosmo::renderer::ShadowSettings &left, const orosmo::renderer::ShadowSettings &right) noexcept;
// Nom du préréglage égal à `settings` (« low », « medium », « high »), sinon « custom » : affiché par le panneau de performances.
std::string_view shadowQualityLabel(const orosmo::renderer::ShadowSettings &settings) noexcept;

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

// --aa taa avec --instances N : les objets instanciés (InstanceRenderer, culling GPU) n'écrivent pas l'AOV de mouvement que le TAA
// lit ; leur historique serait reprojeté avec un mouvement nul (traînées). Combinaison refusée : le TAA est désactivé (aucun
// anticrénelage) et `message` dit pourquoi. Pure, testable sans GPU.
struct AntiAliasingDecision
{
    orosmo::renderer::AntiAliasingMode mode = orosmo::renderer::AntiAliasingMode::kNone;
    std::string message; // vide si la demande est acceptée telle quelle
};
AntiAliasingDecision resolveAntiAliasing(orosmo::renderer::AntiAliasingMode requested, std::uint32_t instances);

} // namespace demo
