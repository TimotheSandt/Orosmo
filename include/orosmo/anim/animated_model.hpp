// Modèle animé prêt à jouer (A75.2, M8-L12a) : un fichier glTF skinné et animé, son `assets::RenderModel`, ses clips, un lecteur
// (clip courant, fondu enchaîné) et la déformation des sommets, en une fabrique. Remplace la dizaine d'objets à créer et relier dans
// un ordre précis (`Model`, `TextureCache`, `PbrShaderCache`, `PbrDefaults`, `RenderModel`, `Clip`s, `StateMachine`, `SkinnedModel`).
//
//     auto context = renderer::RenderContext::create(device, shaderDirectory);
//     auto hero = anim::AnimatedModel::create(*context, "hero.gltf");   // puis, une fois :
//     (*hero)->play("walk");
//     (*hero)->update(dt);               // chaque image : avance le lecteur, pose envoyée
//     (*hero)->dispatch(encoder);        // hors passe, avant SceneRenderer::beginFrame (skinning GPU ; rien en repli CPU)
//     (*hero)->draw(scene, camera);      // entre setCamera et endFrame
//
// Skinning (règle 11) : en compute (`SkinnedModel`) si le périphérique le permet ; sinon repli CPU (`skinVertices`, sommets
// réécrits par `Mesh::updateVertices` à chaque pose), même résultat à 1e-4 près. `AnimatedModelOptions::forceCpuSkinning` force
// le repli. Un maillage dessiné par plusieurs nœuds skinnés n'est déformé que pour le premier (comme `SkinnedModel`, G250).
//
// Lecteur : `play` coupe net, `crossFade` mélange la pose du clip quitté (qui continue d'avancer) vers celle du nouveau pendant
// `seconds` secondes ; même calcul que `StateMachine` (le nouveau clip part de 0, poids = temps écoulé / durée). Pour piloter la
// pose autrement (machine à états, couches), `setPose` l'impose directement. Pas d'événements de clip (G251).
#pragma once

#include "orosmo/anim/clip.hpp"
#include "orosmo/anim/pose.hpp"
#include "orosmo/anim/skinning.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/assets/model_loader.hpp"
#include "orosmo/assets/render_model.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/scene_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::anim
{

struct AnimatedModelOptions
{
    // Options du modèle dessinable (ombres, définitions de shader, clé de cache, scène) ; `skinnable` est choisi par la fabrique.
    assets::RenderModelOptions render{};
    bool forceCpuSkinning = false; // repli CPU même si le compute est disponible (tests, comparaison)
};

struct PlayOptions
{
    LoopMode loop = LoopMode::kLoop;
    float speed = 1.0f;
};

enum class SkinningPath : std::uint8_t
{
    kGpu, // compute (SkinnedModel)
    kCpu, // skinVertices + Mesh::updateVertices
};

class AnimatedModel
{
  public:
    // Charge `path` avec un chargeur propre au modèle. Le contexte doit survivre à l'objet.
    static core::Result<std::unique_ptr<AnimatedModel>> create(const renderer::RenderContext &context, const std::filesystem::path &path,
                                                               const AnimatedModelOptions &options = {});
    // Même chose avec un chargeur partagé (textures et programmes communs à plusieurs modèles) ; `loader` doit survivre à l'objet.
    static core::Result<std::unique_ptr<AnimatedModel>> create(const renderer::RenderContext &context, assets::ModelLoader &loader,
                                                               const std::filesystem::path &path, const AnimatedModelOptions &options = {});
    // Depuis un modèle déjà en mémoire (chargeur partagé).
    static core::Result<std::unique_ptr<AnimatedModel>> create(const renderer::RenderContext &context, assets::ModelLoader &loader, assets::Model model,
                                                               const AnimatedModelOptions &options = {});

    AnimatedModel(const AnimatedModel &) = delete;
    AnimatedModel &operator=(const AnimatedModel &) = delete;
    ~AnimatedModel();

    // Joue `clip` (nom de l'animation glTF) depuis 0, sans fondu. kNotFound si le clip n'existe pas.
    core::Result<> play(std::string_view clip, PlayOptions options = {});
    // Fondu enchaîné de `seconds` secondes vers `clip` (0 : comme `play`). Sans clip en cours, comme `play`. kNotFound.
    core::Result<> crossFade(std::string_view clip, float seconds, PlayOptions options = {});
    // Avance le lecteur de `dt` secondes (temps fourni : déterministe) et envoie la pose. Sans clip joué : pose de repos.
    // kInvalidArgument pour un `dt` non fini (comme une vitesse ou une durée de fondu non finie dans `play` / `crossFade`).
    core::Result<> update(float dt);
    // Impose une pose (une transformation par nœud du modèle) ; le lecteur est arrêté (le prochain `play` le relance).
    core::Result<> setPose(const Pose &pose);

    // Skinning GPU : enregistre les dispatchs (hors passe de rendu), à soumettre avant les dessins. Rien en repli CPU.
    core::Result<> dispatch(rhi::CommandEncoder &encoder);
    // Un dessin par (nœud, primitive) ; entre `SceneRenderer::setCamera` et `endFrame`.
    // Rien n'est dessiné tant que `texturesStale()` (une texture liée a été remplacée et le modèle pas encore reconstruit).
    core::Result<> draw(renderer::SceneRenderer &scene, const renderer::Camera &camera);
    // Place tout le modèle dans le monde.
    void setTransform(const glm::mat4 &world) noexcept;

    // ASSET-05 : relit les textures modifiées sur le disque ; si une texture du modèle a été recréée (taille ou format changé),
    // le modèle dessinable est reconstruit (le lecteur garde son état). La reconstruction suit `texturesStale()`, pas le compte du
    // cache : un modèle qui partage son chargeur est reconstruit même si un autre a déjà relu le fichier, et une reconstruction
    // qui a échoué est retentée à l'appel suivant. Renvoie le nombre de textures relues par cet appel. Une foule créée depuis ce
    // modèle (`CrowdRenderer::create(context, model, …)`, qui a son propre modèle dessinable) n'est PAS reconstruite : à recréer
    // par l'appelant après un rechargement qui a recréé une texture (G402).
    core::Result<std::size_t> reloadChanged();
    // Vrai si une texture liée par les matériaux n'est plus celle du dernier modèle dessinable installé (texture recréée par le
    // cache) : à reconstruire (`rebuild` ou `reloadChanged`) avant de dessiner.
    bool texturesStale() const;
    // Recrée le modèle dessinable et le skinning (même pose, même lecteur), ex. après un changement de textures ou d'ombres.
    core::Result<> rebuild();

    SkinningPath skinningPath() const noexcept { return skinned_ ? SkinningPath::kGpu : SkinningPath::kCpu; }
    const assets::Model &model() const noexcept { return model_; }
    assets::RenderModel &renderModel() noexcept { return *render_; }
    const assets::RenderModel &renderModel() const noexcept { return *render_; }
    // Chargeur du modèle (le sien ou celui passé à `create`).
    assets::ModelLoader &loader() const noexcept { return *loader_; }
    const AnimatedModelOptions &options() const noexcept { return options_; }
    const std::vector<Clip> &clips() const noexcept { return clips_; }
    // Indice du clip de ce nom, ou -1.
    int clipIndex(std::string_view name) const noexcept;
    const Pose &pose() const noexcept { return pose_; }
    // Clip joué (vide si aucun) et son temps local ; fondu en cours et poids du clip joué (1 hors fondu).
    const std::string &current() const noexcept;
    float currentTime() const noexcept { return currentTime_; }
    bool inTransition() const noexcept { return previous_.has_value(); }
    float blendWeight() const noexcept;
    // Os déformés (sommets) par image.
    std::size_t skinnedVertexCount() const noexcept;

  private:
    AnimatedModel() = default;
    struct Playing
    {
        int clip = -1;
        float time = 0.0f;
        PlayOptions options;
    };
    struct CpuEntry
    {
        std::size_t mesh = 0; // maillage GPU du RenderModel
        int node = -1;
        int skin = -1;
        std::vector<float> rest;
        std::vector<glm::u16vec4> joints;
        std::vector<core::Vec4> weights;
    };
    core::Result<> buildRender();
    core::Result<> evaluate();
    core::Result<> applyPose();

    const renderer::RenderContext *context_ = nullptr;
    std::unique_ptr<assets::ModelLoader> ownedLoader_;
    assets::ModelLoader *loader_ = nullptr;
    AnimatedModelOptions options_;
    assets::Model model_;
    std::vector<int> parents_;
    std::vector<Clip> clips_;
    Pose rest_;
    Pose pose_;
    glm::mat4 transform_{1.0f};

    int currentClip_ = -1;
    float currentTime_ = 0.0f;
    PlayOptions currentOptions_;
    std::optional<Playing> previous_;
    float fadeElapsed_ = 0.0f;
    float fadeDuration_ = 0.0f;

    // Ordre de destruction : skinning avant le modèle dessinable qu'il référence.
    std::unique_ptr<assets::RenderModel> render_;
    std::optional<SkinnedModel> skinned_;
    std::vector<CpuEntry> cpu_;
    std::vector<rhi::TextureHandle> installedTextures_; // textures liées par le modèle dessinable installé
};

} // namespace orosmo::anim
