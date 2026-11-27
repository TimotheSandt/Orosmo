// ANIM-04 : foules instanciées. Des centaines ou des milliers d'agents qui partagent un modèle skinné, chacun avec SA pose : le
// skinning se fait dans le vertex shader (shaders/rhi/mesh_pbr_crowd.vert), avec une palette d'os par instance dans un tampon de
// stockage ; chaque primitive skinnée du modèle est UN draw call instancié pour tous les agents (un lot), joué dans la passe
// « scene » du SceneRenderer (drawCustom), avec le matériau PBR du `assets::RenderModel` (mesh_pbr.frag).
//
// Repère : la palette d'une peau est monde(os) × inverseBind (`jointPalette` sans nœud de maillage) dans l'espace du modèle ;
// l'agent la place dans le monde par sa matrice. La transformation du nœud qui porte le maillage est ignorée (règle glTF).
//
// Capacité (règle 11) : tampons de stockage lus par le vertex shader, au groupe 2 (emplacement plat 16 sur OpenGL) ; sinon
// `create` refuse (kUnsupported). Pas de culling par agent, pas d'AOV ni de vecteurs de mouvement (docs/GAPS.md, G199).
#pragma once

#include "orosmo/anim/animated_model.hpp"
#include "orosmo/anim/clip.hpp"
#include "orosmo/anim/pose.hpp"
#include "orosmo/anim/skinning.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/assets/render_model.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace orosmo::anim
{

struct CrowdOptions
{
    std::vector<renderer::Define> extraDefines; // ex. iblDefines(), forwardPlusDefines (pas d'AOV)
};

struct CrowdDrawStats
{
    std::uint32_t drawCalls = 0;
    std::uint32_t instances = 0; // agents × lots
    std::uint64_t paletteBytes = 0;
};

// Disposition d'une instance dans le tampon de sommets (pas d'instance) : matrice monde puis premier os de la palette.
struct CrowdGpuInstance
{
    glm::vec4 model[4];
    glm::uvec4 info; // x : premier os de la palette dans le tampon des palettes
};
static_assert(sizeof(CrowdGpuInstance) == 80);

class CrowdRenderer
{
  public:
    static bool supportedBy(const rhi::Capabilities &capabilities) noexcept;

    // `render` donne les maillages (sommets au repos : ne pas le skinner en même temps par `SkinnedModel`) et les matériaux ; il
    // doit avoir été créé depuis `model` et survivre à l'objet, comme `library` et `pipelines`. Charge mesh_pbr_crowd.vert et
    // mesh_pbr.frag depuis `shaderDirectory`. kInvalidArgument si le modèle n'a aucune primitive skinnée dessinée.
    static core::Result<std::unique_ptr<CrowdRenderer>> create(rhi::Device &device, renderer::ShaderLibrary &library, renderer::PipelineCache &pipelines,
                                                               const std::filesystem::path &shaderDirectory, const assets::Model &model,
                                                               const assets::RenderModel &render, const CrowdOptions &options = {});
    // A75.4 : même fabrique depuis un `renderer::RenderContext`.
    static core::Result<std::unique_ptr<CrowdRenderer>> create(const renderer::RenderContext &context, const assets::Model &model,
                                                               const assets::RenderModel &render, const CrowdOptions &options = {})
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory(), model, render, options);
    }
    // A75.11 : foule d'un modèle animé. Crée son propre `RenderModel` au repos (même chargeur : les images externes sont partagées,
    // les images incluses dans le fichier ont une clé propre à la foule et donc une seconde texture ; définitions
    // `options.extraDefines`, clé `<clé du modèle>.crowd`) et copie les clips, pour `setAgents(worlds, clips, times)`. `context` et
    // le chargeur de `model` doivent survivre à l'objet ; `model` peut être détruit ensuite.
    static core::Result<std::unique_ptr<CrowdRenderer>> create(const renderer::RenderContext &context, const AnimatedModel &model,
                                                               const CrowdOptions &options = {});
    ~CrowdRenderer();
    CrowdRenderer(const CrowdRenderer &) = delete;
    CrowdRenderer &operator=(const CrowdRenderer &) = delete;

    // Os d'une palette d'agent (toutes les peaux du modèle à la suite) ; lots (un draw call chacun).
    std::uint32_t jointCount() const noexcept { return jointCount_; }
    std::size_t batchCount() const noexcept { return batches_.size(); }
    std::size_t agentCount() const noexcept { return agents_; }

    // Palettes d'un agent depuis sa pose (une transformation par nœud du modèle) : `jointCount()` matrices, à la suite.
    core::Result<> agentPalette(const Pose &pose, std::span<core::Mat4> out) const;

    // Agents de l'image : une matrice monde et une pose par agent (palettes calculées ici, sur le CPU, déterministe).
    core::Result<> setAgents(std::span<const core::Mat4> worlds, std::span<const Pose> poses);
    // A75.11 : agents qui jouent chacun un clip du modèle (`clips[i]` : indice dans `AnimatedModel::clips()`) au temps `times[i]`
    // (secondes, bouclé sur la durée du clip ; non fini : kInvalidArgument). Seulement pour une foule créée depuis un
    // `AnimatedModel` (sinon kInvalidArgument).
    core::Result<> setAgents(std::span<const core::Mat4> worlds, std::span<const std::uint32_t> clips, std::span<const float> times);
    // Variante bas niveau : palettes déjà calculées (`worlds.size() × jointCount()` matrices, agent par agent), ex. partagées par
    // des agents à la même pose ou calculées sur plusieurs fils.
    core::Result<> setAgentPalettes(std::span<const core::Mat4> worlds, std::span<const core::Mat4> palettes);

    // Enregistre un draw call instancié par lot dans `scene` (entre beginFrame, setCamera et endFrame). Une fois par image : les
    // tampons sont réécrits par le `setAgents` suivant.
    core::Result<CrowdDrawStats> draw(renderer::SceneRenderer &scene, bool wireframe = false);

  private:
    CrowdRenderer(rhi::Device &device, renderer::PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}
    core::Result<> ensureBuffer(renderer::GpuBuffer &buffer, std::uint64_t bytes, rhi::BufferUsage usage, const char *name, bool &recreated);

    struct Batch
    {
        const renderer::Mesh *mesh = nullptr;
        std::shared_ptr<renderer::ShaderProgram> program;
        rhi::BindGroupHandle material;
        rhi::CullMode cull = rhi::CullMode::kNone;
        renderer::GpuBuffer influences; // uvec4 os (relatifs à la peau + début de la peau) + vec4 poids par sommet
    };
    rhi::Device &device_;
    renderer::PipelineCache &pipelines_;
    std::unique_ptr<assets::RenderModel> ownedRender_; // A75.11 : modèle au repos propre à la foule (détruit après les lots)
    std::vector<Clip> clips_;                          // A75.11 : clips du modèle animé
    Pose rest_;
    std::vector<Pose> poses_; // travail de setAgents(worlds, clips, times)
    std::vector<int> parents_;
    std::vector<assets::SkinData> skins_;
    std::vector<std::uint32_t> skinOffsets_; // premier os de chaque peau dans la palette d'un agent
    std::uint32_t jointCount_ = 0;
    std::vector<Batch> batches_;
    std::size_t agents_ = 0;
    std::vector<core::Mat4> worlds_;   // matrices des agents (envoyées au draw, relatives à la caméra)
    std::vector<core::Mat4> palettes_; // travail
    std::vector<CrowdGpuInstance> scratch_;
    renderer::GpuBuffer paletteBuffer_;
    renderer::GpuBuffer instanceBuffer_;
    rhi::BindGroupHandle paletteGroup_;
};

} // namespace orosmo::anim
