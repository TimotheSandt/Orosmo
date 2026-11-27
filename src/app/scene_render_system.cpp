// Système de rendu des composants de la scène (A79 lot L13) : voir include/orosmo/app/scene_render_system.hpp.
#include <orosmo/app/scene_render_system.hpp>

#include <orosmo/anim/animated_model.hpp>
#include <orosmo/anim/crowd.hpp>
#include <orosmo/assets/render_model.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/renderer/culling.hpp>
#include <orosmo/renderer/instance_batch.hpp>
#include <orosmo/renderer/instance_renderer.hpp>
#include <orosmo/renderer/mesh.hpp>
#include <orosmo/renderer/shadow_feature.hpp>
#include <orosmo/scene/transform.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orosmo::app
{

namespace
{

using core::ErrorCode;
using core::makeError;

template <typename T> std::uint64_t keyOf(core::Handle<T> handle) noexcept
{
    return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
}

glm::mat4 worldMatrix(const scene::Transform &transform) { return scene::relativeMatrix(transform, core::WorldPosition{}); }

// Vecteur local tourné par la partie linéaire monde de l'entité, normalisé ; `fallback` s'il est nul ou non fini.
glm::vec3 rotated(const scene::Transform &transform, const core::Vec3 &local, const glm::vec3 &fallback)
{
    const glm::dvec3 v = transform.worldLinear * glm::dvec3(local);
    const double length = glm::length(v);
    if (!(length > 0.0) || !std::isfinite(length))
        return fallback;
    return glm::vec3(v / length);
}

renderer::Light toLight(const LightComponent &c, const scene::Transform &t)
{
    const glm::vec3 position(t.worldPosition);
    const glm::vec3 direction = rotated(t, c.direction, {0.0f, -1.0f, 0.0f});
    renderer::Light light;
    switch (c.type)
    {
    case renderer::LightType::kPoint:
        light = renderer::pointLight({.position = position, .color = c.color, .intensity = c.intensity, .range = c.range});
        break;
    case renderer::LightType::kSpot:
        light = renderer::spotLight({.position = position,
                                     .direction = direction,
                                     .color = c.color,
                                     .intensity = c.intensity,
                                     .range = c.range,
                                     .outerAngleDegrees = c.outerAngleDegrees,
                                     .innerAngleDegrees = c.innerAngleDegrees});
        break;
    default:
        light = renderer::directionalLight({.direction = direction, .color = c.color, .intensity = c.intensity});
        break;
    }
    light.shadowMode = c.castShadows ? renderer::ShadowMode::kDynamic : renderer::ShadowMode::kNone;
    return light;
}

bool sameLight(const renderer::Light &a, const renderer::Light &b)
{
    return a == b && a.shadowMode == b.shadowMode && a.mobility == b.mobility;
}

} // namespace

SceneResources::SceneResources() = default;
SceneResources::~SceneResources()
{
    // Les modèles animés d'abord (leurs RenderModel peuvent partager des textures avec des maillages), puis les maillages.
    animatedModels.clear();
    meshes.clear();
}

core::Result<RenderComponentTypes> registerRenderComponents(scene::World &world)
{
    RenderComponentTypes types;
    auto mesh = world.registerComponent<MeshRenderer>(std::string(kMeshRendererName));
    if (!mesh)
        return std::unexpected(mesh.error());
    auto light = world.registerComponent<LightComponent>(std::string(kLightComponentName));
    if (!light)
        return std::unexpected(light.error());
    auto camera = world.registerComponent<CameraComponent>(std::string(kCameraComponentName));
    if (!camera)
        return std::unexpected(camera.error());
    auto animated = world.registerComponent<AnimatedModelComponent>(std::string(kAnimatedModelComponentName));
    if (!animated)
        return std::unexpected(animated.error());
    types.mesh = *mesh;
    types.light = *light;
    types.camera = *camera;
    types.animatedModel = *animated;
    return types;
}

struct SceneRenderSystem::Impl
{
    const renderer::RenderContext *context = nullptr;
    scene::World *world = nullptr;
    SceneResources *resources = nullptr;
    renderer::ShadowFeature *shadows = nullptr;
    RenderComponentTypes types;
    renderer::LightManager lights;
    SceneRenderStats stats;

    // Modèles `kSingle` visibles (un par poignée), fixés par `update`.
    std::vector<core::Handle<anim::AnimatedModel>> singleModels;

    // Instanciation : un maillage et un matériau de l'InstanceRenderer par poignée.
    struct Instanced
    {
        std::uint32_t mesh = 0;
        std::uint32_t material = 0;
        const renderer::Mesh *source = nullptr;
    };
    std::unique_ptr<renderer::InstanceRenderer> instancer;
    std::unordered_map<std::uint64_t, Instanced> instanced;
    renderer::InstanceBatchBuilder batches;

    // Casters d'ombres inscrits par le système.
    struct MeshCaster
    {
        core::Handle<renderer::Mesh> handle;
        renderer::ShadowFeature::CasterId id = 0;
        bool used = true; // demandé par une entité pendant le `draw` en cours
    };
    std::vector<MeshCaster> meshCasters;
    struct ModelCasters
    {
        core::Handle<anim::AnimatedModel> handle;
        const assets::RenderModel *render = nullptr; // reconstruit (rebuild) : casters à refaire
        std::vector<renderer::ShadowFeature::CasterId> ids;
        bool used = true;
    };
    std::vector<ModelCasters> modelCasters;

    // Foules : une par poignée de modèle `kCrowd`.
    struct Crowd
    {
        core::Handle<anim::AnimatedModel> handle;
        std::unique_ptr<anim::CrowdRenderer> renderer;
        std::vector<core::Mat4> worlds;
        std::vector<std::uint32_t> clips;
        std::vector<float> times;
    };
    std::map<std::uint64_t, Crowd> crowds;
    bool crowdWarned = false;

    void forgetStale()
    {
        for (auto it = meshCasters.begin(); it != meshCasters.end();)
        {
            if (resources->meshes.contains(it->handle))
            {
                ++it;
                continue;
            }
            shadows->removeCaster(it->id);
            it = meshCasters.erase(it);
        }
        for (auto it = modelCasters.begin(); it != modelCasters.end();)
        {
            const anim::AnimatedModel *model = resources->animatedModels.get(it->handle);
            if (model != nullptr && &model->renderModel() == it->render)
            {
                ++it;
                continue;
            }
            for (const auto id : it->ids)
                shadows->removeCaster(id);
            it = modelCasters.erase(it);
        }
        for (auto it = instanced.begin(); it != instanced.end();)
        {
            const renderer::Mesh *mesh = resources->meshes.get(core::Handle<renderer::Mesh>{static_cast<std::uint32_t>(it->first >> 32),
                                                                                           static_cast<std::uint32_t>(it->first)});
            it = (mesh == it->second.source) ? std::next(it) : instanced.erase(it);
        }
        for (auto it = crowds.begin(); it != crowds.end();)
            it = resources->animatedModels.contains(it->second.handle) ? std::next(it) : crowds.erase(it);
    }

    // Début d'un `draw` : plus aucun caster n'est demandé.
    void resetCasterUse() noexcept
    {
        for (MeshCaster &c : meshCasters)
            c.used = false;
        for (ModelCasters &c : modelCasters)
            c.used = false;
    }

    // Fin d'un `draw` réussi : les casters qu'aucune entité n'a demandés (détruite, cachée, sans ombre, autre mode) partent.
    void removeUnusedCasters()
    {
        std::erase_if(meshCasters,
                      [this](const MeshCaster &c)
                      {
                          if (!c.used)
                              shadows->removeCaster(c.id);
                          return !c.used;
                      });
        std::erase_if(modelCasters,
                      [this](const ModelCasters &c)
                      {
                          if (!c.used)
                              for (const auto id : c.ids)
                                  shadows->removeCaster(id);
                          return !c.used;
                      });
    }

    core::Result<> ensureMeshCaster(core::Handle<renderer::Mesh> handle, const renderer::Mesh &mesh)
    {
        if (shadows == nullptr)
            return {};
        for (MeshCaster &c : meshCasters)
            if (c.handle == handle)
            {
                c.used = true;
                return {};
            }
        auto id = shadows->addMeshCaster(mesh, renderer::Aabb::empty(), renderer::CasterMobility::kMovable);
        if (!id)
            return std::unexpected(id.error());
        meshCasters.push_back({handle, *id});
        return {};
    }

    core::Result<> ensureModelCasters(core::Handle<anim::AnimatedModel> handle, anim::AnimatedModel &model)
    {
        if (shadows == nullptr)
            return {};
        for (ModelCasters &c : modelCasters)
            if (c.handle == handle)
            {
                c.used = true;
                return {};
            }
        ModelCasters casters{handle, &model.renderModel(), {}};
        for (std::size_t i = 0; i < model.renderModel().meshCount(); ++i)
        {
            auto id = shadows->addMeshCaster(model.renderModel().mesh(i), renderer::Aabb::empty(), renderer::CasterMobility::kMovable);
            if (!id)
            {
                for (const auto added : casters.ids)
                    shadows->removeCaster(added);
                return std::unexpected(id.error());
            }
            casters.ids.push_back(*id);
        }
        modelCasters.push_back(std::move(casters));
        return {};
    }

    // Entrée d'instanciation d'un maillage ; nul si le maillage ne peut pas être instancié (dessiné seul).
    core::Result<const Instanced *> instancedEntry(core::Handle<renderer::Mesh> handle, const renderer::Mesh &mesh)
    {
        if (auto it = instanced.find(keyOf(handle)); it != instanced.end())
        {
            // Revue PR #37 : les valeurs copiées du maillage (groupe de textures, push constants, faces) ont pu changer depuis
            // (`Mesh::setTextures` détruit l'ancien groupe) : le matériau d'instance suit, l'entrée reste.
            const renderer::InstanceMaterial *cached = instancer->material(it->second.material);
            if (cached != nullptr && (cached->textures != mesh.textureGroup() || cached->pushConstants != mesh.pushConstants() ||
                                      cached->cull != mesh.cullMode()))
            {
                renderer::InstanceMaterial material = *cached;
                material.textures = mesh.textureGroup();
                material.pushConstants = mesh.pushConstants();
                material.cull = mesh.cullMode();
                (void)instancer->setMaterial(it->second.material, std::move(material));
            }
            return &it->second;
        }
        if (mesh.material() != nullptr || mesh.instanced())
            return static_cast<const Instanced *>(nullptr); // PBR ou instances propres : G352
        if (!instancer)
        {
            auto created = renderer::InstanceRenderer::create(*context);
            if (!created)
                return std::unexpected(created.error());
            instancer = std::move(*created);
        }
        auto id = instancer->addMesh(mesh);
        if (!id)
            return static_cast<const Instanced *>(nullptr);
        renderer::InstanceMaterial material;
        material.textures = mesh.textureGroup();
        material.pushConstants = mesh.pushConstants();
        material.cull = mesh.cullMode();
        const std::uint32_t materialId = instancer->addMaterial(std::move(material));
        auto [it, inserted] = instanced.emplace(keyOf(handle), Instanced{*id, materialId, &mesh});
        (void)inserted;
        return &it->second;
    }

    core::Result<> syncLights()
    {
        std::vector<renderer::Light> wanted;
        renderer::Light ambient = renderer::ambientLight(glm::vec3(1.0f), 0.0f);
        world->view(world->transformType(), types.light)
            .each(
                [&](scene::Entity, scene::Transform &t, LightComponent &c)
                {
                    if (c.type == renderer::LightType::kNone)
                        return;
                    if (c.type == renderer::LightType::kAmbient)
                    {
                        ambient = renderer::ambientLight(c.color, c.intensity);
                        return;
                    }
                    wanted.push_back(toLight(c, t));
                });
        if (!(lights.ambient() == ambient))
            lights.setAmbientLight(ambient.color, ambient.strength);
        const std::size_t common = std::min(wanted.size(), lights.lights().size());
        for (std::size_t i = 0; i < common; ++i)
        {
            if (!sameLight(lights.lights()[i], wanted[i]))
            {
                if (auto set = lights.setLight(i, wanted[i]); !set)
                    return set;
            }
        }
        while (lights.lights().size() > wanted.size())
            lights.removeLight(lights.lights().size() - 1);
        for (std::size_t i = common; i < wanted.size(); ++i)
            lights.addLight(wanted[i]);
        stats.lights = static_cast<std::uint32_t>(wanted.size());
        if (lights.dirty())
            return lights.upload();
        return {};
    }
};

SceneRenderSystem::SceneRenderSystem(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

SceneRenderSystem::~SceneRenderSystem()
{
    if (!impl_ || impl_->shadows == nullptr)
        return;
    for (const auto &c : impl_->meshCasters)
        impl_->shadows->removeCaster(c.id);
    for (const auto &m : impl_->modelCasters)
        for (const auto id : m.ids)
            impl_->shadows->removeCaster(id);
}

core::Result<std::unique_ptr<SceneRenderSystem>> SceneRenderSystem::create(const renderer::RenderContext &context, scene::World &world,
                                                                           SceneResources &resources, renderer::ShadowFeature *shadows)
{
    auto types = registerRenderComponents(world);
    if (!types)
        return std::unexpected(types.error());
    auto lights = renderer::LightManager::create(context.device());
    if (!lights)
        return std::unexpected(lights.error());
    auto impl = std::make_unique<Impl>();
    impl->context = &context;
    impl->world = &world;
    impl->resources = &resources;
    impl->shadows = shadows;
    impl->types = *types;
    impl->lights = std::move(*lights);
    if (auto uploaded = impl->lights.upload(); !uploaded)
        return std::unexpected(uploaded.error());
    return std::unique_ptr<SceneRenderSystem>(new SceneRenderSystem(std::move(impl)));
}

core::Result<> SceneRenderSystem::update(float dt)
{
    Impl &s = *impl_;
    if (!std::isfinite(dt))
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderSystem::update: non-finite delta");
    s.world->updateTransforms();

    s.singleModels.clear();
    s.world->view(s.types.animatedModel)
        .each(
            [&](scene::Entity, AnimatedModelComponent &c)
            {
                if (!c.visible || c.mode != DrawMode::kSingle || !s.resources->animatedModels.contains(c.model))
                    return;
                for (const auto &h : s.singleModels)
                    if (h == c.model)
                        return;
                s.singleModels.push_back(c.model);
            });
    for (const auto &handle : s.singleModels)
    {
        if (auto updated = s.resources->animatedModels.get(handle)->update(dt); !updated)
            return updated;
    }
    return s.syncLights();
}

bool SceneRenderSystem::applyCamera(renderer::Camera &camera) const
{
    const Impl &s = *impl_;
    bool found = false;
    s.world->view(s.world->transformType(), s.types.camera)
        .each(
            [&](scene::Entity, scene::Transform &t, CameraComponent &c)
            {
                if (found || !c.active)
                    return;
                found = true;
                camera.setPosition(glm::vec3(t.worldPosition));
                camera.setForward(rotated(t, c.forward, {0.0f, 0.0f, -1.0f}));
                camera.setUp(rotated(t, c.up, {0.0f, 1.0f, 0.0f}));
                camera.setFovDegrees(c.fovDegrees);
                camera.setNearPlane(c.nearPlane);
                camera.setFarPlane(c.farPlane);
            });
    return found;
}

core::Result<> SceneRenderSystem::dispatch(rhi::CommandEncoder &encoder)
{
    Impl &s = *impl_;
    for (const auto &handle : s.singleModels)
    {
        anim::AnimatedModel *model = s.resources->animatedModels.get(handle);
        if (model == nullptr)
            continue;
        if (auto ok = model->dispatch(encoder); !ok)
            return ok;
    }
    return {};
}

core::Result<SceneRenderStats> SceneRenderSystem::draw(renderer::SceneRenderer &scene, const renderer::Camera &camera)
{
    Impl &s = *impl_;
    const std::uint32_t lightCount = s.stats.lights;
    s.stats = {};
    s.stats.lights = lightCount;
    s.forgetStale();
    s.resetCasterUse();

    core::Result<> failure;
    std::unordered_map<const renderer::Mesh *, std::uint32_t> drawsOfMesh; // emplacements d'historique (vecteurs de mouvement)
    s.batches.clear();

    // ── Maillages ─────────────────────────────────────────────────────────────
    s.world->view(s.world->transformType(), s.types.mesh)
        .each(
            [&](scene::Entity, scene::Transform &t, MeshRenderer &c)
            {
                if (!failure || !c.visible)
                    return;
                renderer::Mesh *mesh = s.resources->meshes.get(c.mesh);
                if (mesh == nullptr)
                {
                    ++s.stats.skipped;
                    return;
                }
                if (c.castShadows)
                {
                    if (auto added = s.ensureMeshCaster(c.mesh, *mesh); !added)
                    {
                        failure = added;
                        return;
                    }
                }
                const glm::mat4 model = worldMatrix(t);
                if (c.mode != DrawMode::kSingle)
                {
                    auto entry = s.instancedEntry(c.mesh, *mesh);
                    if (!entry)
                    {
                        failure = std::unexpected(entry.error());
                        return;
                    }
                    if (*entry != nullptr)
                    {
                        if (c.castShadows)
                            mesh->setModelMatrix(model); // le caster suit la dernière transformation donnée au maillage
                        s.batches.add({(*entry)->mesh, (*entry)->material}, model);
                        return;
                    }
                }
                mesh->setHistorySlot(drawsOfMesh[mesh]++);
                mesh->setModelMatrix(model);
                if (auto drawn = scene.drawMesh(*mesh); !drawn)
                {
                    failure = drawn;
                    return;
                }
                ++s.stats.meshDraws;
            });
    if (!failure)
        return std::unexpected(failure.error());
    if (s.batches.size() > 0)
    {
        s.batches.build();
        auto drawn = s.instancer->draw(scene, s.batches, camera.wireframe());
        if (!drawn)
            return std::unexpected(drawn.error());
        s.stats.instancedBatches = drawn->drawCalls;
        s.stats.instances = drawn->instances;
    }

    // ── Modèles animés ────────────────────────────────────────────────────────
    for (auto &[key, crowd] : s.crowds)
    {
        crowd.worlds.clear();
        crowd.clips.clear();
        crowd.times.clear();
    }
    s.world->view(s.world->transformType(), s.types.animatedModel)
        .each(
            [&](scene::Entity, scene::Transform &t, AnimatedModelComponent &c)
            {
                if (!failure || !c.visible)
                    return;
                anim::AnimatedModel *model = s.resources->animatedModels.get(c.model);
                if (model == nullptr)
                {
                    ++s.stats.skipped;
                    return;
                }
                const glm::mat4 world = worldMatrix(t);
                if (c.mode != DrawMode::kSingle)
                {
                    if (c.clip >= model->clips().size() || !std::isfinite(c.time))
                    {
                        ++s.stats.skipped;
                        return;
                    }
                    Impl::Crowd &crowd = s.crowds[keyOf(c.model)];
                    crowd.handle = c.model;
                    crowd.worlds.push_back(world);
                    crowd.clips.push_back(c.clip);
                    crowd.times.push_back(c.time);
                    return;
                }
                if (c.castShadows)
                {
                    if (auto added = s.ensureModelCasters(c.model, *model); !added)
                    {
                        failure = added;
                        return;
                    }
                }
                model->setTransform(world);
                if (auto drawn = model->draw(scene, camera); !drawn)
                {
                    failure = drawn;
                    return;
                }
                ++s.stats.animatedDraws;
            });
    if (!failure)
        return std::unexpected(failure.error());

    for (auto &[key, crowd] : s.crowds)
    {
        if (crowd.worlds.empty())
            continue;
        if (!crowd.renderer)
        {
            if (!anim::CrowdRenderer::supportedBy(s.context->device().capabilities()))
            {
                // Règle 11 : refus propre, une fois au journal.
                if (!s.crowdWarned)
                    LOG_WARNING("SceneRenderSystem: crowds are not supported by this device; kCrowd entities are not drawn");
                s.crowdWarned = true;
                s.stats.skipped += static_cast<std::uint32_t>(crowd.worlds.size());
                continue;
            }
            anim::AnimatedModel *model = s.resources->animatedModels.get(crowd.handle);
            auto created = anim::CrowdRenderer::create(*s.context, *model);
            if (!created)
                return std::unexpected(created.error());
            crowd.renderer = std::move(*created);
        }
        if (auto set = crowd.renderer->setAgents(crowd.worlds, crowd.clips, crowd.times); !set)
            return std::unexpected(set.error());
        auto drawn = crowd.renderer->draw(scene, camera.wireframe());
        if (!drawn)
            return std::unexpected(drawn.error());
        ++s.stats.crowds;
        s.stats.crowdAgents += static_cast<std::uint32_t>(crowd.worlds.size());
    }

    s.removeUnusedCasters(); // vide sans plugin d'ombres
    std::uint32_t casters = static_cast<std::uint32_t>(s.meshCasters.size());
    for (const auto &m : s.modelCasters)
        casters += static_cast<std::uint32_t>(m.ids.size());
    s.stats.shadowCasters = casters;
    return s.stats;
}

renderer::LightManager &SceneRenderSystem::lights() noexcept { return impl_->lights; }
const RenderComponentTypes &SceneRenderSystem::types() const noexcept { return impl_->types; }
const SceneRenderStats &SceneRenderSystem::stats() const noexcept { return impl_->stats; }
scene::World &SceneRenderSystem::world() const noexcept { return *impl_->world; }

} // namespace orosmo::app
