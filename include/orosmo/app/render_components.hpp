// Composants de rendu de la scène (A75.1, A73 point 3 ; A79 lot L13), définis par le MOTEUR et rendus par
// `SceneRenderSystem` (scene_render_system.hpp). Ils se placent sur une entité qui a un `scene::Transform` (position, rotation,
// échelle : partie monde calculée par `World::updateTransforms`).
//
// Comme tout composant d'un `scene::World`, ce sont des données trivialement copiables décrites par la réflexion : ils tiennent
// leurs ressources par `core::Handle<T>` (validation d'A79, choix 2), jamais par pointeur, et la poignée désigne un objet d'une
// `ResourcePool` (`SceneResources`). Ils se sauvegardent avec la scène, mais une poignée n'a de sens que pour la réserve qui l'a
// donnée, dans la même exécution (G351).
//
// Mode de dessin (A73 point 3) : propriété du composant, pas du code qui dessine.
//   - `MeshRenderer` : `kSingle` (un dessin par entité, `SceneRenderer::drawMesh`) ou `kInstanced` (toutes les entités du même
//     maillage en UN dessin instancié, `renderer::InstanceRenderer` ; `kCrowd` est lu comme `kInstanced`).
//   - `AnimatedModelComponent` : `kSingle` (le modèle et son lecteur, dessiné à la place de chaque entité) ou `kCrowd` (toutes les
//     entités du même modèle en une foule, `anim::CrowdRenderer`, chacune avec son clip et son temps ; `kInstanced` est lu comme
//     `kCrowd`).
#pragma once

#include <orosmo/core/math.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/resources.hpp>
#include <orosmo/renderer/light.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>

namespace orosmo::renderer
{
class Mesh;
}
namespace orosmo::anim
{
class AnimatedModel;
}

namespace orosmo::app
{

enum class DrawMode : std::uint8_t
{
    kSingle,    // un dessin par entité
    kInstanced, // maillages : un dessin instancié par maillage
    kCrowd,     // modèles animés : une foule par modèle
};

// Maillage dessiné à la transformation de l'entité. `kInstanced` : maillage à la disposition colorée (`MeshLayout::kColored`,
// programme `mesh_instanced.vert` + `mesh.frag`) ; un maillage qui porte un matériau PBR est dessiné en `kSingle` (G352).
struct MeshRenderer
{
    core::Handle<renderer::Mesh> mesh{};
    DrawMode mode = DrawMode::kSingle;
    bool visible = true;
    bool castShadows = false; // caster mobile du plugin d'ombres de l'application (s'il y en a un)
};

// Lumière. Position : celle de l'entité (Transform monde) ; `direction` (sens de PROPAGATION, comme les fabriques d'A75.10) est
// tournée par la rotation monde de l'entité. Ambiante : `color` × `intensity`, la dernière entité ambiante l'emporte.
struct LightComponent
{
    renderer::LightType type = renderer::LightType::kDirectional;
    core::Vec3 color{1.0f};
    float intensity = 1.0f;
    core::Vec3 direction{0.0f, -1.0f, 0.0f};
    float range = 10.0f;             // ponctuelle, projecteur (m) ; infinie : sans atténuation
    float outerAngleDegrees = 30.0f; // projecteur
    float innerAngleDegrees = 25.0f;
    bool castShadows = false; // `ShadowMode::kDynamic` (ombres de l'application activées)
};

// Caméra : position de l'entité ; `forward` et `up` tournés par sa rotation monde. La première caméra `active` (ordre dense du
// stockage) remplace, à chaque image, la caméra de l'application (`Application::camera`).
struct CameraComponent
{
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f; // fini : une scène se sauvegarde en JSON, sans réel infini
    core::Vec3 forward{0.0f, 0.0f, -1.0f};
    core::Vec3 up{0.0f, 1.0f, 0.0f};
    bool active = true;
};

// Modèle animé (`anim::AnimatedModel` de la réserve). `kSingle` : le lecteur du modèle (`play`, `crossFade`) avance d'un delta
// d'image par image (une fois par modèle, même s'il est dessiné par plusieurs entités) ; `clip` et `time` ne servent pas.
// `kCrowd` : chaque entité joue le clip d'indice `clip` (`AnimatedModel::clips()`) au temps `time` (secondes, bouclé), que la
// logique du jeu fait avancer ; le rendu ne fait que les lire.
struct AnimatedModelComponent
{
    core::Handle<anim::AnimatedModel> model{};
    DrawMode mode = DrawMode::kSingle;
    std::uint32_t clip = 0;
    float time = 0.0f;
    bool visible = true;
    bool castShadows = false;
};

// Noms d'enregistrement dans le `scene::World` (registerRenderComponents).
inline constexpr std::string_view kMeshRendererName = "MeshRenderer";
inline constexpr std::string_view kLightComponentName = "Light";
inline constexpr std::string_view kCameraComponentName = "Camera";
inline constexpr std::string_view kAnimatedModelComponentName = "AnimatedModel";

} // namespace orosmo::app

// ── Réflexion (champs sauvegardés, inspecteur) ─────────────────────────────────

template <typename T> struct orosmo::core::Reflect<orosmo::core::Handle<T>>
{
    static constexpr auto fields()
    {
        using H = orosmo::core::Handle<T>;
        return std::make_tuple(field("index", &H::index), field("generation", &H::generation));
    }
};

template <> struct orosmo::core::EnumNames<orosmo::app::DrawMode>
{
    static constexpr std::array<const char *, 3> names{"single", "instanced", "crowd"};
};

template <> struct orosmo::core::EnumNames<orosmo::renderer::LightType>
{
    static constexpr std::array<const char *, 5> names{"none", "ambient", "directional", "point", "spot"};
};

template <> struct orosmo::core::Reflect<orosmo::app::MeshRenderer>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using T = orosmo::app::MeshRenderer;
        return std::make_tuple(field("mesh", &T::mesh), field("mode", &T::mode), field("visible", &T::visible),
                               field("castShadows", &T::castShadows));
    }
};

template <> struct orosmo::core::Reflect<orosmo::app::LightComponent>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using T = orosmo::app::LightComponent;
        return std::make_tuple(field("type", &T::type), field("color", &T::color), field("intensity", &T::intensity, range(0.0, 1e6)),
                               field("direction", &T::direction, {}, "propagation direction, rotated by the entity"),
                               field("range", &T::range), field("outerAngleDegrees", &T::outerAngleDegrees, range(0.0, 180.0)),
                               field("innerAngleDegrees", &T::innerAngleDegrees, range(0.0, 180.0)),
                               field("castShadows", &T::castShadows));
    }
};

template <> struct orosmo::core::Reflect<orosmo::app::CameraComponent>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using T = orosmo::app::CameraComponent;
        return std::make_tuple(field("fovDegrees", &T::fovDegrees, range(1.0, 179.0)), field("nearPlane", &T::nearPlane),
                               field("farPlane", &T::farPlane), field("forward", &T::forward), field("up", &T::up),
                               field("active", &T::active));
    }
};

template <> struct orosmo::core::Reflect<orosmo::app::AnimatedModelComponent>
{
    static constexpr int kVersion = 1;
    static constexpr auto fields()
    {
        using T = orosmo::app::AnimatedModelComponent;
        return std::make_tuple(field("model", &T::model), field("mode", &T::mode), field("clip", &T::clip), field("time", &T::time),
                               field("visible", &T::visible), field("castShadows", &T::castShadows));
    }
};
