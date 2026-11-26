// ANIM-01 : squelettes et skinning GPU. Jusqu'à 4 os par sommet (JOINTS_0 / WEIGHTS_0 de glTF) ; les matrices des os
// (palette) sont dans un tampon de stockage ; un compute (shaders/rhi/skin.comp) écrit les sommets déformés (position, normale,
// tangente) dans le tampon de sommets du maillage, que le dessin lit ensuite sans autre changement (mesh_pbr.vert).
//
// Repère (glTF) : la transformation du nœud qui porte un maillage skinné est ignorée ; la palette vaut
// monde(nœud)⁻¹ × monde(os) × inverseBind, et `RenderModel` multiplie ensuite par monde(nœud) : net, monde(os) × inverseBind.
// Les normales et tangentes suivent mat3(palette) puis sont normalisées : exact pour des os sans échelle non uniforme.
//
// Capacité (règle 11) : compute et tampons de stockage. Sans eux, `SkinnedModel::create` refuse (kUnsupported).
#pragma once

#include "orosmo/anim/pose.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/assets/render_model.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace orosmo::anim
{

inline constexpr std::uint32_t kMaxJointsPerVertex = 4;
inline constexpr std::uint32_t kSkinWorkgroupSize = 64;

// Palette d'une peau pour un maillage porté par `meshNode` (voir le repère ci-dessus). `world` : matrices monde de tous les nœuds.
std::vector<core::Mat4> jointPalette(const assets::SkinData &skin, std::span<const core::Mat4> world, int meshNode);

// Référence CPU du compute (tests, outils) : sommets PBR entrelacés (`renderer::kPbrVertexFloats` flottants) déformés.
// kInvalidArgument si les tailles ne concordent pas ou si un indice d'os dépasse la palette.
core::Result<std::vector<float>> skinVertices(std::span<const float> rest, std::span<const glm::u16vec4> joints, std::span<const core::Vec4> weights,
                                              std::span<const core::Mat4> palette);

class SkinnedModel
{
  public:
    static bool supportedBy(const rhi::Capabilities &capabilities) noexcept;

    // `render` doit avoir été créé depuis `model` avec `RenderModelOptions::skinnable` ; `render`, `library` et `pipelines`
    // doivent survivre à l'objet. Charge `skin.comp` depuis `shaderDirectory`. Un maillage GPU dessiné par plusieurs nœuds est
    // déformé pour le premier d'entre eux (docs/GAPS.md).
    static core::Result<SkinnedModel> create(rhi::Device &device, renderer::ShaderLibrary &library, renderer::PipelineCache &pipelines,
                                             const std::filesystem::path &shaderDirectory, const assets::Model &model, assets::RenderModel &render);
    // A75.4 : même fabrique depuis un `renderer::RenderContext`.
    static core::Result<SkinnedModel> create(const renderer::RenderContext &context, const assets::Model &model, assets::RenderModel &render)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory(), model, render);
    }

    // Pose (une transformation par nœud du modèle) → matrices monde du `RenderModel` et palettes envoyées (copie ordonnée : la
    // prochaine soumission les voit).
    core::Result<> setPose(const Pose &pose);
    // Enregistre les dispatchs (hors passe de rendu), à soumettre avant les dessins du modèle.
    core::Result<> dispatch(rhi::CommandEncoder &encoder);

    std::size_t skinnedMeshCount() const noexcept { return entries_.size(); }
    // Total des sommets déformés par `dispatch`.
    std::size_t skinnedVertexCount() const noexcept;

    SkinnedModel(SkinnedModel &&) noexcept = default;
    SkinnedModel &operator=(SkinnedModel &&) = delete; // l'objet remplacé garderait ses groupes
    ~SkinnedModel();

  private:
    SkinnedModel() = default;
    struct Entry
    {
        std::size_t mesh = 0; // maillage GPU du RenderModel
        int node = -1;        // nœud qui le porte
        int skin = -1;
        std::uint32_t vertexCount = 0;
        renderer::GpuBuffer rest;       // sommets au repos (kPbrVertexFloats flottants)
        renderer::GpuBuffer influences; // uvec4 os + vec4 poids par sommet
        renderer::GpuBuffer palette;    // mat4 par os
        rhi::BindGroupHandle group;
    };
    rhi::Device *device_ = nullptr;
    renderer::PipelineCache *pipelines_ = nullptr;
    std::shared_ptr<renderer::ShaderProgram> program_;
    assets::RenderModel *render_ = nullptr;
    std::vector<int> parents_;
    std::vector<assets::SkinData> skins_;
    std::vector<Entry> entries_;
};

} // namespace orosmo::anim
