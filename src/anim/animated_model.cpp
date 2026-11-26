#include "orosmo/anim/animated_model.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace orosmo::anim
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Handles des textures liées par les matériaux d'un modèle dessinable (pour voir si un rechargement en a recréé une).
std::vector<rhi::TextureHandle> textureHandles(const assets::RenderModel &render)
{
    std::vector<rhi::TextureHandle> out;
    for (const auto &material : render.materials())
    {
        for (const auto &map : material->desc().maps)
        {
            out.push_back(map ? map->handle() : rhi::TextureHandle{});
        }
    }
    return out;
}

} // namespace

AnimatedModel::~AnimatedModel()
{
    // Le skinning référence le modèle dessinable, qui lie les textures du chargeur.
    skinned_.reset();
    render_.reset();
}

core::Result<std::unique_ptr<AnimatedModel>> AnimatedModel::create(const renderer::RenderContext &context, const std::filesystem::path &path,
                                                                   const AnimatedModelOptions &options)
{
    auto loader = assets::ModelLoader::create(context);
    if (!loader)
    {
        return std::unexpected(loader.error());
    }
    auto owned = std::make_unique<assets::ModelLoader>(std::move(*loader));
    auto model = create(context, *owned, path, options);
    if (model)
    {
        (*model)->ownedLoader_ = std::move(owned);
    }
    return model;
}

core::Result<std::unique_ptr<AnimatedModel>> AnimatedModel::create(const renderer::RenderContext &context, assets::ModelLoader &loader,
                                                                   const std::filesystem::path &path, const AnimatedModelOptions &options)
{
    auto model = loader.load(path);
    if (!model)
    {
        return std::unexpected(model.error());
    }
    return create(context, loader, std::move(*model), options);
}

core::Result<std::unique_ptr<AnimatedModel>> AnimatedModel::create(const renderer::RenderContext &context, assets::ModelLoader &loader, assets::Model model,
                                                                   const AnimatedModelOptions &options)
{
    std::unique_ptr<AnimatedModel> out(new AnimatedModel());
    out->context_ = &context;
    out->loader_ = &loader;
    out->options_ = options;
    out->model_ = std::move(model);
    out->parents_ = nodeParents(out->model_);
    out->rest_ = restPose(out->model_);
    out->pose_ = out->rest_;
    for (std::size_t i = 0; i < out->model_.animations.size(); ++i)
    {
        auto clip = Clip::fromModel(out->model_, i);
        if (!clip)
        {
            return std::unexpected(clip.error());
        }
        out->clips_.push_back(std::move(*clip));
    }
    if (auto built = out->buildRender(); !built)
    {
        return std::unexpected(built.error());
    }
    return out;
}

core::Result<> AnimatedModel::buildRender()
{
    // Construit à côté puis remplace : un échec (rechargement) laisse le modèle précédent intact.
    const bool gpu = !options_.forceCpuSkinning && SkinnedModel::supportedBy(context_->device().capabilities());
    assets::RenderModelOptions renderOptions = options_.render;
    renderOptions.skinnable = gpu; // tampon de sommets inscriptible par le compute seulement s'il sert
    auto created = loader_->createRenderModel(model_, renderOptions);
    if (!created)
    {
        return makeError(created.error().code, "AnimatedModel: " + created.error().message);
    }
    auto render = std::make_unique<assets::RenderModel>(std::move(*created));
    render->setRootTransform(transform_);

    std::optional<SkinnedModel> skinned;
    std::vector<CpuEntry> cpu;
    if (gpu)
    {
        auto made = SkinnedModel::create(*context_, model_, *render);
        if (!made)
        {
            return makeError(made.error().code, "AnimatedModel: skinning: " + made.error().message);
        }
        skinned.emplace(std::move(*made));
    }
    else
    {
        // Repli CPU : mêmes maillages que `SkinnedModel` (premier nœud skinné de chaque maillage GPU), sommets au repos gardés.
        std::vector<char> seen(render->meshCount(), 0);
        for (const assets::RenderModel::Item &item : render->items())
        {
            const int skin = model_.nodes[static_cast<std::size_t>(item.node)].skin;
            if (skin < 0 || seen[item.mesh] != 0)
            {
                continue;
            }
            const assets::RenderModel::MeshSource &src = render->meshSource(item.mesh);
            const assets::MeshPrimitive &source = model_.meshes[src.mesh].primitives[src.primitive];
            const std::optional<assets::MeshPrimitive> flat = assets::flatShaded(source);
            const assets::MeshPrimitive &prim = flat ? *flat : source; // comme RenderModel
            if (prim.joints.empty())
            {
                continue;
            }
            seen[item.mesh] = 1;
            const std::size_t joints = model_.skins[static_cast<std::size_t>(skin)].joints.size();
            for (const glm::u16vec4 &j : prim.joints)
            {
                if (j.x >= joints || j.y >= joints || j.z >= joints || j.w >= joints)
                {
                    return makeError(ErrorCode::kInvalidArgument, "AnimatedModel: mesh '" + model_.meshes[src.mesh].name + "' uses a joint beyond its skin");
                }
            }
            CpuEntry e;
            e.mesh = item.mesh;
            e.node = item.node;
            e.skin = skin;
            e.rest = assets::pbrVertices(prim);
            e.joints = prim.joints;
            e.weights.assign(prim.weights.begin(), prim.weights.end());
            if (e.weights.size() != e.joints.size() || render->mesh(item.mesh).vertexBufferSize() != e.rest.size() * sizeof(float))
            {
                return makeError(ErrorCode::kInvalidArgument, "AnimatedModel: mesh '" + model_.meshes[src.mesh].name + "' does not match its primitive");
            }
            cpu.push_back(std::move(e));
        }
    }
    skinned_.reset();
    render_ = std::move(render);
    if (skinned)
    {
        skinned_.emplace(std::move(*skinned));
    }
    cpu_ = std::move(cpu);
    return applyPose();
}

core::Result<> AnimatedModel::rebuild() { return buildRender(); }

core::Result<std::size_t> AnimatedModel::reloadChanged()
{
    const std::vector<rhi::TextureHandle> before = textureHandles(*render_);
    const std::size_t reloaded = loader_->reloadChanged();
    if (reloaded == 0)
    {
        return reloaded;
    }
    if (textureHandles(*render_) != before)
    {
        // Texture recréée : les groupes des matériaux lient encore l'ancienne (images en vol comprises).
        context_->device().waitIdle();
        if (auto rebuilt = buildRender(); !rebuilt)
        {
            return std::unexpected(rebuilt.error());
        }
    }
    return reloaded;
}

int AnimatedModel::clipIndex(std::string_view name) const noexcept
{
    const auto it = std::ranges::find_if(clips_, [&](const Clip &c) { return c.name() == name; });
    return it != clips_.end() ? static_cast<int>(it - clips_.begin()) : -1;
}

const std::string &AnimatedModel::current() const noexcept
{
    static const std::string kNone;
    return currentClip_ >= 0 ? clips_[static_cast<std::size_t>(currentClip_)].name() : kNone;
}

float AnimatedModel::blendWeight() const noexcept
{
    if (!previous_)
    {
        return 1.0f;
    }
    return fadeDuration_ > 0.0f ? std::clamp(fadeElapsed_ / fadeDuration_, 0.0f, 1.0f) : 1.0f;
}

std::size_t AnimatedModel::skinnedVertexCount() const noexcept
{
    if (skinned_)
    {
        return skinned_->skinnedVertexCount();
    }
    std::size_t n = 0;
    for (const CpuEntry &e : cpu_)
    {
        n += e.joints.size();
    }
    return n;
}

core::Result<> AnimatedModel::play(std::string_view clip, PlayOptions options)
{
    const int index = clipIndex(clip);
    if (index < 0)
    {
        return makeError(ErrorCode::kNotFound, "AnimatedModel::play: no clip named '" + std::string(clip) + "'");
    }
    currentClip_ = index;
    currentTime_ = 0.0f;
    currentOptions_ = options;
    previous_.reset();
    fadeElapsed_ = 0.0f;
    fadeDuration_ = 0.0f;
    if (auto r = evaluate(); !r)
    {
        return r;
    }
    return applyPose();
}

core::Result<> AnimatedModel::crossFade(std::string_view clip, float seconds, PlayOptions options)
{
    const int index = clipIndex(clip);
    if (index < 0)
    {
        return makeError(ErrorCode::kNotFound, "AnimatedModel::crossFade: no clip named '" + std::string(clip) + "'");
    }
    if (currentClip_ < 0 || !(seconds > 0.0f))
    {
        return play(clip, options);
    }
    // Comme une transition de `StateMachine` : l'état quitté garde son temps et continue d'avancer pendant le fondu.
    previous_ = Playing{currentClip_, currentTime_, currentOptions_};
    fadeElapsed_ = 0.0f;
    fadeDuration_ = seconds;
    currentClip_ = index;
    currentTime_ = 0.0f;
    currentOptions_ = options;
    if (auto r = evaluate(); !r)
    {
        return r;
    }
    return applyPose();
}

core::Result<> AnimatedModel::update(float dt)
{
    dt = std::max(dt, 0.0f);
    if (currentClip_ >= 0)
    {
        const Clip &cur = clips_[static_cast<std::size_t>(currentClip_)];
        currentTime_ = advanceClipTime(currentTime_, dt * currentOptions_.speed, cur.duration(), currentOptions_.loop).time;
        if (previous_)
        {
            const Clip &prev = clips_[static_cast<std::size_t>(previous_->clip)];
            previous_->time = advanceClipTime(previous_->time, dt * previous_->options.speed, prev.duration(), previous_->options.loop).time;
            fadeElapsed_ += dt;
            if (fadeElapsed_ >= fadeDuration_)
            {
                previous_.reset();
            }
        }
        if (auto r = evaluate(); !r)
        {
            return r;
        }
    }
    return applyPose();
}

core::Result<> AnimatedModel::setPose(const Pose &pose)
{
    if (pose.nodes.size() != rest_.nodes.size())
    {
        return makeError(ErrorCode::kInvalidArgument, "AnimatedModel::setPose: the pose does not have one transform per node");
    }
    currentClip_ = -1;
    previous_.reset();
    pose_ = pose;
    return applyPose();
}

core::Result<> AnimatedModel::evaluate()
{
    // Même calcul que `StateMachine::evaluate` : chaque clip est échantillonné sur la pose de repos, puis mélangé.
    Pose target = rest_;
    if (auto r = clips_[static_cast<std::size_t>(currentClip_)].sample(currentTime_, target); !r)
    {
        return r;
    }
    if (!previous_)
    {
        pose_ = std::move(target);
        return {};
    }
    Pose source = rest_;
    if (auto r = clips_[static_cast<std::size_t>(previous_->clip)].sample(previous_->time, source); !r)
    {
        return r;
    }
    auto mixed = blendPoses(source, target, blendWeight());
    if (!mixed)
    {
        return std::unexpected(mixed.error());
    }
    pose_ = std::move(*mixed);
    return {};
}

core::Result<> AnimatedModel::applyPose()
{
    if (skinned_)
    {
        return skinned_->setPose(pose_);
    }
    auto world = worldMatrices(parents_, pose_);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    for (const CpuEntry &e : cpu_)
    {
        const std::vector<core::Mat4> palette = jointPalette(model_.skins[static_cast<std::size_t>(e.skin)], *world, e.node);
        auto skinned = skinVertices(e.rest, e.joints, e.weights, palette);
        if (!skinned)
        {
            return std::unexpected(skinned.error());
        }
        if (auto r = render_->mesh(e.mesh).updateVertices(*skinned); !r)
        {
            return r;
        }
    }
    return render_->setNodeMatrices(std::move(*world));
}

core::Result<> AnimatedModel::dispatch(rhi::CommandEncoder &encoder) { return skinned_ ? skinned_->dispatch(encoder) : core::Result<>{}; }

core::Result<> AnimatedModel::draw(renderer::SceneRenderer &scene, const renderer::Camera &camera) { return render_->draw(scene, camera); }

void AnimatedModel::setTransform(const glm::mat4 &world) noexcept
{
    transform_ = world;
    if (render_)
    {
        render_->setRootTransform(world);
    }
}

} // namespace orosmo::anim
