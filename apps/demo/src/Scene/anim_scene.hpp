// --scene anim (M6, démo d'ANIM-01 à 04 et d'ASSET-01/05) : un personnage skinné et animé, généré par le code (A70 : aucun gros
// binaire), écrit en glTF (`assets::saveGltf`) puis relu (`assets::ModelLoader`) pour passer par le chemin d'import réel ; c'est un
// `anim::AnimatedModel` (M8-L12a, A75.2) qui alterne ses deux clips par fondus (`crossFade`) et se déforme en compute. Mille agents du
// même modèle, chacun à sa phase, sont dessinés par un `anim::CrowdRenderer` créé depuis le modèle (A75.11) : un draw call instancié
// par primitive skinnée (un lot). La texture de couleur est un PNG écrit à côté du glTF et chargé par le cache de textures du
// chargeur : le modifier sur le disque met à jour les deux modèles en direct (`reloadChanged`).
//
// Capacités (règle 11) : sans compute ni tampons de stockage, le personnage est déformé sur le CPU (repli d'`AnimatedModel`) ; sans
// tampons de stockage lus par le vertex shader, la foule est absente (message dans le journal).
#pragma once

#include "orosmo/anim/animated_model.hpp"
#include "orosmo/anim/crowd.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/assets/model_loader.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/camera.hpp"
#include "orosmo/renderer/material.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_context.hpp"
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

    // Écrit puis relit le glTF, crée le modèle animé et la foule. `device`, `library` et `pipelines`
    // doivent survivre à l'objet.
    orosmo::core::Result<> init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &library, orosmo::renderer::PipelineCache &pipelines,
                                const std::filesystem::path &shaderDirectory, AnimSceneOptions options);
    // Avance le modèle animé (fondu entre ses clips toutes les 3 s) et les poses des agents de `dt` secondes (temps fourni : déterministe).
    orosmo::core::Result<> update(float dt);
    // Skinning en compute du personnage, hors passe : à enregistrer avant `SceneRenderer::beginFrame`, dans la même soumission.
    orosmo::core::Result<> dispatch(orosmo::rhi::CommandEncoder &encoder);
    // Entre `SceneRenderer::setCamera` (et ses lumières) et `endFrame`.
    orosmo::core::Result<> render(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera);
    // ASSET-05 : relit la texture si le fichier a changé ; renvoie le nombre de textures rechargées. Si sa taille change (texture
    // recréée), les modèles et la foule sont reconstruits pour lier la nouvelle texture ; tant que la reconstruction est en attente
    // (échec, retentée), ni le personnage ni la foule ne sont dessinés.
    std::size_t reloadChanged();
    void destroy();

    const orosmo::assets::Model &model() const noexcept { return hero_->model(); }
    const std::filesystem::path &modelPath() const noexcept { return modelPath_; }
    const std::filesystem::path &texturePath() const noexcept { return texturePath_; }
    // Personnage déformé en compute (sinon : repli CPU).
    bool heroSkinned() const noexcept { return hero_ && hero_->skinningPath() == orosmo::anim::SkinningPath::kGpu; }
    bool crowdActive() const noexcept { return crowd_ != nullptr; }
    std::size_t agentCount() const noexcept { return crowd_ ? crowd_->agentCount() : 0; }
    // Primitives skinnées dessinées par la foule (un lot, donc un draw call, chacune).
    std::size_t crowdBatchCount() const noexcept { return crowd_ ? crowd_->batchCount() : 0; }
    // Compteurs du dernier `render`.
    const orosmo::anim::CrowdDrawStats &crowdStats() const noexcept { return crowdStats_; }
    std::size_t heroDraws() const noexcept { return hero_ ? hero_->renderModel().drawCount() : 0; }
    const orosmo::anim::AnimatedModel *hero() const noexcept { return hero_.get(); }

  private:
    orosmo::core::Result<> buildCrowd();
    orosmo::core::Result<> updateCrowd();

    AnimSceneOptions options_;
    std::filesystem::path modelPath_;
    std::filesystem::path texturePath_;

    // Ordre de destruction : foule et personnage avant le chargeur (ses textures), le chargeur avant le contexte.
    std::unique_ptr<orosmo::renderer::RenderContext> context_;
    std::unique_ptr<orosmo::assets::ModelLoader> loader_;
    std::unique_ptr<orosmo::anim::AnimatedModel> hero_;
    std::unique_ptr<orosmo::anim::CrowdRenderer> crowd_;
    bool rebuildPending_ = false; // la reconstruction a échoué après un rechargement : à retenter (modification du fichier ou délai)
    int retryCountdown_ = 0;

    std::vector<glm::mat4> agentWorlds_;
    std::vector<std::uint32_t> agentClips_;
    std::vector<float> agentTimes_;
    std::vector<float> agentPhase_;
    std::vector<float> agentSpeed_;
    float time_ = 0.0f;
    float nextSwitch_ = 3.0f;
    bool waving_ = false;
    orosmo::anim::CrowdDrawStats crowdStats_;
    bool statsLogged_ = false;
};

} // namespace demo
