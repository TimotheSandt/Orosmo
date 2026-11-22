// R2D-05 (mélange 2D/3D : 2D du monde avec profondeur, 2D en superposition, 2D vers texture sur un objet 3D) et R2D-06 (plusieurs vues,
// vue 3D dans l'interface) sur chaque backend (FOR_EACH_BACKEND), couches de validation à 0. Caméra orthographique : une unité = un
// pixel, image de 8 x 8 ; l'éclairage ambiant vaut 1 (couleur affichée = couleur du sommet ou du texel).
#include <doctest/doctest.h>

#include "renderer/repo_paths.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/canvas_renderer.hpp>
#include <orosmo/renderer/layer2d_feature.hpp>
#include <orosmo/renderer/light.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/scene_renderer.hpp>
#include <orosmo/renderer/scene_view.hpp>
#include <orosmo/renderer/texture.hpp>
#include <orosmo/renderer/transparent_feature.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>

namespace fs = std::filesystem;
namespace rd = orosmo::renderer;

namespace
{

fs::path shaderDir() { return tests::repoRoot(__FILE__) / "shaders" / "rhi"; }

constexpr std::uint32_t kSize = 8;
using Rgb = std::array<int, 3>;
using Color = std::array<float, 3>;

bool close(const Rgb &a, const Rgb &b, int tolerance = 2)
{
    return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
}
Rgb byte(float r, float g, float b) { return {static_cast<int>(r * 255.0f + 0.5f), static_cast<int>(g * 255.0f + 0.5f), static_cast<int>(b * 255.0f + 0.5f)}; }

void prepareCamera(rd::Camera &camera)
{
    camera.setAspect(kSize, kSize);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(100.0f);
    camera.setOrthographic(static_cast<float>(kSize));
}

struct MixScene
{
    Gpu gpu;
    std::unique_ptr<rd::ShaderLibrary> library;
    std::unique_ptr<rd::PipelineCache> pipelines;
    std::unique_ptr<rd::SceneRenderer> renderer;
    rd::LightManager lights;
    rd::Camera camera;
    std::shared_ptr<rd::ShaderProgram> meshProgram;
    rd::Texture white;

    explicit MixScene(Backend backend) : gpu(backend)
    {
        if (!gpu.device)
        {
            return;
        }
        library = std::make_unique<rd::ShaderLibrary>(*gpu.device);
        pipelines = std::make_unique<rd::PipelineCache>(*gpu.device);
        auto created = rd::SceneRenderer::create(*gpu.device, *library, *pipelines, {.shaderDirectory = shaderDir()});
        INFO((created ? std::string() : created.error().message));
        REQUIRE(created.has_value());
        renderer = std::move(*created);
        auto l = rd::LightManager::create(*gpu.device);
        REQUIRE(l.has_value());
        lights = std::move(*l);
        lights.setAmbientLight({1, 1, 1}, 1.0f);
        REQUIRE(lights.upload().has_value());
        renderer->setLights(&lights);
        renderer->setOutputResolution(kSize, kSize);
        prepareCamera(camera);
        auto program = library->loadGraphics(shaderDir() / "mesh.vert", shaderDir() / "mesh.frag");
        REQUIRE(program.has_value());
        meshProgram = *program;
        const std::uint8_t texel[4] = {255, 255, 255, 255};
        rd::TextureInfo info;
        info.filter = rd::FilterMode::kNearest;
        auto t = rd::Texture::create(*gpu.device, info, std::as_bytes(std::span(texel)));
        REQUIRE(t.has_value());
        white = std::move(*t);
    }
    ~MixScene() { renderer.reset(); }


    rd::Mesh quad(float side, const Color &c, float x, float z, const char *name)
    {
        const float h = side * 0.5f;
        rd::MeshData d;
        d.vertices = {-h, -h, 0, 0, 0, 1, c[0], c[1], c[2], //
                      h,  -h, 0, 0, 0, 1, c[0], c[1], c[2], //
                      h,  h,  0, 0, 0, 1, c[0], c[1], c[2], //
                      -h, h,  0, 0, 0, 1, c[0], c[1], c[2]};
        d.indices = {0, 1, 2, 0, 2, 3};
        d.attributeSizes = {3, 3, 3};
        d.name = name;
        auto mesh = rd::Mesh::create(*gpu.device, d);
        REQUIRE(mesh.has_value());
        mesh->setProgram(meshProgram);
        mesh->setPosition({x, 0.0f, z});
        return std::move(*mesh);
    }

    TextureHandle out() { return gpu.target(Format::kRGBA8Unorm, {kSize, kSize}, "output"); }
    Rgb at(TextureHandle t, int x, int y)
    {
        const auto p = rgba(gpu.pixel(t, x, y));
        return {p[0], p[1], p[2]};
    }
};

// Plugin 2D du test, ajouté au renderer.
rd::Layer2DFeature *addLayer(MixScene &scene, const char *name, rd::Layer2DMode mode)
{
    auto feature = rd::Layer2DFeature::create(name, mode, *scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
    INFO((feature ? std::string() : feature.error().message));
    REQUIRE(feature.has_value());
    auto added = scene.renderer->addFeature(std::move(*feature));
    REQUIRE(added.has_value());
    return static_cast<rd::Layer2DFeature *>(*added);
}

// Sprite de la taille de l'image (8 x 8 unités), centré à l'origine du plan.
void fullSprite(rd::Layer2DFeature &layer, const rd::Texture &white, const glm::vec4 &color)
{
    auto material = layer.renderer().material(white, true);
    REQUIRE(material.has_value());
    rd::Sprite s;
    s.material = *material;
    s.size = {static_cast<float>(kSize), static_cast<float>(kSize)};
    s.color = color;
    REQUIRE(layer.renderer().draw(s).has_value());
}

} // namespace

TEST_SUITE("renderer/mixed-2d-3d")
{
    TEST_CASE("2D in the world is depth tested against the 3D scene, between transparents and post-processing (R2D-05)")
    {
        FOR_EACH_BACKEND(backend)
        {
            MixScene scene(backend);
            REQUIRE(scene.gpu.device);
            // Post-traitement réel (IPostProcessPass, ajouté AVANT les plugins) : compose une couleur prémultipliée (0, 0, 0.5, 0.5)
            // sur toute la scène : pixel = scène / 2 + (0, 0, 0.5). Il doit voir la passe principale complète, sprite du monde compris.
            const std::uint8_t tintTexel[4] = {0, 0, 128, 128};
            rd::TextureInfo tintInfo;
            tintInfo.filter = rd::FilterMode::kNearest;
            auto tintTexture = rd::Texture::create(*scene.gpu.device, tintInfo, std::as_bytes(std::span(tintTexel)));
            REQUIRE(tintTexture.has_value());
            struct Tint final : rd::IPostProcessPass
            {
                TextureHandle texture;
                SamplerHandle sampler;
                std::string name() const noexcept override { return "tint"; }
                orosmo::core::Result<> process(rd::FrameContext &context, rd::RenderTarget &target) override
                {
                    return context.blit.compose(context.encoder, texture, sampler, target.color(0), target.info().colorFormats[0], "post.tint");
                }
            };
            auto tint = std::make_unique<Tint>();
            tint->texture = tintTexture->handle();
            tint->sampler = tintTexture->sampler();
            scene.renderer->addPostProcessPass(std::move(tint));
            auto transparents = rd::TransparentFeature::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            REQUIRE(transparents.has_value());
            auto *transparentFeature = static_cast<rd::TransparentFeature *>(*scene.renderer->addFeature(std::move(*transparents)));
            rd::Layer2DFeature *world = addLayer(scene, "mix.world", rd::Layer2DMode::kWorld);
            rd::Layer2DFeature *overlay = addLayer(scene, "mix.overlay", rd::Layer2DMode::kOverlay);
            REQUIRE(world != nullptr);
            auto wall = scene.quad(4.0f, {0, 1, 0}, -2.0f, -4.0f, "wall");   // opaque vert, colonnes 0 à 3, à z = -4
            wall.setScale({1.0f, 2.0f, 1.0f});
            auto glass = scene.quad(8.0f, {0, 0, 1}, 0.0f, -3.0f, "glass"); // bleu 50 %, devant le mur
            for (const float planeZ : {-6.0f, -2.0f})
            {
                INFO("backend ", tests::backendName(backend), ", plane z = ", planeZ);
                const TextureHandle out = scene.out();
                scene.gpu.run(
                    [&](CommandEncoder &e)
                    {
                        REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                        REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                        REQUIRE(scene.renderer->drawMesh(wall, scene.camera).has_value());
                        REQUIRE(transparentFeature->submit(*scene.renderer, glass, {.mode = rd::BlendMode::kBlend, .alpha = 0.5f}).has_value());
                        world->begin({});
                        world->setPlaneTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, planeZ)));
                        fullSprite(*world, scene.white, {1, 0, 0, 1});
                        rd::Camera2D screen;
                        screen.setViewport(kSize, kSize);
                        screen.setCenter({kSize * 0.5f, kSize * 0.5f});
                        overlay->begin(screen);
                        overlay->renderer().rect({0, 0}, {2, 2}, {1, 1, 1, 1}); // coin bas gauche de la sortie
                        const auto ended = scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                        INFO((ended ? std::string() : ended.error().message));
                        REQUIRE(ended.has_value());
                    });
                // Après le post-traitement : couleur / 2 + (0, 0, 0.5). Un sprite dessiné après lui resterait rouge pur.
                if (planeZ < -5.0f)
                {
                    // Plan derrière le mur et le verre : caché derrière le mur (colonnes 0 à 3). À droite il est dessiné APRÈS les
                    // transparents (ordre fixe) et sans profondeur de leur part : le sprite rouge recouvre le verre.
                    CHECK(close(scene.at(out, 1, 2), byte(0, 0.25f, 0.75f))); // le mur vert, vu à travers le verre bleu, teinté
                    CHECK(close(scene.at(out, 6, 2), byte(0.5f, 0, 0.5f)));   // le sprite, traité par le post-traitement
                }
                else
                {
                    // Plan devant le mur, mais derrière le verre (z = -3 : le plan à z = -2 est devant le verre aussi).
                    CHECK(close(scene.at(out, 1, 2), byte(0.5f, 0, 0.5f)));
                    CHECK(close(scene.at(out, 6, 2), byte(0.5f, 0, 0.5f)));
                }
                CHECK(close(scene.at(out, 0, 7), byte(1, 1, 1))); // superposition : coin bas gauche de la sortie (origine en haut)
                CHECK(close(scene.at(out, 1, 6), byte(1, 1, 1)));
                CHECK(world->passesRun() > 0);
            }
            // Ordre fixe (docs/03 §11) : 3D opaque (scene) -> 3D transparent -> 2D monde -> post-traitement -> mise à l'échelle -> 2D
            // en superposition. Ordre d'exécution réel du graphe, pas celui de la déclaration.
            const auto order = scene.renderer->renderGraph().executionOrder();
            const auto position = [&](const char *pass) { return std::ranges::find(order, pass) - order.begin(); };
            REQUIRE(position("post.tint") < static_cast<std::ptrdiff_t>(order.size()));
            CHECK(position("scene") < position("transparents"));
            CHECK(position("transparents") < position("mix.world"));
            CHECK(position("mix.world") < position("post.tint"));
            CHECK(position("post.tint") < position("upscale"));
            CHECK(position("upscale") < position("mix.overlay"));
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("2D in the world follows camera-relative rendering: the plane stays where it is in the world (R2D-05, REN-02)")
    {
        FOR_EACH_BACKEND(backend)
        {
            MixScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::Layer2DFeature *world = addLayer(scene, "mix.world", rd::Layer2DMode::kWorld);
            scene.camera.setPosition({2.0f, 0.0f, 0.0f}); // vue de x = -2 à x = 6 ; le sprite couvre x de -4 à 4
            for (const bool relative : {false, true})
            {
                INFO("backend ", tests::backendName(backend), ", camera-relative ", relative);
                scene.renderer->setCameraRelative(relative);
                const TextureHandle out = scene.out();
                scene.gpu.run(
                    [&](CommandEncoder &e)
                    {
                        REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                        REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                        world->begin({});
                        world->setPlaneTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -6.0f)));
                        fullSprite(*world, scene.white, {1, 0, 0, 1});
                        REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
                    });
                CHECK(close(scene.at(out, 1, 4), byte(1, 0, 0))); // x = -1 : sur le sprite
                CHECK(close(scene.at(out, 5, 4), byte(1, 0, 0))); // x = 3,5
                CHECK(close(scene.at(out, 7, 4), byte(0, 0, 0))); // x = 5,5 : hors du sprite (centré sur la caméra, il y serait)
            }
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a view whose drawing fails still ends its frame; placements of a failed frame expire (R2D-06)")
    {
        FOR_EACH_BACKEND(backend)
        {
            MixScene scene(backend);
            REQUIRE(scene.gpu.device);
            // Plugin enregistré AVANT les vues, qui fait échouer endFrame avant la déclaration de leur passe.
            struct Failing final : rd::RenderFeature
            {
                bool fail = false;
                std::string name() const override { return "failing"; }
                orosmo::core::Result<> prepare(const rd::FrameData &) override
                {
                    return fail ? orosmo::core::makeError(ErrorCode::kUnknown, "prepare failed") : orosmo::core::Result<>{};
                }
                orosmo::core::Result<> declarePasses(rd::RenderGraphBuilder &) override { return {}; }
            };
            auto *failing = static_cast<Failing *>(*scene.renderer->addFeature(std::make_unique<Failing>()));
            auto views = rd::ViewsFeature::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            REQUIRE(views.has_value());
            auto *placer = static_cast<rd::ViewsFeature *>(*scene.renderer->addFeature(std::move(*views)));
            auto view = rd::SceneView::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir(), 4, 4);
            REQUIRE(view.has_value());
            (*view)->renderer().setLights(&scene.lights);
            (*view)->camera().setOrthographic(4.0f);
            auto blue = scene.quad(2.0f, {0, 0, 1}, 0.0f, -5.0f, "blue");
            const TextureHandle out = scene.out();
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    // Le dépôt échoue : l'erreur est celle du dépôt, et l'image de la vue est refermée malgré tout.
                    auto failed = (*view)->render(e, {.clearColor = {1, 0, 0, 1}}, [](rd::SceneRenderer &, rd::Camera &) -> orosmo::core::Result<>
                                                  { return orosmo::core::makeError(ErrorCode::kInvalidArgument, "draw failed"); });
                    REQUIRE_FALSE(failed.has_value());
                    CHECK(failed.error().message.find("draw failed") != std::string::npos);
                    // La vue est réutilisable tout de suite (sans le correctif : « the previous frame was not ended »).
                    auto rendered = (*view)->render(e, {.clearColor = {0, 1, 0, 1}}, [&](rd::SceneRenderer &r, rd::Camera &c) { return r.drawMesh(blue, c); });
                    INFO((rendered ? std::string() : rendered.error().message));
                    REQUIRE(rendered.has_value());
                    placer->place(**view, {0, 0, 4, 4});
                    failing->fail = true;
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    CHECK_FALSE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
                });
            CHECK(placer->placementCount() == 0); // le placement ne survit pas à l'image qui a échoué
            failing->fail = false;
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    REQUIRE(scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}}).has_value());
                });
            CHECK(close(scene.at(out, 1, 1), byte(0, 0, 0))); // aucune vue composée dans l'image suivante
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("2D rendered to a texture is shown on a 3D object (R2D-05)")
    {
        FOR_EACH_BACKEND(backend)
        {
            MixScene scene(backend);
            REQUIRE(scene.gpu.device);
            rd::Layer2DFeature *texture = addLayer(scene, "mix.texture", rd::Layer2DMode::kTexture);
            REQUIRE(texture->setTextureSize(4, 4).has_value());
            texture->setClearColor({0, 1, 0, 1}); // vert, avec un bandeau bleu en bas
            auto program = scene.library->loadGraphics(shaderDir() / "mesh_textured.vert", shaderDir() / "mesh.frag", {{"OROSMO_TEXTURED", "1"}});
            INFO((program ? std::string() : program.error().message));
            REQUIRE(program.has_value());
            // Carré 8 x 8 ; uv (0, 0) en haut à gauche de la texture.
            rd::MeshData d;
            d.vertices = {-4, -4, 0, 0, 0, 1, 0, 1, //
                          4,  -4, 0, 0, 0, 1, 1, 1, //
                          4,  4,  0, 0, 0, 1, 1, 0, //
                          -4, 4,  0, 0, 0, 1, 0, 0};
            d.indices = {0, 1, 2, 0, 2, 3};
            d.attributeSizes = {3, 3, 2};
            auto mesh = rd::Mesh::create(*scene.gpu.device, d);
            REQUIRE(mesh.has_value());
            mesh->setProgram(*program);
            mesh->setPosition({0, 0, -5});
            const rd::TextureBinding binding[] = {texture->binding()};
            REQUIRE(mesh->setTextures(binding).has_value());
            const TextureHandle out = scene.out();
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    REQUIRE(scene.renderer->drawMesh(*mesh, scene.camera).has_value());
                    rd::Camera2D cam;
                    cam.setViewport(4, 4);
                    cam.setCenter({2, 2});
                    texture->begin(cam);
                    texture->renderer().rect({0, 0}, {4, 1}, {0, 0, 1, 1}); // bandeau bleu : ligne du BAS de la texture
                    const auto ended = scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                    INFO((ended ? std::string() : ended.error().message));
                    REQUIRE(ended.has_value());
                });
            INFO("backend ", tests::backendName(backend));
            CHECK(close(scene.at(out, 4, 1), byte(0, 1, 0))); // haut de l'objet : le vert de la texture
            CHECK(close(scene.at(out, 4, 7), byte(0, 0, 1))); // bas de l'objet : le bandeau bleu (la rangée 6 est filtrée entre vert et bleu)
            const auto order = scene.renderer->renderGraph().executionOrder();
            CHECK(std::ranges::find(order, "mix.texture") < std::ranges::find(order, "scene"));
            scene.gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("several views place their own cameras in zones of the output; a view shows in the interface (R2D-06)")
    {
        FOR_EACH_BACKEND(backend)
        {
            MixScene scene(backend);
            REQUIRE(scene.gpu.device);
            auto views = rd::ViewsFeature::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            REQUIRE(views.has_value());
            auto *placer = static_cast<rd::ViewsFeature *>(*scene.renderer->addFeature(std::move(*views)));
            // Vue A (4 x 8) : caméra devant un carré bleu, fond rouge. Vue B (4 x 4) : fond vert, un carré jaune plus petit.
            auto a = rd::SceneView::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir(), 4, 8);
            auto b = rd::SceneView::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir(), 4, 4);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            for (rd::SceneView *view : {a->get(), b->get()})
            {
                view->renderer().setLights(&scene.lights);
                view->camera().setNearPlane(0.1f);
                view->camera().setFarPlane(100.0f);
            }
            (*a)->camera().setOrthographic(8.0f);
            (*b)->camera().setOrthographic(4.0f);
            auto blue = scene.quad(2.0f, {0, 0, 1}, 0.0f, -5.0f, "blue");
            auto yellow = scene.quad(2.0f, {1, 1, 0}, 0.0f, -5.0f, "yellow");
            const TextureHandle out = scene.out();
            scene.gpu.run(
                [&](CommandEncoder &e)
                {
                    REQUIRE((*a)->render(e, {.clearColor = {1, 0, 0, 1}}, [&](rd::SceneRenderer &r, rd::Camera &c) { return r.drawMesh(blue, c); }).has_value());
                    REQUIRE((*b)->render(e, {.clearColor = {0, 1, 0, 1}}, [&](rd::SceneRenderer &r, rd::Camera &c) { return r.drawMesh(yellow, c); }).has_value());
                    placer->place(**a, {0, 0, 4, 8});  // moitié gauche
                    placer->place(**b, {4, 2, 4, 4});  // carré à droite, décalé vers le bas
                    REQUIRE(scene.renderer->beginFrame(e, {.clearColor = {0, 0, 0, 1}}).has_value());
                    REQUIRE(scene.renderer->setCamera(scene.camera).has_value());
                    const auto ended = scene.renderer->endFrame({out, Format::kRGBA8Unorm, {kSize, kSize}});
                    INFO((ended ? std::string() : ended.error().message));
                    REQUIRE(ended.has_value());
                });
            INFO("backend ", tests::backendName(backend));
            CHECK(close(scene.at(out, 0, 0), byte(1, 0, 0)));   // vue A : fond rouge
            CHECK(close(scene.at(out, 2, 4), byte(0, 0, 1)));   // vue A : le carré bleu (2 unités de 8 -> 1 pixel de large au centre)
            CHECK(close(scene.at(out, 1, 4), byte(0, 0, 1)));
            CHECK(close(scene.at(out, 6, 0), byte(0, 0, 0)));   // hors des vues : le fond de l'image principale
            CHECK(close(scene.at(out, 4, 2), byte(0, 1, 0)));   // vue B : fond vert
            CHECK(close(scene.at(out, 5, 4), byte(1, 1, 0)));   // vue B : le carré jaune (2 unités de 4 : 2 pixels de large)
            CHECK(close(scene.at(out, 6, 7), byte(0, 0, 0)));   // sous la vue B
            CHECK(placer->placementCount() == 0);               // les placements valent pour une image

            // La vue A dans l'interface : image externe de CanvasRenderer, dessinée sur 8 x 8 (A est 4 x 8 : étirée).
            auto canvas = rd::CanvasRenderer::create(*scene.gpu.device, *scene.library, *scene.pipelines, shaderDir());
            REQUIRE(canvas.has_value());
            auto image = std::make_shared<rd::AtlasImage>();
            image->id = rd::newAtlasId();
            image->width = 4;
            image->height = 8;
            REQUIRE((*canvas)->registerExternalImage(image->id, (*a)->texture(), (*a)->sampler()).has_value());
            CHECK((*canvas)->externalImageCount() == 1);
            rd::DrawImage cmd;
            cmd.image = image;
            const float q[] = {0, 0, 0, 0, 0, 8, 0, 1, 8, 8, 1, 1, 0, 0, 0, 0, 8, 8, 1, 1, 8, 0, 1, 0}; // x, y, u, v : v = 0 en haut
            cmd.vertices.assign(std::begin(q), std::end(q));
            rd::DrawList list;
            list.logicalSize = {8, 8};
            list.rect({0, 0}, {8, 8}, {0.2f, 0.2f, 0.2f, 1}); // fond de l'interface
            list.image(cmd);
            const TextureHandle ui = scene.out();
            scene.gpu.run([&](CommandEncoder &e) { REQUIRE((*canvas)->render(e, list, {ui, Format::kRGBA8Unorm, {kSize, kSize}}, false).has_value()); });
            CHECK(close(scene.at(ui, 0, 0), byte(1, 0, 0)));  // fond rouge de la vue
            CHECK(close(scene.at(ui, 4, 4), byte(0, 0, 1)));  // le carré bleu, étiré sur 8 pixels de large : colonnes 2 à 5
            CHECK(close(scene.at(ui, 7, 7), byte(1, 0, 0)));
            // Une image externe inconnue ou sans texture est refusée.
            CHECK_FALSE((*canvas)->registerExternalImage(0, (*a)->texture(), (*a)->sampler()).has_value());
            (*canvas)->unregisterExternalImage(image->id);
            CHECK((*canvas)->externalImageCount() == 0);
            canvas->reset();
            scene.gpu.expectNoValidationErrors();
        }
    }
}
