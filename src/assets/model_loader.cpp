#include "orosmo/assets/model_loader.hpp"

#include <utility>

namespace orosmo::assets
{

core::Result<ModelLoader> ModelLoader::create(const renderer::RenderContext &context)
{
    auto defaults = renderer::PbrDefaults::create(context.device());
    if (!defaults)
    {
        return std::unexpected(defaults.error());
    }
    ModelLoader out;
    out.device_ = &context.device();
    out.textures_ = std::make_unique<TextureCache>(context.device());
    out.shaders_ = std::make_unique<renderer::PbrShaderCache>(context);
    out.defaults_ = std::move(*defaults);
    return out;
}

core::Result<Model> ModelLoader::load(const std::filesystem::path &path) const
{
    auto loaded = loadGltf(path);
    if (!loaded)
    {
        return core::makeError(loaded.error().code, "ModelLoader: loading " + path.string() + ": " + loaded.error().message);
    }
    return loaded;
}

core::Result<RenderModel> ModelLoader::createRenderModel(const Model &model, const RenderModelOptions &options)
{
    return RenderModel::create(*device_, model, *textures_, *shaders_, defaults_, options);
}

} // namespace orosmo::assets
