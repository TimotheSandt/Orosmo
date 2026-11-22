// --scene mixed (REN-32, REN-37, R2D-05) : 3D opaque (la scène de la démo) + transparents triés (verre, lueur additive) + sprites et
// formes 2D dans le monde (testés contre la profondeur) + interface. Les plugins sont enregistrés AVANT ceux de la démo (interface,
// présentation) : l'ordre fixe 3D opaque -> transparent -> 2D monde -> post -> superposition est celui des enregistrements.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/layer2d_feature.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/scene_renderer.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/renderer/shadow_feature.hpp"
#include "orosmo/renderer/texture.hpp"
#include "orosmo/renderer/transparent_feature.hpp"

#include <array>
#include <filesystem>

namespace demo
{

class MixedScene
{
  public:
    // Crée les objets et enregistre les plugins dans `renderer`.
    orosmo::core::Result<> init(orosmo::rhi::Device &device, orosmo::renderer::ShaderLibrary &shaders, orosmo::renderer::PipelineCache &pipelines,
                                orosmo::renderer::SceneRenderer &renderer, const std::filesystem::path &shaderDirectory);
    // REN-39 : les vitres projettent une ombre de ce mode (la lueur additive n'en projette jamais). À appeler après `init`, avec --shadows on.
    orosmo::core::Result<> addShadowCasters(orosmo::renderer::ShadowFeature &shadows, orosmo::renderer::TransparentShadowMode mode);
    // Entre `SceneRenderer::setCamera` et `endFrame`, après le rendu de la scène de base.
    orosmo::core::Result<> render(orosmo::renderer::SceneRenderer &renderer, const orosmo::renderer::Camera &camera, float seconds);
    void destroy();

  private:
    orosmo::renderer::TransparentFeature *transparents_ = nullptr; // possédés par le renderer
    orosmo::renderer::Layer2DFeature *world_ = nullptr;
    std::shared_ptr<orosmo::renderer::ShaderProgram> meshProgram_;
    std::array<orosmo::renderer::Mesh, 4> panes_; // 3 vitres + 1 lueur additive
    orosmo::renderer::Texture checker_;
    orosmo::renderer::ShadowFeature *shadows_ = nullptr;
    std::array<orosmo::renderer::ShadowFeature::CasterId, 3> paneCasters_{};
    orosmo::renderer::TransparentShadowMode shadowMode_ = orosmo::renderer::TransparentShadowMode::kFull;
};

} // namespace demo
