// REN-28 : dessin instancié. Toutes les instances d'une image (InstanceBatchBuilder, déjà triées par (maillage, matériau)) sont
// envoyées dans UN tampon de sommets à pas d'instance ; chaque lot est UN drawIndexed (firstInstance = début du lot dans le tampon).
// Les maillages et matériaux sont enregistrés une fois et désignés par leur identifiant (ceux qu'utilisent ObjectSet et
// InstanceBatchBuilder). `draw` enregistre ses dessins dans SceneRenderer (drawCustom) : ils sont joués dans la passe « scene » du
// graphe (REN-01), à leur place parmi les autres dessins, avant les post-traitements ; sur tous les backends du RHI.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/instance_batch.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace orosmo::renderer
{

// Matériau d'un lot : programme (sommets : attributs du maillage aux emplacements 0..7, instance GpuInstance aux emplacements 8..15 ;
// voir shaders/rhi/mesh_instanced.vert), textures du groupe 1 (non possédées) et push constants (premier entier : drapeau fil de fer,
// convention de mesh.frag).
struct InstanceMaterial
{
    std::shared_ptr<ShaderProgram> program; // nul : programme par défaut (mesh_instanced.vert + mesh.frag avec OROSMO_INSTANCED)
    rhi::BindGroupHandle textures;
    std::vector<std::byte> pushConstants;
    rhi::CullMode cull = rhi::CullMode::kNone;
};

struct InstanceDrawStats
{
    std::uint32_t drawCalls = 0;
    std::uint32_t instances = 0;
    std::uint32_t pipelineChanges = 0;
    std::uint64_t uploadedBytes = 0;
};

// État de pipeline d'un dessin instancié : attributs de `layoutOf` (emplacements 0..7, liaison 0) + GpuInstance (emplacements 8..15,
// liaison 1, pas d'instance), cull du matériau, reverse-Z (fil de fer : profondeur égale acceptée), formats de la cible de scène
// (couleur écrite dans la première cible seulement). Partagé par InstanceRenderer et le rendu piloté par GPU (gpu_driven.hpp).
GraphicsState instancedGraphicsState(const Mesh &layoutOf, rhi::CullMode cull, const SceneDrawInfo &info, bool lines);
// Push constants d'un matériau pour `pipeline` (taille réfléchie du programme, zéros au-delà des octets du matériau ; premier
// entier = drapeau fil de fer, convention de mesh.frag). Vide si le programme n'a pas de push constants.
std::vector<std::byte> instancedPushConstants(PipelineCache &pipelines, rhi::PipelineHandle pipeline, const InstanceMaterial &material, bool lines);

class InstanceRenderer
{
  public:
    // `shaderDirectory` : shaders/rhi (mesh_instanced.vert, mesh.frag).
    static core::Result<std::unique_ptr<InstanceRenderer>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                  const std::filesystem::path &shaderDirectory);
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::unique_ptr<InstanceRenderer>> create(const RenderContext &context)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory());
    }
    InstanceRenderer(const InstanceRenderer &) = delete;
    InstanceRenderer &operator=(const InstanceRenderer &) = delete;

    // Maillage non possédé (il doit survivre au renderer ou à son dernier dessin) ; sans données d'instance propres, 8 attributs au
    // plus. Identifiants consécutifs à partir de 0.
    core::Result<std::uint32_t> addMesh(const Mesh &mesh);
    std::uint32_t addMaterial(InstanceMaterial material);
    // Remplace le matériau `id` (ex. textures, push constants ou faces éliminées du maillage source changés) ; faux si inconnu.
    // Avant `draw` : les dessins déjà enregistrés gardent le matériau lu à leur enregistrement.
    bool setMaterial(std::uint32_t id, InstanceMaterial material);
    std::size_t meshCount() const noexcept { return meshes_.size(); }
    // Maillage d'un identifiant (nul si inconnu) : sert aux casters d'ombres (ShadowFeature) qui dessinent les mêmes lots.
    const Mesh *mesh(std::uint32_t id) const noexcept { return id < meshes_.size() ? meshes_[id] : nullptr; }
    std::size_t materialCount() const noexcept { return materials_.size(); }
    // Matériau d'un identifiant (nul si inconnu).
    const InstanceMaterial *material(std::uint32_t id) const noexcept { return id < materials_.size() ? &materials_[id] : nullptr; }
    const std::shared_ptr<ShaderProgram> &defaultProgram() const noexcept { return defaultProgram_; }

    // Envoie les instances de `batches` (après build) et enregistre un draw call par lot dans `scene` (entre beginFrame, setCamera et
    // endFrame ; les maillages doivent rester valides jusqu'à endFrame).
    // `wireframe` : un second passage en fil de fer par lot (comme SceneRenderer::drawMesh avec une caméra en fil de fer).
    // Une seule fois par image et par InstanceRenderer : le tampon d'instances est réécrit à chaque appel, et les écritures d'une
    // image sont toutes visibles avant ses commandes (un second appel dans la même image remplacerait les instances du premier).
    core::Result<InstanceDrawStats> draw(SceneRenderer &scene, const InstanceBatchBuilder &batches, bool wireframe = false);

  private:
    InstanceRenderer(rhi::Device &device, PipelineCache &pipelines) : device_(device), pipelines_(pipelines) {}
    core::Result<> upload(const InstanceBatchBuilder &batches, const glm::vec3 &origin, InstanceDrawStats &stats);

    rhi::Device &device_;
    PipelineCache &pipelines_;
    std::shared_ptr<ShaderProgram> defaultProgram_;
    std::vector<const Mesh *> meshes_;
    std::vector<InstanceMaterial> materials_;
    GpuBuffer instanceBuffer_;
    std::vector<GpuInstance> scratch_;
};

} // namespace orosmo::renderer
