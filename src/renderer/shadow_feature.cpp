#include "orosmo/renderer/shadow_feature.hpp"

#include "orosmo/renderer/frame_data.hpp"
#include "orosmo/renderer/instance_renderer.hpp"
#include "orosmo/renderer/mesh.hpp"
#include "orosmo/renderer/object_set.hpp"

#include <orosmo/core/log.hpp>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
constexpr rhi::Format kShadowFormat = rhi::Format::kD32Float;

bool sameShape(const Light &a, const Light &b) noexcept
{
    return a.type == b.type && a.position == b.position && a.direction == b.direction && a.outerCone == b.outerCone && a.innerCone == b.innerCone;
}
bool sameColor(const Light &a, const Light &b) noexcept { return a.color == b.color && a.strength == b.strength; }

glm::vec4 tileRect(const AtlasTile &tile, std::uint32_t atlasSize) noexcept
{
    const float s = static_cast<float>(atlasSize);
    return {static_cast<float>(tile.x) / s, static_cast<float>(tile.y) / s, static_cast<float>(tile.x + tile.size) / s,
            static_cast<float>(tile.y + tile.size) / s};
}
} // namespace

core::Result<std::unique_ptr<ShadowFeature>> ShadowFeature::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                   const std::filesystem::path &shaderDirectory, const ShadowSettings &settings)
{
    std::unique_ptr<ShadowFeature> f(new ShadowFeature(device, library, pipelines));
    f->shaderDirectory_ = shaderDirectory;
    const auto load = [&](std::shared_ptr<ShaderProgram> &out, const char *vert, const char *frag, const std::vector<Define> &defines) -> core::Result<>
    {
        auto program = library.loadGraphics(shaderDirectory / vert, shaderDirectory / frag, defines);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        out = *program;
        return {};
    };
    for (auto r : {load(f->casterProgram_, "shadow_caster.vert", "shadow_caster.frag", {}),
                   load(f->instancedCasterProgram_, "shadow_caster_instanced.vert", "shadow_caster.frag", {}),
                   load(f->alphaCasterProgram_, "shadow_caster.vert", "shadow_caster.frag", {{"OROSMO_SHADOW_ALPHA", "1"}}),
                   load(f->clearProgram_, "shadow_tile.vert", "shadow_tile.frag", {}),
                   load(f->copyProgram_, "shadow_tile.vert", "shadow_tile.frag", {{"OROSMO_SHADOW_COPY", "1"}}),
                   load(f->receiverProgram_, "mesh.vert", "mesh.frag", {{"OROSMO_SHADOWS", "1"}}),
                   load(f->instancedReceiverProgram_, "mesh_instanced.vert", "mesh.frag", {{"OROSMO_INSTANCED", "1"}, {"OROSMO_SHADOWS", "1"}})})
    {
        if (!r)
        {
            return std::unexpected(r.error());
        }
    }
    auto compare = device.createSampler({.minFilter = rhi::Filter::kLinear,
                                         .magFilter = rhi::Filter::kLinear,
                                         .mipFilter = std::nullopt,
                                         .addressU = rhi::AddressMode::kClampToEdge,
                                         .addressV = rhi::AddressMode::kClampToEdge,
                                         .addressW = rhi::AddressMode::kClampToEdge,
                                         .compare = rhi::CompareOp::kLessEqual,
                                         .debugName = "shadow.compare"});
    if (!compare)
    {
        return std::unexpected(compare.error());
    }
    f->compareSampler_ = *compare;
    auto point = device.createSampler({.minFilter = rhi::Filter::kNearest,
                                       .magFilter = rhi::Filter::kNearest,
                                       .mipFilter = std::nullopt,
                                       .addressU = rhi::AddressMode::kClampToEdge,
                                       .addressV = rhi::AddressMode::kClampToEdge,
                                       .addressW = rhi::AddressMode::kClampToEdge,
                                       .debugName = "shadow.point"});
    if (!point)
    {
        return std::unexpected(point.error());
    }
    f->pointSampler_ = *point;
    rhi::BufferDesc data;
    data.size = sizeof(ShadowGpuData);
    data.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    data.debugName = "shadow.data";
    auto buffer = GpuBuffer::create(device, data, std::as_bytes(std::span(&f->gpu_, 1)));
    if (!buffer)
    {
        return std::unexpected(buffer.error());
    }
    f->dataBuffer_ = std::move(*buffer);
    if (auto r = f->setSettings(settings); !r)
    {
        return std::unexpected(r.error());
    }
    return f;
}

ShadowFeature::~ShadowFeature()
{
    destroyAtlases();
    device_.destroy(compareSampler_);
    device_.destroy(pointSampler_);
}

void ShadowFeature::destroyAtlases() noexcept
{
    device_.destroy(receiverGroup_);
    device_.destroy(copyGroup_);
    device_.destroy(atlas_);
    device_.destroy(cache_);
    receiverGroup_ = {};
    copyGroup_ = {};
    atlas_ = {};
    cache_ = {};
}

core::Result<> ShadowFeature::setSettings(const ShadowSettings &settings)
{
    ShadowSettings s = settings;
    s.atlasSize = nextPowerOfTwo(std::clamp(s.atlasSize, 64u, std::max(64u, device_.capabilities().maxTextureSize)));
    if (s.atlasSize > device_.capabilities().maxTextureSize && device_.capabilities().maxTextureSize > 0)
    {
        s.atlasSize = std::bit_floor(device_.capabilities().maxTextureSize);
    }
    s.minTileSize = std::min(nextPowerOfTwo(std::max(s.minTileSize, 16u)), s.atlasSize);
    s.cascadeCount = std::clamp(s.cascadeCount, 1u, kMaxShadowCascades);
    s.cascadeResolution = std::clamp(nextPowerOfTwo(s.cascadeResolution), s.minTileSize, s.atlasSize);
    s.spotResolution = std::clamp(nextPowerOfTwo(s.spotResolution), s.minTileSize, s.atlasSize);
    s.maxSpotShadows = std::min(s.maxSpotShadows, kMaxSpotShadows);
    s.pcfRadius = std::clamp(s.pcfRadius, 0, 3);
    for (std::uint32_t &interval : s.cascadeUpdateInterval)
    {
        interval = std::max(interval, 1u);
    }
    const bool recreate = atlas_.isNull() || s.atlasSize != settings_.atlasSize || s.minTileSize != settings_.minTileSize;
    if (recreate)
    {
        // Remplaçants construits d'abord ; en cas d'échec, réglages, atlas, cache et groupes actifs restent intacts.
        if (auto r = createAtlases(s); !r)
        {
            return r;
        }
    }
    settings_ = s;
    invalidate();
    return {};
}

core::Result<> ShadowFeature::createAtlases(const ShadowSettings &settings)
{
    rhi::TextureHandle atlas;
    rhi::TextureHandle cache;
    rhi::BindGroupHandle receiverGroup;
    rhi::BindGroupHandle copyGroup;
    const auto discard = [&]
    {
        device_.destroy(receiverGroup);
        device_.destroy(copyGroup);
        device_.destroy(atlas);
        device_.destroy(cache);
    };
    for (auto [texture, name] : {std::pair{&atlas, "shadow.atlas"}, std::pair{&cache, "shadow.cache"}})
    {
        rhi::TextureDesc desc;
        desc.format = kShadowFormat;
        desc.size = {settings.atlasSize, settings.atlasSize, 1};
        desc.usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kSampled;
        desc.debugName = name;
        auto created = device_.createTexture(desc);
        if (!created)
        {
            discard();
            return std::unexpected(created.error());
        }
        *texture = *created;
    }
    rhi::BindGroupDesc receiver;
    appendReceiverBindings(receiver, atlas);
    receiver.debugName = "shadow.receiver";
    auto group = device_.createBindGroup(receiver);
    if (!group)
    {
        discard();
        return std::unexpected(group.error());
    }
    receiverGroup = *group;
    rhi::BindGroupDesc copy;
    copy.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = cache, .sampler = pointSampler_});
    copy.debugName = "shadow.copy";
    auto copyCreated = device_.createBindGroup(copy);
    if (!copyCreated)
    {
        discard();
        return std::unexpected(copyCreated.error());
    }
    copyGroup = *copyCreated;
    // Succès : les anciens sont détruits, les remplaçants publiés (le groupe récepteur change : à relire par les matériaux).
    destroyAtlases();
    atlas_ = atlas;
    cache_ = cache;
    receiverGroup_ = receiverGroup;
    copyGroup_ = copyGroup;
    allocator_.reset(settings.atlasSize, settings.minTileSize);
    views_.clear();
    atlasFresh_ = true;
    cacheFresh_ = true;
    return {};
}

void ShadowFeature::appendReceiverBindings(rhi::BindGroupDesc &desc) const { appendReceiverBindings(desc, atlas_); }

void ShadowFeature::appendReceiverBindings(rhi::BindGroupDesc &desc, rhi::TextureHandle atlas) const
{
    desc.entries.push_back({.binding = kShadowAtlasBinding, .type = rhi::BindingType::kSampledTexture, .texture = atlas, .sampler = compareSampler_});
    desc.entries.push_back({.binding = kShadowDataBinding, .type = rhi::BindingType::kUniformBuffer, .buffer = dataBuffer_.handle(), .size = sizeof(ShadowGpuData)});
}

core::Result<std::shared_ptr<ShaderProgram>> ShadowFeature::receiverProgramWith(const std::vector<Define> &extraDefines, bool instanced) const
{
    std::vector<Define> defines = extraDefines;
    defines.emplace_back("OROSMO_SHADOWS", "1");
    if (instanced)
    {
        defines.emplace_back("OROSMO_INSTANCED", "1");
    }
    return library_.loadGraphics(shaderDirectory_ / (instanced ? "mesh_instanced.vert" : "mesh.vert"), shaderDirectory_ / "mesh.frag", defines);
}

void ShadowFeature::setLightParams(std::size_t lightIndex, const LightShadowParams &params) { params_[lightIndex] = params; }

LightShadowParams ShadowFeature::paramsFor(std::size_t light) const
{
    const auto it = params_.find(light);
    return it != params_.end() ? it->second : LightShadowParams{};
}

ShadowStatus ShadowFeature::lightStatus(std::size_t lightIndex) const noexcept
{
    return lightIndex < statuses_.size() ? statuses_[lightIndex] : ShadowStatus::kNone;
}

void ShadowFeature::invalidate() noexcept
{
    for (auto &[key, state] : views_)
    {
        state.rendered = false;
        state.cacheValid = false;
    }
}

core::Result<ShadowFeature::CasterId> ShadowFeature::addMeshCaster(const Mesh &mesh, const Aabb &localBounds, CasterMobility mobility)
{
    if (!mesh.valid() || mesh.indexCount() == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "ShadowFeature::addMeshCaster: empty mesh");
    }
    if (mesh.instanced())
    {
        return makeError(ErrorCode::kUnsupported, "ShadowFeature::addMeshCaster: meshes with their own instance data are not supported (use an ObjectSet)");
    }
    Caster c;
    c.id = nextCaster_++;
    c.mesh = {&mesh, localBounds, mobility, {}};
    casters_.push_back(c);
    return c.id;
}

ShadowFeature::CasterId ShadowFeature::addObjectCasters(ObjectSet &objects, const InstanceRenderer &meshes, CasterMobility mobility)
{
    Caster c;
    c.id = nextCaster_++;
    c.object = true;
    c.objects = {&objects, &meshes, mobility};
    casters_.push_back(c);
    return c.id;
}

core::Result<> ShadowFeature::setCasterTransparency(CasterId id, const TransparentShadow &shadow)
{
    for (Caster &c : casters_)
    {
        if (c.id != id)
        {
            continue;
        }
        if (c.object)
        {
            return makeError(ErrorCode::kUnsupported, "ShadowFeature::setCasterTransparency: ObjectSet casters only cast full shadows");
        }
        if (!std::isfinite(shadow.alpha) || !std::isfinite(shadow.cutoff))
        {
            return makeError(ErrorCode::kInvalidArgument, "ShadowFeature::setCasterTransparency: alpha and cutoff must be finite");
        }
        c.mesh.transparency = shadow;
        return {};
    }
    return makeError(ErrorCode::kNotFound, "ShadowFeature::setCasterTransparency: unknown caster");
}

void ShadowFeature::removeCaster(CasterId id)
{
    std::erase_if(casters_, [&](const Caster &c) { return c.id == id; });
}

// ── Préparation de l'image (CPU) ───────────────────────────────────────────────────────────────────────────────────

ShadowViewFit ShadowFeature::fitFor(const Request &request, std::uint32_t resolution) const
{
    if (request.spot)
    {
        return fitSpot(request.light.position, request.light.direction, request.halfAngle, request.lightParams.spotNear, request.lightParams.spotRange,
                       resolution);
    }
    return fitCascade(request.camera, request.sliceNear, request.sliceFar, request.light.direction, resolution, settings_.depthExtension);
}

void ShadowFeature::invalidatePending() noexcept
{
    for (const ViewKey &key : pending_)
    {
        const auto it = views_.find(key);
        if (it != views_.end())
        {
            it->second.rendered = false;
            it->second.cacheValid = false;
        }
    }
    pending_.clear();
}

void ShadowFeature::collectRequests(const FrameData &frame, std::vector<Request> &requests)
{
    const glm::dvec3 world = glm::dvec3(frame.cameraWorldHigh) + glm::dvec3(frame.cameraWorldLow);
    const glm::vec3 origin = glm::vec3(world - glm::dvec3(frame.cameraRelativeOrigin));
    const CameraShape camera = cameraShapeFrom(frame.view, frame.proj, origin);
    const std::vector<Light> &lights = lights_->lights();
    bool directional = false;
    std::vector<Request> spots;
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        const Light &light = lights[i];
        const ShadowBehavior behavior = resolveShadowBehavior(light.mobility, light.shadowMode);
        statuses_[i] = behavior.status;
        if (!behavior.realtime)
        {
            continue;
        }
        const LightShadowParams params = paramsFor(i);
        if (light.type == LightType::kDirectional)
        {
            if (directional)
            {
                statuses_[i] = ShadowStatus::kUnsupportedType; // une seule directionnelle ombrée (la première)
                continue;
            }
            directional = true;
            const std::uint32_t resolution =
                params.maxResolution > 0 ? std::clamp(nextPowerOfTwo(params.maxResolution), settings_.minTileSize, settings_.atlasSize) : settings_.cascadeResolution;
            const float farDistance = std::max(settings_.maxDistance, camera.nearPlane * 2.0f);
            const std::vector<float> splits = cascadeSplits(camera.nearPlane, farDistance, settings_.cascadeCount, settings_.splitLambda);
            float previous = camera.nearPlane;
            for (std::uint32_t c = 0; c < splits.size(); ++c)
            {
                Request r;
                r.key = {i, c};
                r.size = resolution;
                r.priority = params.priority;
                r.distance = static_cast<float>(c);
                r.camera = camera;
                r.light = light;
                r.sliceNear = previous;
                r.sliceFar = splits[c];
                r.fit = fitFor(r, resolution);
                r.behavior = behavior;
                r.interval = settings_.cascadeUpdateInterval[c];
                requests.push_back(r);
                previous = splits[c];
            }
        }
        else if (light.type == LightType::kSpot)
        {
            const float distance = glm::length(light.position - camera.position);
            if (distance >= params.fadeEnd)
            {
                statuses_[i] = ShadowStatus::kFaded;
                continue;
            }
            std::uint32_t resolution =
                params.maxResolution > 0 ? std::clamp(nextPowerOfTwo(params.maxResolution), settings_.minTileSize, settings_.atlasSize) : settings_.spotResolution;
            // Importance à l'écran : la résolution est divisée par deux à chaque doublement de la distance au-delà de la portée.
            for (float d = std::max(params.spotRange, 1.0f); distance > d && resolution > settings_.minTileSize; d *= 2.0f)
            {
                resolution /= 2;
            }
            // `outerCone` : cosinus du demi-angle extérieur (convention de spotLight) ; -1 : 180° (refusé par fitSpot, REN-08) ;
            // autres valeurs hors de (-1, 1) : 45°.
            const float halfAngle = light.outerCone == -1.0f ? glm::pi<float>()
                                    : light.outerCone > -1.0f && light.outerCone < 1.0f ? std::acos(light.outerCone)
                                                                                         : glm::radians(45.0f);
            Request r;
            r.key = {i, 0};
            r.size = resolution;
            r.priority = params.priority;
            r.spot = true;
            r.distance = distance;
            r.light = light;
            r.halfAngle = halfAngle;
            r.lightParams = params;
            r.fit = fitFor(r, resolution);
            if (!r.fit.valid)
            {
                statuses_[i] = ShadowStatus::kUnsupportedType; // cône trop large pour une seule perspective : refusé, jamais tronqué
                continue;
            }
            r.behavior = behavior;
            spots.push_back(r);
        }
        else
        {
            statuses_[i] = ShadowStatus::kUnsupportedType; // ponctuelle (cubemaps, REN-09) ou ambiante
        }
    }
    // Projecteurs : les plus prioritaires puis les plus proches, au plus maxSpotShadows.
    std::stable_sort(spots.begin(), spots.end(),
                     [](const Request &a, const Request &b) { return a.priority != b.priority ? a.priority > b.priority : a.distance < b.distance; });
    for (std::size_t s = 0; s < spots.size(); ++s)
    {
        if (s < settings_.maxSpotShadows)
        {
            requests.push_back(spots[s]);
        }
        else
        {
            statuses_[spots[s].key.light] = ShadowStatus::kAtlasFull;
        }
    }
    // Ordre de service (atlas, budget) : priorité de la lumière, directionnelle avant projecteurs, cascades proches d'abord.
    std::stable_sort(requests.begin(), requests.end(),
                     [](const Request &a, const Request &b)
                     {
                         if (a.priority != b.priority)
                         {
                             return a.priority > b.priority;
                         }
                         if (a.spot != b.spot)
                         {
                             return !a.spot;
                         }
                         return a.distance < b.distance;
                     });
}

void ShadowFeature::allocateTiles(std::vector<Request> &requests)
{
    for (auto &[key, state] : views_)
    {
        state.requested = false;
    }
    for (const Request &r : requests)
    {
        ViewState &state = views_[r.key];
        state.requested = true;
        // Taille demandée changée : la tuile est rendue (une tuile réduite faute de place reste tant que la demande ne change pas).
        if (state.tile && state.requestedSize != r.size)
        {
            allocator_.release(*state.tile);
            state.tile.reset();
        }
        state.requestedSize = r.size;
    }
    for (auto it = views_.begin(); it != views_.end();)
    {
        if (!it->second.requested)
        {
            if (it->second.tile)
            {
                allocator_.release(*it->second.tile);
            }
            it = views_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        ViewState &state = views_[requests[i].key];
        if (state.tile)
        {
            continue;
        }
        std::uint32_t size = requests[i].size;
        while (true)
        {
            if (auto tile = allocator_.allocate(size))
            {
                state.tile = *tile;
                state.rendered = false;
                state.cacheValid = false;
                break;
            }
            // Place prise à une vue moins prioritaire (la dernière servie), qui retentera à son tour.
            bool evicted = false;
            for (std::size_t j = requests.size(); j-- > i + 1;)
            {
                ViewState &other = views_[requests[j].key];
                if (other.tile)
                {
                    allocator_.release(*other.tile);
                    other.tile.reset();
                    other.rendered = false;
                    other.cacheValid = false;
                    evicted = true;
                    break;
                }
            }
            if (evicted)
            {
                continue;
            }
            if (size > settings_.minTileSize)
            {
                size /= 2;
                continue;
            }
            ++stats_.dropped;
            break;
        }
    }
    // Une lumière dont AUCUNE vue n'a de tuile n'a pas d'ombre (kAtlasFull) ; une cascade manquante seule est comptée (dropped).
    for (const Request &r : requests)
    {
        bool any = false;
        for (const Request &other : requests)
        {
            if (other.key.light == r.key.light && views_[other.key].tile)
            {
                any = true;
            }
        }
        if (!any)
        {
            statuses_[r.key.light] = ShadowStatus::kAtlasFull;
        }
    }
}

std::uint64_t ShadowFeature::gatherCasters(const ShadowViewFit &fit, const glm::vec3 &eye, CasterMobility mobility, std::vector<CasterDraw> &draws)
{
    ShadowHash hash;
    const Frustum frustum = Frustum::fromMatrix(fit.viewProjection);
    for (const Caster &caster : casters_)
    {
        if (!caster.object)
        {
            if (caster.mesh.mobility != mobility || caster.mesh.mesh == nullptr || !caster.mesh.mesh->valid())
            {
                continue;
            }
            const TransparentShadow &transparency = caster.mesh.transparency;
            if (transparency.mode == TransparentShadowMode::kNone)
            {
                continue; // REN-39 : ne projette aucune ombre (ni dessin, ni empreinte)
            }
            const glm::mat4 model = caster.mesh.mesh->modelMatrix();
            if (caster.mesh.localBounds.valid() && frustum.test(caster.mesh.localBounds.transformed(model)) == CullResult::kOutside)
            {
                continue;
            }
            draws.push_back({caster.mesh.mesh, false, fit.viewProjection * model, 0, 1, transparency});
            hash.value(caster.id);
            hash.value(model);
            if (transparency.mode == TransparentShadowMode::kMasked || transparency.mode == TransparentShadowMode::kDithered)
            {
                // REN-39 : l'opacité animée d'un caster fait changer l'empreinte, donc invalide le cache (fixe) ou la recopie (mobile).
                hash.value(static_cast<std::uint32_t>(transparency.mode));
                hash.value(transparency.alpha);
                hash.value(transparency.cutoff);
            }
            continue;
        }
        if (caster.objects.mobility != mobility || caster.objects.objects == nullptr || caster.objects.meshes == nullptr)
        {
            continue;
        }
        CullView view;
        view.frustum = frustum;
        view.eye = eye; // LOD choisi depuis la caméra, comme la vue principale
        caster.objects.objects->prepare(view, scratchBatches_);
        const std::span<const GpuInstance> instances = scratchBatches_.instances();
        for (const InstanceBatch &batch : scratchBatches_.batches())
        {
            const Mesh *mesh = caster.objects.meshes->mesh(batch.key.mesh);
            if (mesh == nullptr || !mesh->valid() || mesh->instanced())
            {
                continue;
            }
            const auto first = static_cast<std::uint32_t>(frameInstances_.size());
            hash.value(caster.id);
            hash.value(batch.key.mesh);
            for (std::uint32_t k = 0; k < batch.instanceCount; ++k)
            {
                const GpuInstance &instance = instances[batch.firstInstance + k];
                frameInstances_.push_back(instance);
                hash.bytes(std::as_bytes(std::span(instance.model)));
            }
            draws.push_back({mesh, true, fit.viewProjection, first, batch.instanceCount, {}});
        }
    }
    return hash.digest();
}

core::Result<> ShadowFeature::prepare(const FrameData &frame)
{
    ++frame_;
    stats_ = {};
    invalidatePending(); // l'image précédente n'a pas validé ses mises à jour (échec avant la fin de l'enregistrement) : à redessiner
    planned_.clear();
    frameInstances_.clear();
    const glm::dvec3 world = glm::dvec3(frame.cameraWorldHigh) + glm::dvec3(frame.cameraWorldLow);
    const glm::vec3 origin = glm::vec3(world - glm::dvec3(frame.cameraRelativeOrigin));
    const glm::vec3 eye = glm::vec3(world);

    std::vector<Request> requests;
    const std::size_t lightCount = lights_ != nullptr ? lights_->lights().size() : 0;
    statuses_.assign(lightCount, ShadowStatus::kNone);
    if (lights_ != nullptr)
    {
        // Mobilité (REN-06) : une lumière Static ou Stationary qui bouge est une violation (comptée, avertie) ; ses vues sont
        // redessinées. Une lumière dont l'index change de type (liste modifiée) repart de zéro.
        const std::vector<Light> &lights = lights_->lights();
        for (std::size_t i = 0; i < lights.size() && i < previousLights_.size(); ++i)
        {
            const Light &now = lights[i];
            const Light &before = previousLights_[i];
            const ShadowBehavior behavior = resolveShadowBehavior(now.mobility, now.shadowMode);
            const bool moved = !sameShape(now, before);
            const bool recolored = !sameColor(now, before);
            if (now.type == before.type && ((moved && !behavior.mayMove) || (recolored && !behavior.mayRecolor)))
            {
                ++mobilityViolations_;
                LOG_WARNING("ShadowFeature: light ", i, " is ", now.mobility == LightMobility::kStatic ? "Static" : "Stationary",
                            " but changed; its shadows are redrawn (use LightMobility::kDynamic for lights that move)");
            }
            if (now.type != before.type)
            {
                // Autre type de lumière au même index (setLight) : la tuile publiée appartient à l'ancien type, même si la mise à jour
                // est reportée (amortissement) ; la vue est redessinée tout de suite.
                for (auto &[key, state] : views_)
                {
                    if (key.light == i)
                    {
                        state.rendered = false;
                        state.cacheValid = false;
                    }
                }
            }
            else if (moved)
            {
                for (auto &[key, state] : views_)
                {
                    if (key.light == i)
                    {
                        state.cacheValid = false; // le contenu suit la nouvelle matrice (comparée plus bas)
                    }
                }
            }
        }
        previousLights_ = lights;
        collectRequests(frame, requests);
    }
    allocateTiles(requests);

    // Vues à mettre à jour (et dues) : jamais dessinées d'abord, puis par priorité de la lumière, puis la plus ancienne (pas de
    // famine sous un budget serré) ; le budget coupe le reste, reporté à l'image suivante.
    struct Candidate
    {
        const Request *request = nullptr;
        ShadowViewFit fit; // à la résolution de la tuile réellement allouée
        Action action = Action::kNone;
        std::vector<CasterDraw> staticDraws;
        std::vector<CasterDraw> movableDraws;
        std::uint64_t staticHash = 0;
        std::uint64_t movableHash = 0;
    };
    std::vector<Candidate> candidates;
    for (const Request &r : requests)
    {
        ViewState &state = views_[r.key];
        ++stats_.views;
        if (!state.tile)
        {
            continue;
        }
        Candidate c;
        c.request = &r;
        // Vue recalculée à la taille de la tuile quand elle a été réduite faute de place (texels, biais et ancrage réels).
        c.fit = state.tile->size == r.size ? r.fit : fitFor(r, state.tile->size);
        const std::size_t mark = frameInstances_.size();
        ShadowHash staticHash;
        staticHash.value(c.fit.viewProjection);
        staticHash.value(*state.tile);
        staticHash.value(gatherCasters(c.fit, eye, CasterMobility::kStatic, c.staticDraws));
        c.staticHash = staticHash.digest();
        c.movableHash = gatherCasters(c.fit, eye, CasterMobility::kMovable, c.movableDraws);
        const bool matrixChanged = !state.rendered || c.fit.viewProjection != state.renderedFit.viewProjection;
        if (!r.behavior.cacheStatic)
        {
            c.action = Action::kFull; // Dynamic : redessinée à chaque mise à jour
        }
        else if (!state.cacheValid || matrixChanged || c.staticHash != state.staticHash)
        {
            c.action = Action::kStaticCopy;
        }
        else if (!state.rendered || c.movableHash != state.movableHash)
        {
            c.action = Action::kCopy;
        }
        if (c.action == Action::kNone)
        {
            ++stats_.cacheHits;
            frameInstances_.resize(mark);
            continue;
        }
        if (state.rendered && frame_ - state.lastUpdate < r.interval)
        {
            ++stats_.deferred; // amortissement : la vue garde son contenu et sa matrice précédents
            frameInstances_.resize(mark);
            continue;
        }
        candidates.push_back(std::move(c));
    }
    const auto serviceKey = [&](const Candidate &c)
    {
        const ViewState &state = views_[c.request->key];
        return ShadowServiceKey{state.rendered, c.request->priority, frame_ - state.lastUpdate};
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Candidate &a, const Candidate &b) { return shadowServiceBefore(serviceKey(a), serviceKey(b)); });
    std::uint32_t budget = settings_.maxViewUpdatesPerFrame;
    for (Candidate &c : candidates)
    {
        const Request &r = *c.request;
        ViewState &state = views_[r.key];
        if (budget == 0)
        {
            ++stats_.deferred; // budget épuisé (ses instances restent dans le tampon de l'image, inutilisées)
            continue;
        }
        --budget;
        PlannedView planned;
        planned.key = r.key;
        planned.tile = *state.tile;
        planned.action = c.action;
        if (c.action == Action::kFull)
        {
            planned.staticDraws = std::move(c.staticDraws);
            planned.staticDraws.insert(planned.staticDraws.end(), c.movableDraws.begin(), c.movableDraws.end());
            ++stats_.fullRedraws;
        }
        else
        {
            if (c.action == Action::kStaticCopy)
            {
                planned.staticDraws = std::move(c.staticDraws);
                ++stats_.staticRedraws;
            }
            if (!c.movableDraws.empty())
            {
                ++stats_.movableRedraws;
            }
            planned.movableDraws = std::move(c.movableDraws);
            ++stats_.cacheCopies;
        }
        ++stats_.viewsUpdated;
        planned_.push_back(std::move(planned));
        // Provisoire : validé à la fin de l'enregistrement de la passe de l'atlas (recordAtlas), invalidé sinon (invalidatePending).
        pending_.push_back(r.key);
        state.rendered = true;
        state.cacheValid = r.behavior.cacheStatic;
        state.renderedFit = c.fit;
        state.staticHash = c.staticHash;
        state.movableHash = c.movableHash;
        state.lastUpdate = frame_;
        state.spot = r.spot;
        state.params = paramsFor(r.key.light);
    }
    // Seules les instances des dessins retenus sont envoyées (budget, amortissement, instances fixes des vues recopiées exclus).
    std::vector<GpuInstance> compact;
    for (PlannedView &view : planned_)
    {
        for (std::vector<CasterDraw> *draws : {&view.staticDraws, &view.movableDraws})
        {
            for (CasterDraw &d : *draws)
            {
                if (!d.instanced)
                {
                    continue;
                }
                const auto first = static_cast<std::uint32_t>(compact.size());
                compact.insert(compact.end(), frameInstances_.begin() + d.firstInstance, frameInstances_.begin() + d.firstInstance + d.instanceCount);
                d.firstInstance = first;
            }
        }
    }
    frameInstances_ = std::move(compact);
    stats_.uploadedInstances = static_cast<std::uint32_t>(frameInstances_.size());
    stats_.mobilityViolations = mobilityViolations_;
    writeGpuData(origin);
    return uploadFrame();
}

void ShadowFeature::writeGpuData(const glm::vec3 &origin)
{
    ShadowGpuData g{};
    const glm::mat4 toWorld = glm::translate(glm::mat4(1.0f), origin); // repère des shaders -> monde
    g.directional = {-1.0f, 0.0f, 0.0f, 0.0f};
    g.filter = {static_cast<float>(settings_.pcfRadius), settings_.constantBias, settings_.slopeBias, settings_.normalOffset};
    g.atlas = {1.0f / static_cast<float>(settings_.atlasSize), static_cast<float>(settings_.atlasSize), 0.0f, 0.0f};
    std::uint32_t spots = 0;
    for (const auto &[key, state] : views_)
    {
        if (!state.tile || !state.rendered)
        {
            continue;
        }
        const glm::mat4 toAtlas = atlasTileMatrix(*state.tile, settings_.atlasSize);
        const glm::vec4 rect = tileRect(*state.tile, settings_.atlasSize);
        if (!state.spot && key.cascade < kMaxShadowCascades)
        {
            g.directional.x = static_cast<float>(key.light);
            g.directional.y = static_cast<float>(settings_.cascadeCount);
            g.directional.z = settings_.maxDistance * (1.0f - std::clamp(settings_.fadeFraction, 0.0f, 1.0f));
            g.directional.w = settings_.maxDistance;
            g.cascadeMatrix[key.cascade] = toAtlas * state.renderedFit.viewProjection * toWorld;
            g.cascadeRect[key.cascade] = rect;
            g.cascadeParams[key.cascade] = {state.renderedFit.texelWorld, 1.0f / std::max(state.renderedFit.depthRange, 1e-6f), 1.0f, 0.0f};
        }
        else if (state.spot && spots < kMaxSpotShadows)
        {
            const ShadowViewFit &fit = state.renderedFit;
            const glm::mat4 inverseView = glm::inverse(fit.view);
            const glm::vec3 position = glm::vec3(inverseView[3]);
            const glm::vec3 direction = -glm::normalize(glm::vec3(inverseView[2]));
            g.spotMatrix[spots] = toAtlas * fit.viewProjection * toWorld;
            g.spotRect[spots] = rect;
            g.spotParams[spots] = {static_cast<float>(key.light), fit.nearPlane, fit.farPlane, fit.texelWorld}; // plans de la vue réellement dessinée
            g.spotPosition[spots] = glm::vec4(position - origin, state.params.fadeStart);
            g.spotDirection[spots] = glm::vec4(direction, state.params.fadeEnd);
            ++spots;
        }
    }
    g.atlas.z = static_cast<float>(spots);
    gpu_ = g;
}

core::Result<> ShadowFeature::uploadFrame()
{
    if (auto r = dataBuffer_.update(0, std::as_bytes(std::span(&gpu_, 1))); !r)
    {
        return r;
    }
    if (frameInstances_.empty())
    {
        return {};
    }
    const std::uint64_t bytes = frameInstances_.size() * sizeof(GpuInstance);
    if (!instanceBuffer_.valid() || instanceBuffer_.size() < bytes)
    {
        const std::uint64_t size = std::bit_ceil(std::max<std::uint64_t>(bytes, 64 * sizeof(GpuInstance)));
        if (instanceBuffer_.valid())
        {
            if (auto r = instanceBuffer_.resize(size, false); !r)
            {
                return r;
            }
        }
        else
        {
            rhi::BufferDesc desc;
            desc.size = size;
            desc.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
            desc.debugName = "shadow.instances";
            auto buffer = GpuBuffer::create(device_, desc);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            instanceBuffer_ = std::move(*buffer);
        }
    }
    return instanceBuffer_.update(0, std::as_bytes(std::span(frameInstances_)));
}

// ── Passes (GPU) ───────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<> ShadowFeature::declarePasses(RenderGraphBuilder &builder)
{
    if (atlas_.isNull())
    {
        return {};
    }
    RenderGraph &graph = builder.graph();
    const RgTextureDesc desc{.format = kShadowFormat, .size = {settings_.atlasSize, settings_.atlasSize}, .usage = rhi::TextureUsage::kRenderTarget | rhi::TextureUsage::kSampled};
    const RgTexture atlas = graph.importTexture("shadow.atlas", atlas_, desc);
    const bool staticWork = cacheFresh_ || std::any_of(planned_.begin(), planned_.end(), [](const PlannedView &p) { return p.action == Action::kStaticCopy; });
    const bool copies = std::any_of(planned_.begin(), planned_.end(), [](const PlannedView &p) { return p.action == Action::kStaticCopy || p.action == Action::kCopy; });
    RgTexture cache;
    if (staticWork || copies)
    {
        cache = graph.importTexture("shadow.cache", cache_, desc);
    }
    if (staticWork)
    {
        graph.addPass("shadow.cache", PassType::kGeneric, [this](RenderPassContext &ctx) { return recordStatic(ctx); }).write(cache);
    }
    if (atlasFresh_ || !planned_.empty())
    {
        auto pass = graph.addPass("shadow.atlas", PassType::kGeneric, [this](RenderPassContext &ctx) { return recordAtlas(ctx); });
        if (cache.valid())
        {
            pass.read(cache);
        }
        pass.write(atlas);
    }
    builder.addSceneInput(atlas);
    return {};
}

core::Result<rhi::PipelineHandle> ShadowFeature::casterPipeline(const Mesh &mesh, bool instanced, bool alphaTest)
{
    GraphicsState state;
    state.vertexBuffers.clear();
    for (const rhi::VertexBufferLayout &layout : mesh.vertexBuffers())
    {
        if (layout.stepRate == rhi::VertexStepRate::kVertex)
        {
            state.vertexBuffers.push_back(layout);
        }
    }
    for (const rhi::VertexAttribute &attribute : mesh.vertexAttributes())
    {
        if (attribute.location == 0)
        {
            state.vertexAttributes.push_back(attribute); // la position seule : les autres attributs ne sont pas lus
        }
    }
    if (instanced)
    {
        state.vertexBuffers.push_back({.binding = 1, .stride = sizeof(GpuInstance), .stepRate = rhi::VertexStepRate::kInstance});
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            state.vertexAttributes.push_back({.location = kInstanceFirstLocation + i, .binding = 1, .format = rhi::VertexFormat::kFloat4, .offset = i * 16});
        }
    }
    state.raster.cull = rhi::CullMode::kNone; // casters fines et faces des deux côtés
    state.depth = {true, true, rhi::CompareOp::kLess};
    state.depthFormat = kShadowFormat;
    state.colorTargets.clear();
    state.debugName = instanced ? "shadow.caster.instanced" : (alphaTest ? "shadow.caster.alpha" : "shadow.caster");
    return pipelines_.graphics(instanced ? *instancedCasterProgram_ : (alphaTest ? *alphaCasterProgram_ : *casterProgram_), state);
}

core::Result<> ShadowFeature::drawCasters(rhi::CommandEncoder &e, const std::vector<CasterDraw> &draws)
{
    rhi::PipelineHandle current;
    for (const CasterDraw &d : draws)
    {
        const bool alphaTest = !d.instanced && (d.transparency.mode == TransparentShadowMode::kMasked || d.transparency.mode == TransparentShadowMode::kDithered);
        auto pipeline = casterPipeline(*d.mesh, d.instanced, alphaTest);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        if (*pipeline != current)
        {
            e.setPipeline(*pipeline);
            current = *pipeline;
        }
        if (alphaTest)
        {
            // Bloc du shader : clip × modèle puis (alpha, seuil, mode, 0) ; mode comme TransparentShadowMode (2 masqué, 3 tramé).
            struct AlphaPush
            {
                glm::mat4 matrix;
                glm::vec4 params;
            } push{d.matrix, {d.transparency.alpha, d.transparency.cutoff, static_cast<float>(static_cast<int>(d.transparency.mode)), 0.0f}};
            e.setPushConstants(std::as_bytes(std::span(&push, 1)));
        }
        else
        {
            e.setPushConstants(std::as_bytes(std::span(&d.matrix, 1)));
        }
        e.setVertexBuffer(0, d.mesh->vertexBuffer());
        if (d.instanced)
        {
            e.setVertexBuffer(1, instanceBuffer_.handle());
        }
        e.setIndexBuffer(d.mesh->indexBuffer(), rhi::IndexType::kUint32);
        e.drawIndexed(d.mesh->indexCount(), d.instanceCount, 0, 0, d.instanced ? d.firstInstance : 0);
        ++stats_.drawCalls;
        stats_.casterInstances += d.instanceCount;
    }
    return {};
}

namespace
{
core::Result<rhi::PipelineHandle> tilePipeline(PipelineCache &pipelines, const ShaderProgram &program, const char *name)
{
    GraphicsState state;
    state.vertexBuffers.clear();
    state.raster.cull = rhi::CullMode::kNone;
    state.depth = {true, true, rhi::CompareOp::kAlways};
    state.depthFormat = kShadowFormat;
    state.colorTargets.clear();
    state.debugName = name;
    return pipelines.graphics(program, state);
}

void setTile(rhi::CommandEncoder &e, const AtlasTile &tile)
{
    e.setViewport({.x = static_cast<float>(tile.x), .y = static_cast<float>(tile.y), .width = static_cast<float>(tile.size), .height = static_cast<float>(tile.size)});
    e.setScissor({.x = static_cast<std::int32_t>(tile.x), .y = static_cast<std::int32_t>(tile.y), .width = tile.size, .height = tile.size});
}
} // namespace

core::Result<> ShadowFeature::recordStatic(RenderPassContext &ctx)
{
    rhi::CommandEncoder &e = ctx.encoder();
    auto clear = tilePipeline(pipelines_, *clearProgram_, "shadow.tile.clear");
    if (!clear)
    {
        return std::unexpected(clear.error());
    }
    rhi::RenderPassDesc pass;
    pass.depth = rhi::DepthAttachment{.texture = cache_, .load = cacheFresh_ ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, .clearDepth = 1.0f};
    pass.debugName = "shadow.cache";
    e.beginRenderPass(pass);
    ++stats_.renderPasses;
    for (const PlannedView &view : planned_)
    {
        if (view.action != Action::kStaticCopy)
        {
            continue;
        }
        setTile(e, view.tile);
        e.setPipeline(*clear);
        e.draw(3);
        if (auto r = drawCasters(e, view.staticDraws); !r)
        {
            e.endRenderPass();
            invalidatePending();
            return r;
        }
    }
    e.endRenderPass();
    cacheFresh_ = false;
    return {};
}

core::Result<> ShadowFeature::recordAtlas(RenderPassContext &ctx)
{
    rhi::CommandEncoder &e = ctx.encoder();
    auto clear = tilePipeline(pipelines_, *clearProgram_, "shadow.tile.clear");
    if (!clear)
    {
        return std::unexpected(clear.error());
    }
    auto copy = tilePipeline(pipelines_, *copyProgram_, "shadow.tile.copy");
    if (!copy)
    {
        return std::unexpected(copy.error());
    }
    rhi::RenderPassDesc pass;
    pass.depth = rhi::DepthAttachment{.texture = atlas_, .load = atlasFresh_ ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, .clearDepth = 1.0f};
    pass.debugName = "shadow.atlas";
    e.beginRenderPass(pass);
    ++stats_.renderPasses;
    for (const PlannedView &view : planned_)
    {
        setTile(e, view.tile);
        if (view.action == Action::kFull)
        {
            e.setPipeline(*clear);
            e.draw(3);
        }
        else
        {
            e.setPipeline(*copy);
            e.setBindGroup(0, copyGroup_);
            e.draw(3);
        }
        const std::vector<CasterDraw> &draws = view.action == Action::kFull ? view.staticDraws : view.movableDraws;
        if (auto r = drawCasters(e, draws); !r)
        {
            e.endRenderPass();
            invalidatePending();
            return r;
        }
    }
    e.endRenderPass();
    atlasFresh_ = false;
    pending_.clear(); // mises à jour enregistrées : le cache et les empreintes de ces vues sont maintenant valides
    return {};
}

} // namespace orosmo::renderer
