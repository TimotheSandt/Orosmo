// Plugin de rendu de la démo : l'interface puis, au besoin, la présentation, en fin du graphe de l'image (après la mise à l'échelle
// de la scène). Perf M5 (point 6) : l'image finale est composée directement dans le backbuffer quand rien ne relit l'image composée
// (pas de capture à cette image) et que la vue a la taille et le format du backbuffer ; sinon, comme avant, dans la cible
// intermédiaire « composed », copiée (étirée) sur le backbuffer par la passe « present ».
#pragma once

#include <orosmo/renderer/render_feature.hpp>
#include <orosmo/rhi/device.hpp>

#include <functional>
#include <string>
#include <utility>

namespace demo
{

// Où l'image finale (scène, puis interface) est composée.
enum class FrameComposition
{
    kBackbuffer,     // la sortie de la scène est le backbuffer : pas de cible intermédiaire ni de passe « present »
    kComposedTarget, // cible « composed » à la taille de la vue, copiée sur le backbuffer par la passe « present »
};

struct FrameCompositionQuery
{
    orosmo::rhi::Extent2D view{};       // sortie de la scène (pixels de la vue)
    orosmo::rhi::Extent2D backbuffer{}; // taille réelle du backbuffer (Device::textureDesc)
    orosmo::rhi::Format backbufferFormat = orosmo::rhi::Format::kUndefined;
    bool keepComposed = false; // l'image composée est relue après l'image (capture : ui.png)
};

// Pure (sans GPU) : le backbuffer seulement si rien ne relit l'image composée, à la même taille (la copie « present » étirerait
// sinon) et au format de l'interface (RGBA8).
FrameComposition chooseFrameComposition(const FrameCompositionQuery &query) noexcept;

class FrameOverlay final : public orosmo::renderer::RenderFeature
{
  public:
    // Dessine l'interface sur `target` (contenu préservé) : la sortie de la scène, backbuffer ou cible « composed ».
    using DrawUi = std::function<void(orosmo::rhi::CommandEncoder &encoder, orosmo::rhi::TextureHandle target)>;
    // Copie `composed` sur le backbuffer de l'image.
    using Present = std::function<void(orosmo::rhi::CommandEncoder &encoder, orosmo::rhi::TextureHandle composed)>;

    FrameOverlay(DrawUi drawUi, Present present) : drawUi_(std::move(drawUi)), present_(std::move(present)) {}

    std::string name() const override { return "demo.overlay"; }

    // Avant SceneRenderer::endFrame : backbuffer de l'image, composition choisie, interface active ou non (sans interface, pas de
    // passe « ui »). Remet uiDone et presented à faux.
    void beginFrame(orosmo::rhi::TextureHandle backbuffer, FrameComposition composition, bool ui) noexcept;

    orosmo::core::Result<> declarePasses(orosmo::renderer::RenderGraphBuilder &builder) override;

    FrameComposition composition() const noexcept { return composition_; }

    bool uiDone = false;    // la passe « ui » s'est exécutée
    bool presented = false; // la passe « present » s'est exécutée

  private:
    DrawUi drawUi_;
    Present present_;
    orosmo::rhi::TextureHandle backbuffer_;
    FrameComposition composition_ = FrameComposition::kComposedTarget;
    bool ui_ = true;
};

} // namespace demo
