#include "Scene/mixed_scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace demo
{

namespace rd = orosmo::renderer;
using orosmo::core::makeError;

namespace
{
rd::MeshData quadData(float width, float height, const glm::vec3 &c, const char *name)
{
    const float w = width * 0.5f;
    const float h = height * 0.5f;
    rd::MeshData d;
    d.vertices = {-w, -h, 0, 0, 0, 1, c.r, c.g, c.b, //
                  w,  -h, 0, 0, 0, 1, c.r, c.g, c.b, //
                  w,  h,  0, 0, 0, 1, c.r, c.g, c.b, //
                  -w, h,  0, 0, 0, 1, c.r, c.g, c.b};
    d.indices = {0, 1, 2, 0, 2, 3};
    d.attributeSizes = {3, 3, 3};
    d.name = name;
    return d;
}
} // namespace

orosmo::core::Result<> MixedScene::init(orosmo::rhi::Device &device, rd::ShaderLibrary &shaders, rd::PipelineCache &pipelines, rd::SceneRenderer &renderer,
                                        const std::filesystem::path &shaderDirectory)
{
    auto program = shaders.loadGraphics(shaderDirectory / "mesh.vert", shaderDirectory / "mesh.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    meshProgram_ = *program;
    struct Pane
    {
        float w, h;
        glm::vec3 color;
        glm::vec3 position;
    };
    const Pane panes[] = {{4.0f, 4.0f, {0.2f, 0.5f, 1.0f}, {-3.5f, 3.0f, 4.5f}}, // vitre bleue
                          {4.0f, 4.0f, {1.0f, 0.3f, 0.2f}, {-1.5f, 3.0f, 6.0f}}, // vitre rouge, plus près de la caméra : devant la bleue
                          {3.0f, 3.0f, {0.3f, 1.0f, 0.4f}, {3.0f, 2.5f, 5.0f}},  // vitre verte
                          {5.0f, 2.0f, {1.0f, 0.7f, 0.2f}, {0.0f, 1.5f, -4.0f}}}; // lueur additive derrière le héros
    for (std::size_t i = 0; i < panes_.size(); ++i)
    {
        auto mesh = rd::Mesh::create(device, quadData(panes[i].w, panes[i].h, panes[i].color, "mixed.pane"));
        if (!mesh)
        {
            return std::unexpected(mesh.error());
        }
        mesh->setProgram(meshProgram_);
        mesh->setPosition(panes[i].position);
        panes_[i] = std::move(*mesh);
    }
    auto transparents = rd::TransparentFeature::create(device, shaders, pipelines, shaderDirectory);
    if (!transparents)
    {
        return std::unexpected(transparents.error());
    }
    auto world = rd::Layer2DFeature::create("demo.world2d", rd::Layer2DMode::kWorld, device, shaders, pipelines, shaderDirectory);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    // Damier 8 x 8 (sprites au plus proche).
    std::array<std::uint8_t, 8 * 8 * 4> texels{};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const std::uint8_t v = ((x + y) & 1) != 0 ? 255 : 90;
            const std::size_t i = static_cast<std::size_t>(y * 8 + x) * 4;
            texels[i] = v;
            texels[i + 1] = v;
            texels[i + 2] = 255;
            texels[i + 3] = 255;
        }
    }
    rd::TextureInfo info;
    info.width = info.height = 8;
    info.filter = rd::FilterMode::kNearest;
    auto checker = rd::Texture::create(device, info, std::as_bytes(std::span(texels)));
    if (!checker)
    {
        return std::unexpected(checker.error());
    }
    checker_ = std::move(*checker);
    auto t = renderer.addFeature(std::move(*transparents));
    if (!t)
    {
        return std::unexpected(t.error());
    }
    transparents_ = static_cast<rd::TransparentFeature *>(*t);
    auto w = renderer.addFeature(std::move(*world));
    if (!w)
    {
        return std::unexpected(w.error());
    }
    world_ = static_cast<rd::Layer2DFeature *>(*w);
    return {};
}

orosmo::core::Result<> MixedScene::addShadowCasters(rd::ShadowFeature &shadows, rd::TransparentShadowMode mode)
{
    shadows_ = &shadows;
    shadowMode_ = mode;
    for (std::size_t i = 0; i < paneCasters_.size(); ++i)
    {
        // Boîte invalide : toujours dessinée (quatre petits quadrilatères, pas de culling par vue d'ombre).
        auto id = shadows.addMeshCaster(panes_[i], rd::Aabb::empty(), rd::CasterMobility::kMovable);
        if (!id)
        {
            return std::unexpected(id.error());
        }
        paneCasters_[i] = *id;
    }
    return {};
}

orosmo::core::Result<> MixedScene::render(rd::SceneRenderer &renderer, const rd::Camera &, float seconds)
{
    (void)renderer;
    if (shadows_ != nullptr)
    {
        // Même opacité que le mélange (0,45) ; seuil du masque sous l'opacité, pour que l'ombre masquée existe.
        for (const rd::ShadowFeature::CasterId id : paneCasters_)
        {
            if (auto r = shadows_->setCasterTransparency(id, {shadowMode_, 0.45f, 0.3f}); !r)
            {
                return std::unexpected(r.error());
            }
        }
    }
    // Verres triés par la distance à la caméra (ordre de dépôt volontairement faux), lueur additive en dernier.
    const rd::TransparentMaterial glass{.mode = rd::BlendMode::kBlend, .alpha = 0.45f};
    for (const std::size_t i : {std::size_t{0}, std::size_t{2}, std::size_t{1}})
    {
        if (auto r = transparents_->submit(renderer, panes_[i], glass); !r)
        {
            return std::unexpected(r.error());
        }
    }
    if (auto r = transparents_->submit(renderer, panes_[3], {.mode = rd::BlendMode::kAdditive, .alpha = 0.6f}); !r)
    {
        return std::unexpected(r.error());
    }
    // 2D dans le monde : plan vertical à z = 0 passant par le héros (il en cache une partie : test de profondeur).
    world_->begin({});
    world_->setPlaneTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)));
    auto material = world_->renderer().material(checker_, true);
    if (!material)
    {
        return std::unexpected(material.error());
    }
    for (int i = 0; i < 5; ++i)
    {
        rd::Sprite s;
        s.material = *material;
        s.position = {-4.0f + 2.0f * static_cast<float>(i), 2.0f + 0.5f * std::sin(seconds + static_cast<float>(i))};
        s.size = {1.4f, 1.4f};
        s.rotation = seconds * 0.5f + static_cast<float>(i);
        if (auto r = world_->renderer().draw(s); !r)
        {
            return std::unexpected(r.error());
        }
    }
    world_->renderer().circleOutline({0.0f, 3.0f}, 3.0f, 0.15f, {1, 1, 1, 0.9f}, 48);
    world_->renderer().line({-5.0f, 0.2f}, {5.0f, 0.2f}, 0.12f, {1, 0.9f, 0.2f, 1});
    return {};
}

void MixedScene::destroy()
{
    for (auto &pane : panes_)
    {
        pane.reset();
    }
    checker_ = rd::Texture{};
    meshProgram_.reset();
    if (shadows_ != nullptr)
    {
        for (const rd::ShadowFeature::CasterId id : paneCasters_)
        {
            shadows_->removeCaster(id);
        }
        shadows_ = nullptr;
    }
    transparents_ = nullptr;
    world_ = nullptr;
}

} // namespace demo
