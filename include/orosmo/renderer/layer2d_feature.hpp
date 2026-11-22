// R2D-05 : mélange 2D/3D. Un `Renderer2D` enveloppé dans un plugin du graphe (`RenderFeature`), en trois modes. Ordre fixe d'une
// image (docs/03 §11) : 3D opaque -> 3D transparent -> 2D du monde -> post-traitement -> 2D en superposition. Il vient de l'ordre
// d'enregistrement des plugins (voir `SceneRenderer::addFeature`) :
//
//   addFeature(TransparentFeature)          // après la passe « scene » (3D opaque)
//   addFeature(Layer2DFeature kWorld)       // 2D du monde, avec le test de profondeur de la scène
//   addFeature(<post-traitements>)          // lisent la scène, 2D du monde comprise
//   addFeature(Layer2DFeature kOverlay)     // écrit la sortie, après la mise à l'échelle (et avant l'interface)
//   Layer2DFeature kTexture                 // n'importe où : sa passe s'exécute AVANT la scène
//
// Chaque plugin possède son `Renderer2D` (un dépôt par plugin) : l'application dépose ses sprites et formes dans `renderer()` entre
// `begin(...)` et `SceneRenderer::endFrame`, comme avec un `Renderer2D` seul ; `Renderer2D::render` est inchangé, le plugin ne fait
// que l'appeler depuis une passe du graphe.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/render_feature.hpp"
#include "orosmo/renderer/render_target.hpp"
#include "orosmo/renderer/renderer2d.hpp"
#include "orosmo/renderer/shader_library.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace orosmo::renderer
{

enum class Layer2DMode : std::uint8_t
{
    // 2D dans le monde : le plan 2D (x, y) est placé dans la scène 3D par `setPlaneTransform` et vu par la caméra 3D ; les sprites
    // sont testés contre la profondeur de la scène (cachés par un objet 3D devant eux, sans écrire la profondeur). La `Camera2D`
    // n'est pas utilisée (la caméra 3D décide) ; pas de pixel parfait.
    kWorld,
    // 2D en superposition : dessinée sur la sortie, après le post-traitement et la mise à l'échelle, en pixels de la sortie
    // (`Camera2D` sur la taille de la sortie). Sans test de profondeur.
    kOverlay,
    // 2D vers texture : dessinée dans une texture propre (`setTextureSize`) avant la passe de scène, que `binding()` donne à un
    // maillage 3D (`Mesh::setTextures`).
    kTexture,
};

class Layer2DFeature final : public RenderFeature
{
  public:
    static core::Result<std::unique_ptr<Layer2DFeature>> create(std::string name, Layer2DMode mode, rhi::Device &device, ShaderLibrary &library,
                                                                PipelineCache &pipelines, const std::filesystem::path &shaderDirectory);

    std::string name() const override { return name_; }
    Layer2DMode mode() const noexcept { return mode_; }
    core::Result<> prepare(const FrameData &frame) override;
    core::Result<> declarePasses(RenderGraphBuilder &builder) override;

    // Dessinateur du plugin : `material(texture)`, `draw(Sprite)`, formes. Les dépôts se font après `begin`.
    Renderer2D &renderer() noexcept { return *renderer_; }
    // Commence l'image du plugin (équivaut à `renderer().begin(camera)`). En mode kWorld la caméra n'est pas utilisée.
    void begin(const Camera2D &camera) { renderer_->begin(camera); }

    // kWorld : placement du plan 2D dans le monde (défaut : identité, plan z = 0).
    void setPlaneTransform(const glm::mat4 &planeToWorld) noexcept { plane_ = planeToWorld; }

    // kTexture : taille de la texture (la crée ou la recrée ; son contenu est perdu), couleur d'effacement avant chaque image.
    core::Result<> setTextureSize(std::uint32_t width, std::uint32_t height);
    void setClearColor(const glm::vec4 &color) noexcept { clear_ = color; }
    rhi::TextureHandle texture() const noexcept { return target_.valid() ? target_.color(0) : rhi::TextureHandle{}; }
    // Texture et échantillonneur à donner à `Mesh::setTextures` (invalides tant que `setTextureSize` n'a pas réussi).
    TextureBinding binding() const noexcept { return {texture(), target_.valid() ? target_.sampler() : rhi::SamplerHandle{}}; }

    std::uint32_t passesRun() const noexcept { return passesRun_; }

  private:
    Layer2DFeature() = default;

    std::string name_;
    Layer2DMode mode_ = Layer2DMode::kOverlay;
    rhi::Device *device_ = nullptr;
    std::unique_ptr<Renderer2D> renderer_;
    glm::mat4 plane_{1.0f};
    glm::mat4 viewProj_{1.0f};
    glm::vec4 clear_{0.0f};
    RenderTarget target_;
    std::uint32_t passesRun_ = 0;
};

} // namespace orosmo::renderer
