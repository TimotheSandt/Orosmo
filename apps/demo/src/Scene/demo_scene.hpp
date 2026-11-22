#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/core/math.hpp"
#include "orosmo/core/reflection.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/debug_draw.hpp"
#include "orosmo/renderer/instance_renderer.hpp"
#include "orosmo/renderer/light.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/object_set.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/shadow_feature.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <tuple>
#include <vector>

namespace demo
{

enum class ProjectionMode : std::uint8_t
{
    kPerspective,
    kOrthographic,
};

struct DemoSceneParams
{
    bool animate = true;
    float animationSpeed = 1.0f;
    bool wireframe = false;
    bool debugDraw = true;
    bool cameraRelative = false;
    float fovDegrees = 70.0f;
    ProjectionMode projection = ProjectionMode::kPerspective;
    float renderScale = 1.0f;
    float sunElevation = 42.0f;
    float sunAzimuth = 35.0f;
    float sunIntensity = 0.95f;
    orosmo::core::Vec3 sunColor{1.0f, 0.96f, 0.88f};
    float ambientStrength = 0.28f;
    orosmo::core::Vec3 ambientColor{0.68f, 0.78f, 0.95f};
    orosmo::core::Vec3 accentColor{0.28f, 0.62f, 0.92f};
};

class DemoScene
{
  public:
    DemoScene() = default;
    ~DemoScene();
    DemoScene(const DemoScene &) = delete;
    DemoScene &operator=(const DemoScene &) = delete;

    static glm::vec3 defaultCameraPosition();
    static glm::vec3 defaultCameraOrientation();

    // `forwardPlus` (REN-04) : variante forward+ de mesh.frag (le plugin ForwardPlusLighting doit être installé sur le renderer) ;
    // `pointLights` (--lights N) : N lumières ponctuelles déterministes au-dessus de la cour, en plus du soleil.
    orosmo::core::Result<> init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &shaders,
                                const std::filesystem::path &shaderDirectory, bool forwardPlus = false, std::uint32_t pointLights = 0);
    // Lumières ponctuelles de --lights N : spirale de Fibonacci (rayon 12 m), hauteurs et couleurs tirées d'un hachage entier de
    // l'indice (aucun hasard, aucune horloge) ; intensité réduite quand N grandit (environ une douzaine de lumières par point du sol).
    static std::vector<orosmo::renderer::Light> pointLightField(std::uint32_t count);
    // --instances N (REN-28/30/31) : N objets instanciés autour de la cour (après init). 0 : rien n'est créé.
    orosmo::core::Result<> initInstances(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &shaders,
                                         orosmo::renderer::PipelineCache &pipelines, const std::filesystem::path &shaderDirectory,
                                         std::uint32_t count);
    // --shadows on (REN-06 à 08) : à appeler AVANT init/initInstances. Les maillages lisent alors les ombres (programme récepteur)
    // et sont enregistrés comme casters (fixes : la structure et les instances ; mobiles : le cristal et les prismes). Nul : rien.
    void setShadows(orosmo::renderer::ShadowFeature *shadows) noexcept { shadows_ = shadows; }
    void destroy();
    void update(float deltaSeconds, const DemoSceneParams &params);
    orosmo::core::Result<> render(orosmo::renderer::SceneRenderer &renderer, orosmo::renderer::Camera &camera,
                                  const DemoSceneParams &params);

    // Statistiques du dernier dessin instancié (zéros sans --instances).
    const orosmo::renderer::VisibilityStats &instanceVisibility() const noexcept { return instanceVisibility_; }
    const orosmo::renderer::InstanceDrawStats &instanceDrawStats() const noexcept { return instanceDrawStats_; }

  private:
    orosmo::core::Result<> renderInstances(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera);
    void applyTransforms();
    orosmo::core::Result<> syncLights(const DemoSceneParams &params);
    orosmo::core::Result<> syncAccentColor(const DemoSceneParams &params);

    orosmo::core::Result<> registerCaster(const orosmo::renderer::Mesh &mesh, const orosmo::renderer::MeshData &data,
                                          orosmo::renderer::CasterMobility mobility);

    std::shared_ptr<orosmo::renderer::ShaderProgram> meshProgram_;
    orosmo::renderer::ShadowFeature *shadows_ = nullptr;
    std::vector<orosmo::renderer::ShadowFeature::CasterId> casterIds_;
    std::uint64_t shadowFrames_ = 0;
    orosmo::renderer::Mesh groundMesh_;
    orosmo::renderer::Mesh structureMesh_;
    orosmo::renderer::Mesh heroMesh_;
    std::array<orosmo::renderer::Mesh, 3> orbitMeshes_;
    orosmo::renderer::LightManager lights_;
    orosmo::renderer::DebugDraw debugDraw_;

    // Objets instanciés (--instances) : deux niveaux de détail, deux matériaux.
    std::array<orosmo::renderer::Mesh, 2> instanceLods_;
    std::unique_ptr<orosmo::renderer::InstanceRenderer> instanceRenderer_;
    orosmo::renderer::ObjectSet instanceObjects_;
    orosmo::renderer::InstanceBatchBuilder instanceBatches_;
    orosmo::renderer::VisibilityStats instanceVisibility_;
    orosmo::renderer::InstanceDrawStats instanceDrawStats_;
    std::uint64_t instanceFrames_ = 0;

    std::vector<float> heroVertices_;
    glm::vec3 heroPosition_{0.0f, 2.85f, 0.0f};
    float elapsedSeconds_ = 0.0f;
    bool initialized_ = false;

    bool lightingInitialized_ = false;
    float lastSunElevation_ = 0.0f;
    float lastSunAzimuth_ = 0.0f;
    float lastSunIntensity_ = 0.0f;
    orosmo::core::Vec3 lastSunColor_{0.0f};
    float lastAmbientStrength_ = 0.0f;
    orosmo::core::Vec3 lastAmbientColor_{0.0f};

    bool accentInitialized_ = false;
    orosmo::core::Vec3 lastAccentColor_{0.0f};
};

} // namespace demo

template <> struct orosmo::core::EnumNames<demo::ProjectionMode>
{
    static constexpr std::array<const char *, 2> names{"Perspective", "Orthographic"};
};

template <> struct orosmo::core::Reflect<demo::DemoSceneParams>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using P = demo::DemoSceneParams;
        return std::make_tuple(
            field("animate", &P::animate, {}, "Animate rotating crystal and orbiting prisms"),
            field("animationSpeed", &P::animationSpeed, range(0.0, 4.0), "Animation playback speed multiplier"),
            field("wireframe", &P::wireframe, {}, "Overlay triangle wireframe on scene meshes"),
            field("debugDraw", &P::debugDraw, {}, "Draw world axes, hero bounds, and sun direction ray"),
            field("cameraRelative", &P::cameraRelative, {}, "Render geometry in camera-relative coordinates"),
            field("fovDegrees", &P::fovDegrees, range(30.0, 110.0), "Vertical field of view in degrees"),
            field("projection", &P::projection, {}, "Camera projection mode"),
            field("renderScale", &P::renderScale, range(0.25, 1.0), "Internal 3D scene resolution scale"),
            field("sunElevation", &P::sunElevation, range(5.0, 89.0), "Directional sun elevation in degrees"),
            field("sunAzimuth", &P::sunAzimuth, range(0.0, 360.0), "Directional sun azimuth in degrees"),
            field("sunIntensity", &P::sunIntensity, range(0.0, 2.5), "Directional sun intensity"),
            field("sunColor", &P::sunColor, {}, "Directional sun RGB color"),
            field("ambientStrength", &P::ambientStrength, range(0.0, 1.0), "Ambient light strength"),
            field("ambientColor", &P::ambientColor, {}, "Ambient light RGB color"),
            field("accentColor", &P::accentColor, {}, "Accent RGB color of the central crystal"));
    }
};
