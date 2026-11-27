// REN-32 et REN-37 (rendu) : plugin du graphe qui dessine les objets à mode de fusion (`BlendMode`) APRÈS les opaques de la passe
// « scene », avec le test de profondeur de la scène. Ordre fixe (docs/03 §11 bis) :
//   1. kOpaque, kMask, kMaskA2C, kDithered : écrivent la profondeur, dans l'ordre de dépôt (le test de profondeur suffit) ;
//   2. kBlend, kPremultiplied, kTransmission : de l'arrière vers l'avant (distance du centre de l'objet à la caméra), test de
//      profondeur SANS écriture ;
//   3. kAdditive : après les mélangés (priorité puis distance, déterministe), sans écriture de profondeur.
// Dans chaque groupe, la priorité de tri du matériau l'emporte sur la distance (petite priorité d'abord : dessinée en dessous).
// Le plugin s'enregistre avec `SceneRenderer::addFeature` AVANT les post-traitements et les superpositions : sa passe écrit
// `scene.color` (chargée, pas effacée) et lit/teste `scene.depth`, donc les passes suivantes (post-traitement, mise à l'échelle,
// historique) voient les transparents.
// Limites : kTransmission se compose comme kBlend (pas de copie de la scène ni de réfraction, REN-38) ; kMaskA2C se comporte comme
// kMask (le RHI n'a ni MSAA ni alpha-to-coverage) ; le seuil et l'opacité sont par objet, pas par texel.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/render_context.hpp"
#include "orosmo/renderer/blend_mode.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace orosmo::renderer
{

// Matériau d'un objet transparent.
struct TransparentMaterial
{
    BlendMode mode = BlendMode::kBlend;
    float alpha = 1.0f;     // opacité : mélange (kBlend...), part de pixels gardés (kDithered), comparée au seuil (kMask)
    float cutoff = 0.5f;    // seuil de kMask / kMaskA2C : le pixel est rejeté si alpha < cutoff
    std::int32_t priority = 0; // priorité de tri : l'emporte sur la distance (petite valeur : dessiné d'abord, donc dessous)
    glm::vec3 centerOffset{0.0f}; // centre de l'objet pour le tri, relatif à `Mesh::position` (grands objets : décaler ou prioriser)
};

struct TransparentStats
{
    std::uint32_t submitted = 0; // objets déposés pour l'image
    std::uint32_t opaque = 0;    // groupe 1
    std::uint32_t blended = 0;   // groupe 2
    std::uint32_t additive = 0;  // groupe 3
    std::uint32_t drawCalls = 0;
};

class TransparentFeature final : public RenderFeature
{
  public:
    static constexpr const char *kName = "transparents";
    static constexpr const char *kPassName = "transparents";

    // `shaderDirectory` : shaders/rhi (mesh.vert, mesh.frag).
    static core::Result<std::unique_ptr<TransparentFeature>> create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                    const std::filesystem::path &shaderDirectory);
    // A75.4 : même fabrique depuis un `RenderContext`.
    static core::Result<std::unique_ptr<TransparentFeature>> create(const RenderContext &context)
    {
        return create(context.device(), context.library(), context.pipelines(), context.shaderDirectory());
    }

    std::string name() const override { return kName; }
    core::Result<> prepare(const FrameData &frame) override;
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;
    // A61 C : la passe n'existe que si des objets sont déposés ; elle lit les dépôts de l'image à l'exécution.
    std::optional<std::uint64_t> passSignature() const override { return entries_.empty() ? 1u : 2u; }
    void frameEnded() noexcept override;

    // Dépose un objet pour l'image (entre `SceneRenderer::setCamera` et `endFrame`). La géométrie de `mesh` est utilisée ; son
    // programme est remplacé par celui du plugin (éclairage de mesh.frag, sortie avec alpha). La transformation est copiée au dépôt
    // (`SceneRenderer::recordModelSlice`) : un même maillage peut être déposé plusieurs fois avec des poses différentes. Ses tampons
    // doivent rester valides jusqu'à `endFrame`. Les dépôts valent pour une image, même si `endFrame` échoue (`frameEnded`).
    // Retourne l'indice de dépôt (celui de `drawOrder`).
    core::Result<std::uint32_t> submit(SceneRenderer &scene, Mesh &mesh, const TransparentMaterial &material);

    // Ordre de dessin de la dernière image : indices de dépôt, dans l'ordre exécuté (tests, outils de debug).
    const std::vector<std::uint32_t> &drawOrder() const noexcept { return lastOrder_; }
    const TransparentStats &stats() const noexcept { return stats_; }

    struct Entry
    {
        rhi::PipelineHandle pipeline;
        std::uint32_t modelOffset = 0; // tranche du modèle de ce dépôt dans FrameResources::modelGroup
        rhi::BindGroupHandle textureGroup;
        rhi::BufferHandle vertexBuffer;
        rhi::BufferHandle indexBuffer;
        std::uint32_t indexCount = 0;
        TransparentMaterial material;
        glm::vec3 center{0.0f}; // monde
        std::uint32_t sequence = 0;
    };

  private:
    TransparentFeature() = default;
    core::Result<> play(RenderPassContext &ctx);

    PipelineCache *pipelines_ = nullptr;
    std::shared_ptr<ShaderProgram> program_;
    std::vector<Entry> entries_;
    std::vector<std::uint32_t> lastOrder_;
    glm::vec3 cameraPosition_{0.0f};
    TransparentStats stats_;
};

// Ordre de dessin d'une image : indices de `entries` triés par groupe, priorité, puis distance à `camera` (de l'arrière vers l'avant ;
// égalité : ordre de dépôt). Pure (testable sans GPU).
std::vector<std::uint32_t> sortTransparents(std::span<const TransparentFeature::Entry> entries, const glm::vec3 &camera);

} // namespace orosmo::renderer
