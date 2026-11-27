// Chargeur de modèles (A75.2, M8-L12a) : réunit ce qu'il faut pour passer d'un fichier glTF à un `RenderModel` dessinable, que
// le code appelant créait et reliait à la main : le cache de textures (`TextureCache`), le cache des programmes PBR
// (`renderer::PbrShaderCache`) et les textures par défaut (`renderer::PbrDefaults`). Un même chargeur peut servir plusieurs modèles
// (les textures et les programmes sont alors partagés).
//
// Durée de vie : le `RenderContext` (son périphérique, sa bibliothèque de shaders) doit vivre plus longtemps que le chargeur, et le
// chargeur plus longtemps que les `RenderModel` qu'il a créés (leurs matériaux lient ses textures).
#pragma once

#include "orosmo/assets/model.hpp"
#include "orosmo/assets/render_model.hpp"
#include "orosmo/assets/texture_cache.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/material.hpp"
#include "orosmo/renderer/render_context.hpp"

#include <filesystem>
#include <memory>

namespace orosmo::assets
{

class ModelLoader
{
  public:
    // Crée les caches et les textures par défaut sur le périphérique du contexte.
    static core::Result<ModelLoader> create(const renderer::RenderContext &context);

    // Lit un fichier glTF (.gltf ou .glb) ; le message d'erreur nomme le fichier.
    core::Result<Model> load(const std::filesystem::path &path) const;
    // Crée le modèle dessinable de `model` avec les caches du chargeur.
    core::Result<RenderModel> createRenderModel(const Model &model, const RenderModelOptions &options = {});

    // ASSET-05 : relit les textures dont le fichier a changé ; renvoie leur nombre.
    std::size_t reloadChanged() { return textures_->reloadChanged(); }

    rhi::Device &device() const noexcept { return *device_; }
    TextureCache &textures() noexcept { return *textures_; }
    renderer::PbrShaderCache &shaders() noexcept { return *shaders_; }
    const std::shared_ptr<const renderer::PbrDefaults> &defaults() const noexcept { return defaults_; }

  private:
    ModelLoader() = default;

    rhi::Device *device_ = nullptr;
    std::unique_ptr<TextureCache> textures_;
    std::unique_ptr<renderer::PbrShaderCache> shaders_;
    std::shared_ptr<const renderer::PbrDefaults> defaults_;
};

} // namespace orosmo::assets
