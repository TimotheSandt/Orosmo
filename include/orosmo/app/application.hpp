// Façade d'application (A75.1, reste d'A75.9 ; A79 lot L13) : une couche mince et FACULTATIVE au-dessus de l'API bas niveau,
// qui reste disponible. Elle possède et relie ce qu'un programme créait à la main : fenêtre, périphérique (avec repli Vulkan ->
// OpenGL), contexte de rendu (bibliothèque de shaders, cache de pipelines), renderers de scène et 2D, lumières, ombres et
// tonemapping facultatifs, chargeur de modèles (caches de textures et de programmes), police et `ui::Ui`, `sim::Simulation`,
// module de jeu facultatif, et le système de rendu des composants de la scène (`SceneRenderSystem`).
//
//     int main(int argc, char **argv)
//     {
//         return orosmo::app::Application::main(argc, argv, {.title = "Cube"}, [](orosmo::app::Application &app) {
//             auto cube = app.addMesh(orosmo::renderer::MeshBuilder(orosmo::renderer::MeshLayout::kColored).addBox({1, 1, 1}));
//             if (!cube) return orosmo::core::Result<>(std::unexpected(cube.error()));
//             app.spawn(orosmo::app::MeshRenderer{.mesh = *cube});
//             app.spawn(orosmo::app::LightComponent{.direction = {-0.6f, -0.8f, -0.5f}});
//             app.onUpdate([](orosmo::app::Application &, const orosmo::app::FrameTime &time) { ... time.dt ... });
//             return orosmo::core::Result<>();
//         });
//     }
//
// Une image (`frame`, appelée par `run`), dans cet ordre :
//   1. événements de la fenêtre ; delta d'image réel BORNÉ (`maxFrameDelta`, `core::FrameTimer`), ou fixe (un pas de
//      simulation) sans fenêtre ou en capture, sauf horloge fournie (`settings.clock`) : même image d'une exécution à l'autre ;
//   2. module de jeu (`GameModule::update` : rechargement à chaud en développement), puis `Simulation::advance` (pas fixe ; sauf
//      `pauseSimulation`) ;
//   3. `onUpdate(app, temps)` ; 4. `Ui::update` puis `onUi(app, temps)` ;
//   5. rendu : système de la scène (`update`, caméra active, `dispatch` hors passe), `beginFrame`, `setCamera`, dessins des
//      composants, `onRender(app, image)` (dessins en plus, dans la passe de la scène), `endFrame`, interface, présentation.
// Les rappels reçoivent le pas de temps borné et le pas fixe (`FrameTime`, reste d'A75.9).
//
// Erreurs : `std::expected` (règle 10). Un rappel `onRender` qui échoue, ou une image qui échoue (y compris `beginFrame` ou
// `present` du périphérique, perte comprise ; seul kBusy est toléré), arrête `run` (code d'échec). À la destruction, les rappels
// (et leurs captures) sont libérés avant le module de jeu et le périphérique.
// Ordre de vie : un état lié par l'interface (`cui::build...(ui, state)`) doit vivre plus longtemps que l'application (le
// déclarer avant elle) ou être retiré par `ui().setRoot(nullptr)`.
#pragma once

#include <orosmo/app/game_api.hpp>
#include <orosmo/app/game_context.hpp>
#include <orosmo/app/game_module.hpp>
#include <orosmo/app/render_components.hpp>
#include <orosmo/app/scene_render_system.hpp>
#include <orosmo/core/error.hpp>
#include <orosmo/core/system.hpp>
#include <orosmo/core/time.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/image.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/mesh_builder.hpp>
#include <orosmo/rhi/device.hpp>
#include <orosmo/scene/entity.hpp>
#include <orosmo/scene/transform.hpp>
#include <orosmo/sim/simulation.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace orosmo::platform
{
class Window;
}
namespace orosmo::renderer
{
class SceneRenderer;
class ShadowFeature;
} // namespace orosmo::renderer
namespace orosmo::ui
{
class Ui;
}
namespace orosmo::assets
{
class ModelLoader;
struct Model;
} // namespace orosmo::assets
namespace orosmo::anim
{
struct AnimatedModelOptions;
}

namespace orosmo::app
{

class Application;

// Logique de jeu facultative (`GameModule`), démarrée par `create` sur la simulation et les actions de l'interface. Deux sources,
// choisies dans le programme (pas dans le moteur : la version finale ne lie ainsi ni `platform::DynamicLibrary` ni le code de
// rechargement, GAME-07) :
//   - `GameSource::fromApi(orosmoGameApi())` : table liée dans l'exécutable (version finale, `OROSMO_GAME_STATIC`) ;
//   - `GameSource::fromLibrary("game")` : bibliothèque dynamique rechargée à chaud (développement). Chemin relatif : depuis le
//     dossier de l'exécutable ; sans extension : celle de la plate-forme (.dll, .dylib, .so).
// `defaultGame()` (orosmo/app/default_game.hpp) choisit selon `OROSMO_GAME_STATIC`.
struct GameSource
{
    using Starter = std::function<core::Result<GameModule>(const GameTargets &)>;
    Starter start;
    // Échec du démarrage du jeu : vrai = `create` échoue ; faux (défaut) = journalisé, l'application tourne sans jeu.
    bool required = false;

    static GameSource fromApi(const OrosmoGameApi *api)
    {
        return {.start = [api](const GameTargets &targets) { return GameModule::fromApi(api, targets); }};
    }
    static GameSource fromLibrary(std::filesystem::path library, bool watch = true)
    {
        return {.start = [library = std::move(library), watch](const GameTargets &targets) {
            return GameModule::load({.path = resolveGameLibrary(library), .targets = targets, .watch = watch});
        }};
    }
    // `library` relatif -> dossier de l'exécutable ; extension de la plate-forme ajoutée s'il n'en a pas.
    static std::filesystem::path resolveGameLibrary(std::filesystem::path library)
    {
        if (!library.has_extension())
        {
#if defined(_WIN32)
            library += ".dll";
#elif defined(__APPLE__)
            library += ".dylib";
#else
            library += ".so";
#endif
        }
        return library.is_absolute() ? library : core::system::executablePath().parent_path() / library;
    }
};

// Usine de périphérique (défaut : `rhi::createDevice`) ; un test la remplace pour provoquer un échec de backend.
using DeviceFactory = std::function<core::Result<std::unique_ptr<rhi::Device>>(const rhi::DeviceDesc &)>;

struct ApplicationSettings
{
    std::string title = "Orosmo";
    int width = 1280;
    int height = 720;

    // Backend voulu ; Vulkan qui ne démarre pas passe à OpenGL si `backendFallback` (pas de repli depuis OpenGL ni Metal).
    rhi::Backend backend = rhi::Backend::kVulkan;
    bool backendFallback = true;
    // Sans fenêtre : rendu hors écran de `width` × `height` (un GPU est quand même demandé).
    bool headless = false;
    bool validation = true; // couches de validation Vulkan / GL_KHR_debug
    bool vsync = false;
    DeviceFactory createDevice;

    // Dossier des ressources (shader/rhi, fonts, themes). Vide : « res » s'il contient shader/rhi (construction de
    // développement), sinon le dossier courant (paquet installé, ressources à côté de l'exécutable).
    std::filesystem::path resourceDirectory;
    // Shaders du moteur ; vide : `resourceDirectory / "shader/rhi"`.
    std::filesystem::path shaderDirectory;

    std::array<float, 4> clearColor{0.10f, 0.13f, 0.18f, 1.0f};
    bool shadows = false; // plugin d'ombres (ShadowFeature) : `castShadows` des composants
    bool hdr = false;     // couleur de scène RGBA16F + tonemapping (TonemapFeature)

    // Interface : police (relative à `resourceDirectory`) et thème ; `ui = false` : ni police ni `Ui` (`ui()` interdit).
    bool ui = true;
    std::filesystem::path font = "fonts/Roboto-Regular.ttf";
    unsigned fontSize = 48;
    std::filesystem::path theme = "themes/dark.json";

    sim::SimulationSettings simulation{};
    std::optional<GameSource> game; // voir GameSource

    // Delta d'image réel borné (A75.9) ; horloge (défaut : `core::SteadyClock`, ou `ManualClock` d'un test).
    core::Nanoseconds maxFrameDelta = std::chrono::milliseconds(250);
    const core::IClock *clock = nullptr;

    // Arrêt de `run` : après `frames` images (0 : jusqu'à la fermeture de la fenêtre ; sans fenêtre et sans capture :
    // `kDefaultHeadlessFrames`).
    std::uint32_t frames = 0;
    // Capture : après `captureFrames` images, scene.png (scène seule) et ui.png (image composée) dans ce dossier, puis arrêt.
    // Un `frames` non nul plus petit que `captureFrames` arrête sans capture : `run` rend faux.
    std::filesystem::path captureDirectory;
    std::uint32_t captureFrames = 3;

    static constexpr std::uint32_t kDefaultHeadlessFrames = 3;
};

// Options de la ligne de commande comprises par `parseArguments` : --api vulkan|opengl, --strict-backend, --headless,
// --size WxH, --frames N, --capture DIR, --capture-frames N, --help. Rend faux pour --help (rien d'autre n'est lu après) ;
// kInvalidArgument (message qui nomme l'option) pour une option inconnue ou une valeur invalide. Un dossier de capture relatif
// est rendu absolu par rapport au dossier courant (avant le changement de dossier de `main`).
core::Result<bool> parseArguments(ApplicationSettings &settings, int argc, const char *const *argv);
std::string usage(std::string_view program);

// Temps fourni aux rappels (A75.9).
struct FrameTime
{
    double dt = 0.0;            // delta d'image borné (s) : animations, caméra, interface
    double fixedStep = 0.0;     // pas fixe de la simulation (s)
    int steps = 0;              // pas de simulation joués à cette image
    double alpha = 0.0;         // fraction du pas suivant écoulée (interpolation)
    std::uint64_t frame = 0;    // numéro de l'image (0 pour la première)
    std::uint64_t simulationStep = 0; // pas de simulation joués depuis le début
};

// Image en cours de rendu, pour `onRender` : dessins en plus dans la passe de la scène (`scene.drawMesh(...)`, entre `setCamera` et
// `endFrame`).
struct RenderFrame
{
    renderer::SceneRenderer &scene;
    const renderer::Camera &camera;
    rhi::CommandEncoder &encoder;
    rhi::Extent2D size;
};

using UpdateCallback = std::function<void(Application &, const FrameTime &)>;
using UiCallback = std::function<void(Application &, const FrameTime &)>;
using RenderCallback = std::function<core::Result<>(Application &, RenderFrame &)>;
using SetupCallback = std::function<core::Result<>(Application &)>;

class Application
{
  public:
    // Crée tout (fenêtre, périphérique avec repli, renderers, interface, simulation, jeu). Erreurs : celles de la création qui a
    // échoué, message qui la nomme ; kUnsupported si le backend ne démarre pas sans repli possible.
    static core::Result<std::unique_ptr<Application>> create(ApplicationSettings settings);

    // Programme complet : `parseArguments`, dossier courant = celui de l'exécutable, journal, `create`, `setup`, `run`, fin de la
    // fenêtre, journal écrit. Rend le code de sortie (EXIT_SUCCESS / EXIT_FAILURE ; --help : succès).
    static int main(int argc, char **argv, ApplicationSettings settings, const SetupCallback &setup = {});

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    ~Application();

    // ── Boucle ────────────────────────────────────────────────────────────────
    // Images jusqu'à `quit`, la fermeture de la fenêtre, `frames` ou la capture. Faux si une image ou la capture a échoué.
    bool run();
    // Une image (ordre en tête du fichier), même après un arrêt demandé (`run` ne la rappelle plus). Faux : arrêt demandé
    // (`quit`, `frames` ou capture atteints, fenêtre fermée : alors rien n'est rendu) ; erreur : image ratée.
    core::Result<bool> frame();
    void quit() noexcept;
    bool quitting() const noexcept;

    void onUpdate(UpdateCallback callback);
    void onUi(UiCallback callback);
    void onRender(RenderCallback callback);

    // Arrête l'avance de la simulation (`steps` = 0) ; le rendu et les rappels continuent.
    void pauseSimulation(bool paused) noexcept;
    bool simulationPaused() const noexcept;

    // ── Scène ─────────────────────────────────────────────────────────────────
    scene::World &world() noexcept;
    sim::Simulation &simulation() noexcept;
    const RenderComponentTypes &components() const noexcept;
    // Caméra de l'image (remplacée par la caméra active de la scène si une entité porte `CameraComponent`).
    renderer::Camera &camera() noexcept;

    // Entité avec ce composant et cette transformation locale.
    scene::Entity spawn(const MeshRenderer &component, const scene::Transform &transform = {});
    scene::Entity spawn(const LightComponent &component, const scene::Transform &transform = {});
    scene::Entity spawn(const CameraComponent &component, const scene::Transform &transform = {});
    scene::Entity spawn(const AnimatedModelComponent &component, const scene::Transform &transform = {});

    // ── Ressources (poignées des composants) ─────────────────────────────────
    core::Handle<renderer::Mesh> addMesh(renderer::Mesh mesh);
    core::Result<core::Handle<renderer::Mesh>> addMesh(const renderer::MeshData &data);
    core::Result<core::Handle<renderer::Mesh>> addMesh(const renderer::MeshBuilder &builder);
    renderer::Mesh *mesh(core::Handle<renderer::Mesh> handle) noexcept;
    // Modèle animé (glTF), chargé par le chargeur de l'application (textures et programmes partagés) ; matériaux récepteurs
    // d'ombres si `shadows`. Le lecteur se pilote par `animatedModel(h)->play(...)`.
    core::Result<core::Handle<anim::AnimatedModel>> loadAnimatedModel(const std::filesystem::path &path);
    core::Result<core::Handle<anim::AnimatedModel>> loadAnimatedModel(const std::filesystem::path &path, const anim::AnimatedModelOptions &options);
    core::Result<core::Handle<anim::AnimatedModel>> addAnimatedModel(assets::Model model);
    anim::AnimatedModel *animatedModel(core::Handle<anim::AnimatedModel> handle) noexcept;
    SceneResources &resources() noexcept;
    // Chemin d'une ressource : `resourceDirectory / relative`.
    std::filesystem::path resourcePath(const std::filesystem::path &relative) const;

    // ── Sous-systèmes (API bas niveau) ────────────────────────────────────────
    rhi::Device &device() noexcept;
    rhi::Backend backend() const noexcept;
    // Vrai si Vulkan n'a pas démarré et que l'application est passée à OpenGL.
    bool fellBack() const noexcept;
    const renderer::RenderContext &renderContext() const noexcept;
    renderer::SceneRenderer &scene() noexcept;
    renderer::ShadowFeature *shadows() noexcept; // nul sans `shadows`
    renderer::LightManager &lights() noexcept;
    SceneRenderSystem &sceneSystem() noexcept;
    // Chargeur de modèles de l'application, créé au premier appel (erreurs de `ModelLoader::create`).
    core::Result<assets::ModelLoader *> modelLoader();
    // Interface (`settings.ui`) ; `ui()` sans interface : assertion (ENGINE_ASSERT) et comportement indéfini.
    bool hasUi() const noexcept;
    ui::Ui &ui() noexcept;
    platform::Window *window() noexcept; // nul sans fenêtre
    GameModule *game() noexcept;         // nul sans jeu
    const ApplicationSettings &settings() const noexcept;

    // Taille de l'image de la dernière image rendue (fenêtre : tampon d'image ; sans fenêtre ou en capture : width × height).
    rhi::Extent2D outputSize() const noexcept;
    // Image composée (scène + interface) de la dernière image rendue, lue sur le GPU. kNotFound avant la première image.
    core::Result<renderer::Image> readOutput();
    std::uint64_t frameIndex() const noexcept;

    struct Impl;

  private:
    explicit Application(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace orosmo::app
