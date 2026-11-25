// --scene anim (M6, démo d'ANIM-01 à 04 et d'ASSET-01/05) : un personnage skinné et animé, généré par le code (A70 : aucun gros
// binaire), écrit en glTF (`assets::saveGltf`) puis relu (`assets::loadGltf`) pour passer par le chemin d'import réel ; il est joué
// par une `anim::StateMachine` (deux clips, fondus) et déformé en compute (`anim::SkinnedModel`). Mille agents du même modèle, chacun
// à sa phase, sont dessinés par un `anim::CrowdRenderer` : un draw call instancié par primitive skinnée (un lot). La texture de
// couleur est un PNG écrit à côté du glTF et chargé par le `assets::TextureCache` : le modifier sur le disque met à jour les deux
// modèles en direct (`reloadChanged`).
//
// Capacités (règle 11) : sans compute ni tampons de stockage, le personnage n'est pas déformé (pose de repos, message dans le
// journal) ; sans tampons de stockage lus par le vertex shader, la foule est absente (message dans le journal).
#pragma once

#include "orosmo/anim/clip.hpp"
#include "orosmo/anim/crowd.hpp"
#include "orosmo/anim/skinning.hpp"
#include "orosmo/anim/state_machine.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/assets/render_model.hpp"
#include "orosmo/assets/texture_cache.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/material.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace demo
{

// Forme du personnage généré : un tronc cylindrique effilé de `rings` anneaux de `sides` côtés, porté par une chaîne de `joints`
// os, coiffé d'un cône (seconde primitive, second matériau) ; deux clips en boucle (« sway » autour de Z, « wave » autour de X).
struct AnimFigureDesc
{
    std::uint32_t sides = 12;
    std::uint32_t rings = 24;
    std::uint32_t joints = 4;
    float height = 1.6f;
    float radius = 0.2f;
};

// Modèle généré (déterministe) ; `texture` : chemin de l'image de couleur du tronc (image externe du glTF).
orosmo::assets::Model makeAnimatedFigure(const AnimFigureDesc &desc, const std::filesystem::path &texture);
// Sommets du modèle généré (tronc + coiffe).
std::size_t animatedFigureVertexCount(const AnimFigureDesc &desc) noexcept;
// Image de couleur du tronc (bandes horizontales, 64x64) écrite en PNG ; `variant` change les couleurs (même taille).
orosmo::core::Result<> writeFigureTexture(const std::filesystem::path &path, std::uint32_t variant);

struct AnimSceneOptions
{
    std::filesystem::path workDirectory; // reçoit figure.gltf, figure.bin et figure_albedo.png (créé si absent)
    std::uint32_t agents = 1000;
    AnimFigureDesc figure{};
    std::vector<orosmo::renderer::Define> modelDefines; // matériaux du personnage : AOV et forward+ de la scène
    std::vector<orosmo::renderer::Define> crowdDefines; // foule : forward+ seulement (pas d'AOV, G199)
    glm::vec3 heroPosition{0.6f, 0.0f, 4.4f};
    float heroScale = 1.6f;
    glm::vec3 crowdCenter{0.0f, 0.0f, -2.6f};
    glm::vec2 crowdExtent{11.0f, 6.0f}; // largeur (X) et profondeur (Z) de la grille des agents
    float agentScale = 0.34f;
};

class AnimScene
{
  public:
    AnimScene() = default;
    ~AnimScene();
    AnimScene(const AnimScene &) = delete;
    AnimScene &operator=(const AnimScene &) = delete;

    // Écrit puis relit le glTF, crée les modèles, la machine à états, le skinning et la foule. `device`, `library` et `pipelines`
    // doivent survivre à l'objet.
    orosmo::core::Result<> init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &library, orosmo::renderer::PipelineCache &pipelines,
                                const std::filesystem::path &shaderDirectory, AnimSceneOptions options);
    // Avance la machine à états et les poses des agents de `dt` secondes (temps fourni : déterministe).
    orosmo::core::Result<> update(float dt);
    // Skinning en compute du personnage, hors passe : à enregistrer avant `SceneRenderer::beginFrame`, dans la même soumission.
    orosmo::core::Result<> dispatch(orosmo::rhi::CommandEncoder &encoder);
    // Entre `SceneRenderer::setCamera` (et ses lumières) et `endFrame`.
    orosmo::core::Result<> render(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera);
    // ASSET-05 : relit la texture si le fichier a changé ; renvoie le nombre de textures rechargées. Si sa taille change (texture
    // recréée), les modèles et la foule sont reconstruits pour lier la nouvelle texture.
    std::size_t reloadChanged();
    void destroy();

    const orosmo::assets::Model &model() const noexcept { return model_; }
    const std::filesystem::path &modelPath() const noexcept { return modelPath_; }
    const std::filesystem::path &texturePath() const noexcept { return texturePath_; }
    bool heroSkinned() const noexcept { return skinned_.has_value(); }
    bool crowdActive() const noexcept { return crowd_ != nullptr; }
    std::size_t agentCount() const noexcept { return crowd_ ? crowd_->agentCount() : 0; }
    // Primitives skinnées dessinées par la foule (un lot, donc un draw call, chacune).
    std::size_t crowdBatchCount() const noexcept { return crowd_ ? crowd_->batchCount() : 0; }
    // Compteurs du dernier `render`.
    const orosmo::anim::CrowdDrawStats &crowdStats() const noexcept { return crowdStats_; }
    std::size_t heroDraws() const noexcept { return hero_ ? hero_->drawCount() : 0; }
    const orosmo::anim::StateMachine *stateMachine() const noexcept { return machine_ ? &*machine_ : nullptr; }

  private:
    orosmo::core::Result<> buildModels();
    orosmo::core::Result<> updateCrowd();

    orosmo::rhi::Device *device_ = nullptr;
    orosmo::renderer::ShaderLibrary *library_ = nullptr;
    orosmo::renderer::PipelineCache *pipelines_ = nullptr;
    std::filesystem::path shaderDirectory_;
    AnimSceneOptions options_;
    std::filesystem::path modelPath_;
    std::filesystem::path texturePath_;

    orosmo::assets::Model model_;
    std::unique_ptr<orosmo::assets::TextureCache> textures_;
    std::unique_ptr<orosmo::renderer::PbrShaderCache> pbrShaders_;
    std::shared_ptr<orosmo::renderer::PbrDefaults> defaults_;
    std::vector<orosmo::anim::Clip> clips_;
    std::optional<orosmo::anim::StateMachine> machine_;
    // Ordre de destruction : skinning et foule avant les modèles qu'ils référencent.
    std::unique_ptr<orosmo::assets::RenderModel> hero_;
    std::unique_ptr<orosmo::assets::RenderModel> crowdModel_;
    std::optional<orosmo::anim::SkinnedModel> skinned_;
    std::unique_ptr<orosmo::anim::CrowdRenderer> crowd_;
    orosmo::rhi::TextureHandle albedoHandle_;
    bool rebuildPending_ = false; // buildModels a échoué après un rechargement : à retenter (modification du fichier ou délai)
    int retryCountdown_ = 0;

    std::vector<glm::mat4> agentWorlds_;
    std::vector<orosmo::anim::Pose> agentPoses_;
    std::vector<float> agentPhase_;
    std::vector<float> agentSpeed_;
    float time_ = 0.0f;
    float nextSwitch_ = 3.0f;
    bool waving_ = false;
    orosmo::anim::CrowdDrawStats crowdStats_;
    bool statsLogged_ = false;
};

} // namespace demo
