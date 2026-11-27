// Système de rendu de la scène (A75.1, A73 point 3, A75.2 ; A79 lot L13) : dessine les composants de rendu
// (`render_components.hpp`) d'un `scene::World` avec un `renderer::SceneRenderer`. Utilisable seul (code bas niveau) ou par
// `app::Application`, qui l'appelle dans cet ordre à chaque image :
//
//     system->update(dt);               // transformations monde, lecteurs des modèles animés, lumières -> LightManager (envoi)
//     system->applyCamera(camera);      // caméra active de la scène, s'il y en a une
//     system->dispatch(encoder);        // skinning en compute, HORS passe : avant SceneRenderer::beginFrame (A75.2)
//     scene.beginFrame(encoder); scene.setCamera(camera);
//     system->draw(scene, camera);      // dessins (seuls, instanciés, foules)
//     scene.endFrame(...);
//
// Ressources : les composants tiennent des `core::Handle<T>` vers les réserves de `SceneResources`, qui possèdent les objets.
// Une poignée périmée est ignorée (comptée dans `SceneRenderStats::skipped`). Retirer une ressource de la réserve hors d'une
// image (entre `endFrame` et le `update` suivant) : le système oublie ses casters d'ombres et sa foule au `draw` suivant.
//
// Ombres : avec un `renderer::ShadowFeature` (facultatif), un `castShadows` inscrit le maillage (ou ceux du modèle animé) comme
// caster mobile ; le caster suit la DERNIÈRE transformation donnée au maillage dans l'image (un maillage partagé par plusieurs
// entités projette à la place de la dernière, G353). La feature lit les lumières du système (`lights()`).
#pragma once

#include <orosmo/app/render_components.hpp>
#include <orosmo/app/resource_pool.hpp>
#include <orosmo/core/error.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/render_context.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/world.hpp>

#include <cstdint>
#include <memory>

namespace orosmo::renderer
{
class Mesh;
class ShadowFeature;
} // namespace orosmo::renderer
namespace orosmo::anim
{
class AnimatedModel;
}

namespace orosmo::app
{

// Réserves des ressources désignées par les composants. Ordre de vie : après (détruites avant) le chargeur de modèles et le
// contexte de rendu qui les ont servies ; un `SceneRenderSystem` qui les lit doit être détruit avant elles.
struct SceneResources
{
    SceneResources();
    ~SceneResources();
    SceneResources(const SceneResources &) = delete;
    SceneResources &operator=(const SceneResources &) = delete;

    ResourcePool<renderer::Mesh> meshes;
    ResourcePool<anim::AnimatedModel> animatedModels;
};

// Types des composants de rendu dans un monde.
struct RenderComponentTypes
{
    scene::ComponentType<MeshRenderer> mesh;
    scene::ComponentType<LightComponent> light;
    scene::ComponentType<CameraComponent> camera;
    scene::ComponentType<AnimatedModelComponent> animatedModel;
};

// Enregistre (ou retrouve) les quatre types sous leurs noms (`kMeshRendererName`...). Erreurs de `World::registerComponent`
// (kAlreadyExists : un type du même nom avec une autre disposition).
core::Result<RenderComponentTypes> registerRenderComponents(scene::World &world);

// Ce qu'a fait le dernier `draw` (tests, mesure).
struct SceneRenderStats
{
    std::uint32_t meshDraws = 0;        // dessins `kSingle` de maillages
    std::uint32_t instancedBatches = 0; // dessins instanciés (un par maillage `kInstanced`)
    std::uint32_t instances = 0;        // entités dessinées par ces lots
    std::uint32_t animatedDraws = 0;    // dessins `kSingle` de modèles animés
    std::uint32_t crowds = 0;           // foules dessinées (une par modèle `kCrowd`)
    std::uint32_t crowdAgents = 0;
    std::uint32_t lights = 0;      // lumières envoyées par le dernier `update` (ambiante non comptée)
    std::uint32_t skipped = 0;     // poignée vide ou périmée, clip hors limites, foule non prise en charge
    std::uint32_t shadowCasters = 0; // casters inscrits par le système
};

class SceneRenderSystem
{
  public:
    // `world`, `resources` et `context` doivent survivre au système ; `shadows` (facultatif) aussi. Enregistre les types de
    // composants dans `world` (registerRenderComponents) et crée le gestionnaire de lumières.
    static core::Result<std::unique_ptr<SceneRenderSystem>> create(const renderer::RenderContext &context, scene::World &world,
                                                                   SceneResources &resources, renderer::ShadowFeature *shadows = nullptr);
    ~SceneRenderSystem();
    SceneRenderSystem(const SceneRenderSystem &) = delete;
    SceneRenderSystem &operator=(const SceneRenderSystem &) = delete;

    // Avant l'image : `World::updateTransforms`, lecteurs des modèles `kSingle` avancés de `dt` secondes (une fois par modèle),
    // lumières du monde recopiées dans `lights()` puis envoyées si elles ont changé. kInvalidArgument : `dt` non fini.
    core::Result<> update(float dt);
    // Recopie la première caméra active du monde (position, direction, haut, champ, plans) dans `camera` ; faux s'il n'y en a pas
    // (`camera` inchangée). L'aspect reste celui de `camera`.
    bool applyCamera(renderer::Camera &camera) const;
    // Skinning GPU des modèles `kSingle` dessinés (une fois par modèle), hors passe : avant `SceneRenderer::beginFrame`.
    core::Result<> dispatch(rhi::CommandEncoder &encoder);
    // Dessins de l'image, entre `SceneRenderer::setCamera` et `endFrame`.
    core::Result<SceneRenderStats> draw(renderer::SceneRenderer &scene, const renderer::Camera &camera);

    renderer::LightManager &lights() noexcept;
    const RenderComponentTypes &types() const noexcept;
    const SceneRenderStats &stats() const noexcept;
    scene::World &world() const noexcept;

    struct Impl;

  private:
    explicit SceneRenderSystem(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace orosmo::app
