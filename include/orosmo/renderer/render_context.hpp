// Contexte de rendu (A75.4, M8-L11) : le quatuor `(device, library, pipelines, shaderDirectory)` que chaque fabrique du renderer
// demande, réuni en un objet passé par référence. Deux formes :
//   - `RenderContext::create(device, shaderDirectory)` possède sa `ShaderLibrary` et son `PipelineCache` (usage courant) ;
//   - `RenderContext(device, library, pipelines, shaderDirectory)` référence des objets existants (code qui les partage déjà).
// Les anciennes signatures des fabriques restent (choix 10 d'A79 : jusqu'à M10) ; les surcharges qui prennent un `RenderContext`
// les appellent. Le contexte doit vivre plus longtemps que tout ce qu'il a servi à créer (comme le périphérique, la bibliothèque et
// le cache qu'il porte).
#pragma once

#include "orosmo/renderer/pipeline_cache.hpp"
#include "orosmo/renderer/shader_library.hpp"
#include "orosmo/rhi/device.hpp"

#include <filesystem>
#include <memory>
#include <utility>

namespace orosmo::renderer
{

class RenderContext
{
  public:
    // Références vers des objets existants (non possédés : ils doivent vivre plus longtemps que le contexte).
    RenderContext(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines, std::filesystem::path shaderDirectory)
        : device_(&device), library_(&library), pipelines_(&pipelines), shaderDirectory_(std::move(shaderDirectory))
    {
    }
    // Contexte qui possède sa bibliothèque de shaders et son cache de pipelines. `shaderDirectory` : shaders du moteur
    // (shaders/rhi/, ex. `GET_RESOURCE_PATH("shader/rhi")`).
    static std::unique_ptr<RenderContext> create(rhi::Device &device, std::filesystem::path shaderDirectory,
                                                 ShaderLibraryConfig libraryConfig = {})
    {
        std::unique_ptr<RenderContext> context(new RenderContext(device, std::move(shaderDirectory)));
        context->ownedLibrary_ = std::make_unique<ShaderLibrary>(device, std::move(libraryConfig));
        context->ownedPipelines_ = std::make_unique<PipelineCache>(device);
        context->library_ = context->ownedLibrary_.get();
        context->pipelines_ = context->ownedPipelines_.get();
        return context;
    }

    // Non copiable ni déplaçable : les objets créés gardent des références vers la bibliothèque et le cache.
    RenderContext(const RenderContext &) = delete;
    RenderContext &operator=(const RenderContext &) = delete;
    RenderContext(RenderContext &&) = delete;
    RenderContext &operator=(RenderContext &&) = delete;
    ~RenderContext()
    {
        // Les pipelines référencent des programmes de la bibliothèque : le cache part le premier.
        ownedPipelines_.reset();
        ownedLibrary_.reset();
    }

    rhi::Device &device() const noexcept { return *device_; }
    ShaderLibrary &library() const noexcept { return *library_; }
    PipelineCache &pipelines() const noexcept { return *pipelines_; }
    const std::filesystem::path &shaderDirectory() const noexcept { return shaderDirectory_; }
    // Vrai si la bibliothèque et le cache appartiennent au contexte (`create`).
    bool ownsResources() const noexcept { return ownedLibrary_ != nullptr; }

  private:
    RenderContext(rhi::Device &device, std::filesystem::path shaderDirectory) : device_(&device), shaderDirectory_(std::move(shaderDirectory)) {}

    rhi::Device *device_ = nullptr;
    ShaderLibrary *library_ = nullptr;
    PipelineCache *pipelines_ = nullptr;
    std::filesystem::path shaderDirectory_;
    std::unique_ptr<ShaderLibrary> ownedLibrary_;
    std::unique_ptr<PipelineCache> ownedPipelines_;
};

} // namespace orosmo::renderer
