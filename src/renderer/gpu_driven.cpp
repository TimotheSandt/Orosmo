#include "orosmo/renderer/gpu_driven.hpp"

#include "orosmo/renderer/scene_renderer.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Bloc `CullBlock` de gpu_cull.comp (std140).
struct GpuCullParams
{
    glm::vec4 planes[6];
    glm::mat4 occlusionViewProj;
    glm::vec4 eye;
    glm::vec4 origin;
    glm::vec4 view;
    glm::vec4 hiz;
    std::uint32_t counts[4];
};
static_assert(sizeof(GpuCullParams) == 240, "CullBlock std140 : 6 vec4, un mat4, 5 vec4");

constexpr std::uint32_t kStatsWords = 16; // en-tête du tampon des compteurs (stats[16] de gpu_cull.comp)
constexpr std::uint32_t kCounterCount = 12;
constexpr std::uint32_t kHizWorkgroup = 8; // local_size de hiz_build.comp
constexpr std::uint32_t kMaxWorkgroups = 65535;
constexpr std::uint32_t kCommandStride = sizeof(rhi::DrawIndexedIndirectCommand);
static_assert(kCommandStride == 20, "DrawIndexedIndirectCommand : 5 mots serrés (std430 de DrawCommand)");

bool sameLayout(const Mesh &a, const Mesh &b)
{
    const auto &la = a.vertexBuffers();
    const auto &lb = b.vertexBuffers();
    const auto &aa = a.vertexAttributes();
    const auto &ab = b.vertexAttributes();
    if (la.size() != lb.size() || aa.size() != ab.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < la.size(); ++i)
    {
        if (la[i].binding != lb[i].binding || la[i].stride != lb[i].stride || la[i].stepRate != lb[i].stepRate)
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < aa.size(); ++i)
    {
        if (aa[i].location != ab[i].location || aa[i].binding != ab[i].binding || aa[i].format != ab[i].format || aa[i].offset != ab[i].offset)
        {
            return false;
        }
    }
    return true;
}

std::uint32_t groupsFor(std::uint32_t size, std::uint32_t local) { return (size + local - 1) / local; }

} // namespace

GpuDrivenView GpuDrivenView::fromMatrices(const glm::mat4 &view, const glm::mat4 &projection, float viewportHeight, bool orthographic)
{
    GpuDrivenView out;
    out.cull = CullView::fromMatrices(view, projection, viewportHeight, orthographic);
    out.viewProjection = projection * view;
    return out;
}

bool GpuDrivenFeature::supportedBy(const rhi::Capabilities &caps) noexcept
{
    return caps.compute && caps.indirectDraw && caps.maxStorageBufferBindings >= kGpuCullStorageBindings &&
           caps.maxComputeWorkGroupSize[0] >= kGpuCullWorkgroupSize && caps.maxComputeInvocations >= kGpuCullWorkgroupSize;
}

core::Result<std::unique_ptr<GpuDrivenFeature>> GpuDrivenFeature::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                         const std::filesystem::path &shaderDirectory, const InstanceRenderer &meshes)
{
    if (!supportedBy(device.capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "GpuDrivenFeature: compute shaders, indirect draws and " + std::to_string(kGpuCullStorageBindings) +
                                                      " storage buffer bindings are required");
    }
    std::unique_ptr<GpuDrivenFeature> feature(new GpuDrivenFeature(device, pipelines, meshes));
    auto cull = library.loadCompute(shaderDirectory / "gpu_cull.comp");
    if (!cull)
    {
        return std::unexpected(cull.error());
    }
    auto cullHiz = library.loadCompute(shaderDirectory / "gpu_cull.comp", {{"OROSMO_HIZ", "1"}});
    if (!cullHiz)
    {
        return std::unexpected(cullHiz.error());
    }
    auto fromDepth = library.loadCompute(shaderDirectory / "hiz_build.comp", {{"OROSMO_HIZ_FROM_DEPTH", "1"}});
    if (!fromDepth)
    {
        return std::unexpected(fromDepth.error());
    }
    auto reduce = library.loadCompute(shaderDirectory / "hiz_build.comp");
    if (!reduce)
    {
        return std::unexpected(reduce.error());
    }
    feature->cullProgram_ = *cull;
    feature->cullHizProgram_ = *cullHiz;
    feature->hizDepthProgram_ = *fromDepth;
    feature->hizReduceProgram_ = *reduce;
    return feature;
}

GpuDrivenFeature::~GpuDrivenFeature()
{
    releaseHiz();
    if (!cullGroup_.isNull())
    {
        device_.destroy(cullGroup_);
    }
    if (!nearest_.isNull())
    {
        device_.destroy(nearest_);
    }
}

void GpuDrivenFeature::setOcclusion(bool enabled) noexcept
{
    occlusion_ = enabled;
    if (!enabled)
    {
        hizValid_ = false;
    }
}

core::Result<> GpuDrivenFeature::ensureBuffer(GpuBuffer &buffer, std::uint64_t size, rhi::BufferUsage usage, const char *name, bool exact)
{
    size = std::max<std::uint64_t>(size, 16);
    if (buffer.valid() && buffer.size() >= size)
    {
        return {};
    }
    rhi::BufferDesc desc;
    desc.size = exact ? size : std::bit_ceil(size);
    desc.usage = usage;
    desc.debugName = name;
    auto created = GpuBuffer::create(device_, desc);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    buffer = std::move(*created);
    return {};
}

core::Result<> GpuDrivenFeature::ensurePools()
{
    const auto meshCount = static_cast<std::uint32_t>(meshes_.meshCount());
    bool rebuild = meshCount != meshCount_ || meshSlots_.size() != meshCount;
    for (std::uint32_t id = 0; id < meshCount && !rebuild; ++id)
    {
        const Mesh *mesh = meshes_.mesh(id);
        rebuild = mesh == nullptr || mesh->vertexBuffer() != meshSlots_[id].vertexBuffer || mesh->indexBuffer() != meshSlots_[id].indexBuffer;
    }
    if (!rebuild)
    {
        return {};
    }
    pools_.clear();
    meshSlots_.assign(meshCount, MeshSlot{});
    pendingCopies_.clear();
    for (std::uint32_t id = 0; id < meshCount; ++id)
    {
        const Mesh *mesh = meshes_.mesh(id);
        if (mesh == nullptr || !mesh->valid() || mesh->vertexBuffers().empty())
        {
            return makeError(ErrorCode::kNotFound, "GpuDrivenFeature: mesh " + std::to_string(id) + " of the InstanceRenderer was released");
        }
        auto pool = std::ranges::find_if(pools_, [&](const Pool &p) { return sameLayout(*p.layout, *mesh); });
        if (pool == pools_.end())
        {
            pools_.push_back({mesh, {}, {}, {}});
            pool = pools_.end() - 1;
        }
        pool->meshes.push_back(id);
        meshSlots_[id].pool = static_cast<std::uint32_t>(pool - pools_.begin());
    }
    std::uint32_t rank = 0;
    for (Pool &pool : pools_)
    {
        const std::uint32_t stride = pool.layout->vertexBuffers()[0].stride;
        std::uint64_t vertexBytes = 0;
        std::uint64_t indexCount = 0;
        for (const std::uint32_t id : pool.meshes)
        {
            const Mesh &mesh = *meshes_.mesh(id);
            MeshSlot &slot = meshSlots_[id];
            slot.rank = rank++;
            slot.vertexOffset = static_cast<std::int32_t>(vertexBytes / stride);
            slot.firstIndex = static_cast<std::uint32_t>(indexCount);
            slot.vertexBuffer = mesh.vertexBuffer();
            slot.indexBuffer = mesh.indexBuffer();
            vertexBytes += mesh.vertexBufferSize();
            indexCount += mesh.indexCount();
        }
        rhi::BufferDesc vd;
        vd.size = std::max<std::uint64_t>(vertexBytes, 16);
        vd.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
        vd.debugName = "gpudriven.pool.vertices";
        auto vertices = GpuBuffer::create(device_, vd);
        if (!vertices)
        {
            return std::unexpected(vertices.error());
        }
        rhi::BufferDesc id;
        id.size = std::max<std::uint64_t>(indexCount * sizeof(std::uint32_t), 16);
        id.usage = rhi::BufferUsage::kIndex | rhi::BufferUsage::kCopyDst;
        id.debugName = "gpudriven.pool.indices";
        auto indices = GpuBuffer::create(device_, id);
        if (!indices)
        {
            return std::unexpected(indices.error());
        }
        pool.vertices = std::move(*vertices);
        pool.indices = std::move(*indices);
        // Copies GPU (les maillages ont l'usage CopySrc), jouées au début de la passe de culling, avant la scène.
        for (const std::uint32_t meshId : pool.meshes)
        {
            const Mesh &mesh = *meshes_.mesh(meshId);
            const MeshSlot &slot = meshSlots_[meshId];
            pendingCopies_.push_back({mesh.vertexBuffer(), pool.vertices.handle(),
                                      {.srcOffset = 0, .dstOffset = std::uint64_t(slot.vertexOffset) * stride, .size = mesh.vertexBufferSize()}});
            pendingCopies_.push_back({mesh.indexBuffer(), pool.indices.handle(),
                                      {.srcOffset = 0, .dstOffset = std::uint64_t{slot.firstIndex} * sizeof(std::uint32_t),
                                       .size = std::uint64_t{mesh.indexCount()} * sizeof(std::uint32_t)}});
        }
    }
    meshCount_ = meshCount;
    tablesDirty_ = true;
    return {};
}

core::Result<> GpuDrivenFeature::ensureTables()
{
    const auto materialCount = static_cast<std::uint32_t>(meshes_.materialCount());
    if (!tablesDirty_ && materialCount == materialCount_ && scene_.keyVersion() == tablesKeyVersion_)
    {
        return {};
    }
    materialCount_ = materialCount;
    commandCount_ = materialCount_ * meshCount_;
    commandKeys_.assign(commandCount_, InstanceKey{});
    commandCapacity_.assign(commandCount_, 0);
    std::vector<std::uint32_t> meshByRank(meshCount_);
    for (std::uint32_t id = 0; id < meshCount_; ++id)
    {
        meshByRank[meshSlots_[id].rank] = id;
    }
    std::vector<rhi::DrawIndexedIndirectCommand> commands(commandCount_);
    std::vector<std::uint32_t> tables(meshCount_ + commandCount_);
    std::uint64_t firstInstance = 0;
    const auto &refs = scene_.keyReferences();
    for (std::uint32_t command = 0; command < commandCount_; ++command)
    {
        const std::uint32_t material = command / meshCount_;
        const std::uint32_t mesh = meshByRank[command % meshCount_];
        const InstanceKey key{mesh, material};
        const auto it = refs.find(key);
        const std::uint32_t capacity = it != refs.end() ? it->second : 0;
        commandKeys_[command] = key;
        commandCapacity_[command] = capacity;
        commands[command] = {meshes_.mesh(mesh)->indexCount(), 0, meshSlots_[mesh].firstIndex, meshSlots_[mesh].vertexOffset,
                             static_cast<std::uint32_t>(firstInstance)};
        tables[meshCount_ + command] = capacity;
        firstInstance += capacity;
    }
    for (std::uint32_t id = 0; id < meshCount_; ++id)
    {
        tables[id] = meshSlots_[id].rank;
    }
    if (firstInstance > 0xFFFFFFFFull / sizeof(GpuInstance))
    {
        return makeError(ErrorCode::kOutOfRange, "GpuDrivenFeature: too many instances for one buffer");
    }
    const std::uint64_t commandBytes = std::uint64_t{std::max<std::uint32_t>(commandCount_, 1)} * kCommandStride;
    if (auto r = ensureBuffer(tables_, std::uint64_t{std::max<std::size_t>(tables.size(), 1)} * sizeof(std::uint32_t),
                              rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst, "gpudriven.tables");
        !r)
    {
        return r;
    }
    if (auto r = ensureBuffer(template_, commandBytes, rhi::BufferUsage::kCopySrc | rhi::BufferUsage::kCopyDst, "gpudriven.template"); !r)
    {
        return r;
    }
    if (auto r = ensureBuffer(args_, commandBytes,
                              rhi::BufferUsage::kIndirect | rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc,
                              "gpudriven.args");
        !r)
    {
        return r;
    }
    if (auto r = ensureBuffer(instances_, std::max<std::uint64_t>(firstInstance, 1) * sizeof(GpuInstance),
                              rhi::BufferUsage::kVertex | rhi::BufferUsage::kStorage, "gpudriven.instances");
        !r)
    {
        return r;
    }
    if (!tables.empty())
    {
        if (auto r = tables_.update(0, std::as_bytes(std::span(tables))); !r)
        {
            return r;
        }
    }
    if (!commands.empty())
    {
        if (auto r = template_.update(0, std::as_bytes(std::span(commands))); !r)
        {
            return r;
        }
    }
    tablesKeyVersion_ = scene_.keyVersion();
    tablesDirty_ = false;
    return {};
}

core::Result<> GpuDrivenFeature::ensureCullGroup()
{
    const std::vector<rhi::BufferHandle> buffers{params_.handle(), scene_.objectBuffer(), scene_.lodBuffer(), tables_.handle(),
                                                 args_.handle(),   instances_.handle(),   stats_.handle()};
    if (!cullGroup_.isNull() && buffers == cullGroupBuffers_)
    {
        return {};
    }
    if (!cullGroup_.isNull())
    {
        device_.destroy(cullGroup_);
        cullGroup_ = {};
    }
    rhi::BindGroupDesc desc;
    desc.debugName = "gpudriven.cull";
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = buffers[0]});
    for (std::uint32_t binding = 1; binding < buffers.size(); ++binding)
    {
        desc.entries.push_back({.binding = binding, .type = rhi::BindingType::kStorageBuffer, .buffer = buffers[binding]});
    }
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    cullGroup_ = *group;
    cullGroupBuffers_ = buffers;
    return {};
}

void GpuDrivenFeature::releaseHiz() noexcept
{
    for (rhi::BindGroupHandle group : hizLevelGroups_)
    {
        device_.destroy(group);
    }
    hizLevelGroups_.clear();
    device_.destroy(hizSampleGroup_);
    hizSampleGroup_ = {};
    device_.destroy(hiz_);
    hiz_ = {};
    hizDepth_ = {};
    hizSize_ = {};
    hizLevels_ = 0;
    hizValid_ = false;
}

core::Result<> GpuDrivenFeature::ensureHiz(rhi::Extent2D size)
{
    if (!hiz_.isNull() && hizSize_.width == size.width && hizSize_.height == size.height)
    {
        return {};
    }
    releaseHiz();
    if (nearest_.isNull())
    {
        rhi::SamplerDesc sd;
        sd.minFilter = rhi::Filter::kNearest;
        sd.magFilter = rhi::Filter::kNearest;
        sd.mipFilter = rhi::Filter::kNearest;
        sd.addressU = rhi::AddressMode::kClampToEdge;
        sd.addressV = rhi::AddressMode::kClampToEdge;
        sd.addressW = rhi::AddressMode::kClampToEdge;
        auto sampler = device_.createSampler(sd);
        if (!sampler)
        {
            return std::unexpected(sampler.error());
        }
        nearest_ = *sampler;
    }
    rhi::TextureDesc td;
    td.format = rhi::Format::kR32Float;
    td.size = {size.width, size.height, 1};
    td.mipLevels = 0; // chaîne complète
    td.usage = rhi::TextureUsage::kStorage | rhi::TextureUsage::kSampled | rhi::TextureUsage::kCopySrc;
    td.debugName = "gpudriven.hiz";
    auto texture = device_.createTexture(td);
    if (!texture)
    {
        return std::unexpected(texture.error());
    }
    hiz_ = *texture;
    auto real = device_.textureDesc(hiz_);
    if (!real)
    {
        return std::unexpected(real.error());
    }
    hizSize_ = size;
    hizLevels_ = real->mipLevels;
    rhi::BindGroupDesc sample;
    sample.debugName = "gpudriven.hiz.sample";
    sample.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = hiz_, .sampler = nearest_});
    auto sampleGroup = device_.createBindGroup(sample);
    if (!sampleGroup)
    {
        return std::unexpected(sampleGroup.error());
    }
    hizSampleGroup_ = *sampleGroup;
    hizLevelGroups_.assign(hizLevels_, rhi::BindGroupHandle{});
    for (std::uint32_t level = 1; level < hizLevels_; ++level)
    {
        rhi::BindGroupDesc reduce;
        reduce.debugName = "gpudriven.hiz.level";
        reduce.entries.push_back({.binding = 0, .type = rhi::BindingType::kStorageImage, .texture = hiz_, .mipLevel = level - 1});
        reduce.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageImage, .texture = hiz_, .mipLevel = level});
        auto group = device_.createBindGroup(reduce);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        hizLevelGroups_[level] = *group;
    }
    return {};
}

core::Result<> GpuDrivenFeature::buildHiz(RenderPassContext &ctx, rhi::TextureHandle depth)
{
    auto depthDesc = device_.textureDesc(depth);
    if (!depthDesc || !rhi::hasFlag(depthDesc->usage, rhi::TextureUsage::kSampled))
    {
        // Profondeur non échantillonnable (SceneRenderer::setDepthAsTextureEnabled(false)) : pas de Hi-Z, pas d'occlusion.
        hizValid_ = false;
        return {};
    }
    if (depth != hizDepth_ || hizLevelGroups_[0].isNull())
    {
        if (!hizLevelGroups_[0].isNull())
        {
            device_.destroy(hizLevelGroups_[0]);
            hizLevelGroups_[0] = {};
        }
        rhi::BindGroupDesc first;
        first.debugName = "gpudriven.hiz.depth";
        first.entries.push_back({.binding = 0, .type = rhi::BindingType::kSampledTexture, .texture = depth, .sampler = nearest_});
        first.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageImage, .texture = hiz_, .mipLevel = 0});
        auto group = device_.createBindGroup(first);
        if (!group)
        {
            return std::unexpected(group.error());
        }
        hizLevelGroups_[0] = *group;
        hizDepth_ = depth;
    }
    auto fromDepth = pipelines_.compute(*hizDepthProgram_);
    if (!fromDepth)
    {
        return std::unexpected(fromDepth.error());
    }
    auto reduce = pipelines_.compute(*hizReduceProgram_);
    if (!reduce)
    {
        return std::unexpected(reduce.error());
    }
    rhi::CommandEncoder &e = ctx.encoder();
    for (std::uint32_t level = 0; level < hizLevels_; ++level)
    {
        const std::uint32_t w = std::max(hizSize_.width >> level, 1u);
        const std::uint32_t h = std::max(hizSize_.height >> level, 1u);
        e.bindComputePipeline(level == 0 ? *fromDepth : *reduce);
        e.setBindGroup(0, hizLevelGroups_[level]);
        e.dispatch(groupsFor(w, kHizWorkgroup), groupsFor(h, kHizWorkgroup), 1);
    }
    // Lue par le culling de l'image suivante, avec la vue-projection de CETTE image.
    hizValid_ = true;
    hizViewProj_ = frameViewProj_;
    return {};
}

core::Result<GpuDrawStats> GpuDrivenFeature::draw(SceneRenderer &scene, const ObjectSet &objects, const GpuDrivenView &view,
                                                  const GpuDrivenOptions &options)
{
    auto info = scene.sceneDrawInfo();
    if (!info)
    {
        return std::unexpected(info.error());
    }
    if (pending_ && lastDrawFrame_ == scene.frameIndex())
    {
        return makeError(ErrorCode::kInvalidArgument, "GpuDrivenFeature::draw: already called for this frame (the command buffers are reused)");
    }
    GpuDrawStats stats;
    auto synced = scene_.sync(device_, objects);
    if (!synced)
    {
        return std::unexpected(synced.error());
    }
    stats.sync = *synced;
    if (auto r = ensurePools(); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = ensureTables(); !r)
    {
        return std::unexpected(r.error());
    }
    const std::uint32_t slots = scene_.slotCount();
    if (groupsFor(slots, kGpuCullWorkgroupSize) > std::min(kMaxWorkgroups, device_.capabilities().maxComputeWorkGroupCount[0]))
    {
        return makeError(ErrorCode::kOutOfRange, "GpuDrivenFeature::draw: too many object slots for one dispatch (" + std::to_string(slots) + ")");
    }
    if (auto r = ensureBuffer(stats_, (std::uint64_t{kStatsWords} + std::max<std::uint32_t>(slots, 1)) * sizeof(std::uint32_t),
                              rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc, "gpudriven.stats");
        !r)
    {
        return std::unexpected(r.error());
    }
    if (!zeros_.valid())
    {
        const std::vector<std::byte> zero(kStatsWords * sizeof(std::uint32_t), std::byte{0});
        rhi::BufferDesc desc;
        desc.size = zero.size();
        desc.usage = rhi::BufferUsage::kCopySrc;
        desc.debugName = "gpudriven.zeros";
        auto created = GpuBuffer::create(device_, desc, zero);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        zeros_ = std::move(*created);
    }
    if (auto r = ensureBuffer(params_, sizeof(GpuCullParams), rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst, "gpudriven.params", true); !r)
    {
        return std::unexpected(r.error());
    }

    cullWithHiz_ = occlusion_ && hizValid_ && !hiz_.isNull();
    GpuCullParams params{};
    const auto planes = view.cull.frustum.planes();
    for (std::size_t i = 0; i < planes.size(); ++i)
    {
        params.planes[i] = planes[i];
    }
    params.occlusionViewProj = hizViewProj_;
    params.eye = glm::vec4(view.cull.eye, static_cast<float>(planes.size()));
    params.origin = glm::vec4(info->cameraOrigin, 0.0f);
    params.view = glm::vec4(view.cull.projectionScale, view.cull.viewportHeight, view.cull.minScreenRadius, view.cull.maxDistance);
    params.hiz = glm::vec4(static_cast<float>(hizSize_.width), static_cast<float>(hizSize_.height), static_cast<float>(hizLevels_), 0.0f);
    params.counts[0] = slots;
    params.counts[1] = meshCount_;
    params.counts[2] = materialCount_;
    params.counts[3] = (view.cull.orthographic ? 1u : 0u) | (options.frustumCulling ? 2u : 0u) | (options.lod ? 4u : 0u);
    if (auto r = params_.update(0, std::as_bytes(std::span(&params, 1))); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = ensureCullGroup(); !r)
    {
        return std::unexpected(r.error());
    }
    frameViewProj_ = view.viewProjection;

    // Un dessin indirect par (matériau, pool) dont une commande au moins peut recevoir des instances.
    struct Draw
    {
        rhi::PipelineHandle pipeline;
        rhi::BindGroupHandle textures;
        std::vector<std::byte> pushConstants;
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::uint32_t firstCommand = 0;
        std::uint32_t count = 0;
    };
    std::vector<Draw> draws;
    for (int pass = 0; pass < (options.wireframe ? 2 : 1); ++pass)
    {
        const bool lines = pass == 1;
        for (std::uint32_t material = 0; material < materialCount_; ++material)
        {
            const InstanceMaterial &mat = *meshes_.material(material);
            const ShaderProgram &program = mat.program ? *mat.program : *meshes_.defaultProgram();
            for (const Pool &pool : pools_)
            {
                const std::uint32_t firstCommand = material * meshCount_ + meshSlots_[pool.meshes.front()].rank;
                const auto count = static_cast<std::uint32_t>(pool.meshes.size());
                std::uint64_t capacity = 0;
                for (std::uint32_t c = firstCommand; c < firstCommand + count; ++c)
                {
                    capacity += commandCapacity_[c];
                }
                if (capacity == 0)
                {
                    continue;
                }
                GraphicsState state = instancedGraphicsState(*pool.layout, mat.cull, *info, lines);
                state.debugName = program.name() + ".gpudriven" + (lines ? ".lines" : "");
                auto pipeline = pipelines_.graphics(program, state);
                if (!pipeline)
                {
                    return std::unexpected(pipeline.error());
                }
                draws.push_back({*pipeline, mat.textures, instancedPushConstants(pipelines_, *pipeline, mat, lines), pool.vertices.handle(),
                                 pool.indices.handle(), firstCommand, count});
            }
        }
    }
    stats.commands = commandCount_;
    stats.indirectCalls = static_cast<std::uint32_t>(draws.size());
    stats.pools = static_cast<std::uint32_t>(pools_.size());
    if (!draws.empty())
    {
        auto recorded = scene.drawCustom(
            [draws = std::move(draws), args = args_.handle(), instances = instances_.handle()](rhi::CommandEncoder &e,
                                                                                               rhi::BindGroupHandle frameGroup) -> core::Result<>
            {
                for (const Draw &d : draws)
                {
                    e.setPipeline(d.pipeline);
                    e.setBindGroup(0, frameGroup);
                    if (!d.textures.isNull())
                    {
                        e.setBindGroup(1, d.textures);
                    }
                    if (!d.pushConstants.empty())
                    {
                        e.setPushConstants(d.pushConstants);
                    }
                    e.setVertexBuffer(0, d.vertices);
                    e.setVertexBuffer(1, instances);
                    e.setIndexBuffer(d.indices, rhi::IndexType::kUint32);
                    e.drawIndexedIndirect(args, std::uint64_t{d.firstCommand} * kCommandStride, d.count, kCommandStride);
                }
                return {};
            });
        if (!recorded)
        {
            return std::unexpected(recorded.error());
        }
    }
    pending_ = true;
    lastDrawFrame_ = scene.frameIndex();
    slotCount_ = slots;
    return stats;
}

core::Result<> GpuDrivenFeature::declarePasses(RenderGraphBuilder &builder)
{
    if (!pending_)
    {
        return {};
    }
    pending_ = false;
    RenderGraph &graph = builder.graph();
    const RenderGraphBuilder::Resources &known = builder.resources();
    const bool buildHizThisFrame = occlusion_ && known.sceneDepth.valid();
    if (buildHizThisFrame && (hizSize_.width != known.renderSize.width || hizSize_.height != known.renderSize.height))
    {
        // Nouvelle taille : la Hi-Z est recréée (l'ancienne n'est plus liée par le culling de cette image).
        cullWithHiz_ = false;
        if (auto r = ensureHiz(known.renderSize); !r)
        {
            return r;
        }
    }
    const bool useHiz = cullWithHiz_ && !hizSampleGroup_.isNull();

    const RgBuffer args = graph.importBuffer("gpudriven.args", args_.handle(), {.size = args_.size(), .usage = args_.desc().usage});
    const RgBuffer instances = graph.importBuffer("gpudriven.instances", instances_.handle(), {.size = instances_.size(), .usage = instances_.desc().usage});
    const RgBuffer stats = graph.importBuffer("gpudriven.stats", stats_.handle(), {.size = stats_.size(), .usage = stats_.desc().usage});
    graph
        .addPass("gpudriven.cull", PassType::kGeneric,
                 [this, useHiz, slots = slotCount_, commands = commandCount_,
                  hizGroup = hizSampleGroup_](RenderPassContext &ctx) -> core::Result<>
                 {
                     rhi::CommandEncoder &e = ctx.encoder();
                     // Copies des pools gardées jusqu'à leur enregistrement (une image avortée avant cette passe les rejoue).
                     for (const PendingCopy &copy : pendingCopies_)
                     {
                         if (copy.region.size > 0)
                         {
                             e.copyBuffer(copy.source, copy.destination, copy.region);
                         }
                     }
                     pendingCopies_.clear();
                     e.copyBuffer(zeros_.handle(), stats_.handle(), {.srcOffset = 0, .dstOffset = 0, .size = kStatsWords * sizeof(std::uint32_t)});
                     if (commands > 0)
                     {
                         e.copyBuffer(template_.handle(), args_.handle(), {.srcOffset = 0, .dstOffset = 0, .size = std::uint64_t{commands} * kCommandStride});
                     }
                     // Sans commande (aucun maillage ou matériau), le culling tourne quand même : il écrit les drapeaux de visibilité
                     // lus par readVisibility ; emit vérifie la clé avant tout accès aux tables (instance comptée invalide).
                     if (slots == 0)
                     {
                         return {};
                     }
                     auto pipeline = pipelines_.compute(useHiz ? *cullHizProgram_ : *cullProgram_);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     e.bindComputePipeline(*pipeline);
                     e.setBindGroup(0, cullGroup_);
                     if (useHiz)
                     {
                         e.setBindGroup(1, hizGroup);
                     }
                     e.dispatch(groupsFor(slots, kGpuCullWorkgroupSize), 1, 1);
                     return {};
                 })
        .write(args)
        .write(instances)
        .write(stats);
    // Les dessins indirects de la passe de scène lisent les commandes et les instances : le culling s'exécute avant elle.
    builder.addSceneInput(args);
    builder.addSceneInput(instances);

    if (buildHizThisFrame)
    {
        if (auto r = ensureHiz(known.renderSize); !r)
        {
            return r;
        }
        const RgTexture hiz = graph.importTexture("gpudriven.hiz", hiz_,
                                                  {.format = rhi::Format::kR32Float,
                                                   .size = hizSize_,
                                                   .mipLevels = hizLevels_,
                                                   .usage = rhi::TextureUsage::kStorage | rhi::TextureUsage::kSampled});
        const RgTexture depth = known.sceneDepth;
        // Lit la profondeur laissée par la passe de scène (déclarée avant les plugins) : la pyramide sert au culling de l'image
        // suivante (le culling de cette image, avant la scène, ne déclare pas la Hi-Z : il lit celle de l'image précédente).
        graph.addPass("gpudriven.hiz", PassType::kGeneric, [this, depth](RenderPassContext &ctx) { return buildHiz(ctx, ctx.texture(depth)); })
            .read(depth)
            .write(hiz);
    }
    return {};
}

core::Result<GpuCullStats> GpuDrivenFeature::readStats() const
{
    GpuCullStats out;
    if (!stats_.valid())
    {
        return out;
    }
    auto bytes = stats_.read(0, kCounterCount * sizeof(std::uint32_t));
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::uint32_t c[kCounterCount]{};
    std::memcpy(c, bytes->data(), sizeof c);
    out.objects = c[0];
    out.frustumCulled = c[1];
    out.distanceCulled = c[2];
    out.smallCulled = c[3];
    out.frustumVisible = c[4];
    out.occlusionCulled = c[5];
    out.visible = c[6];
    out.lodCulled = c[7];
    out.crossFades = c[8];
    out.instances = c[9];
    out.invalid = c[10];
    out.overflow = c[11];
    auto counts = readCommandCounts();
    if (!counts)
    {
        return std::unexpected(counts.error());
    }
    out.drawsWithInstances = static_cast<std::uint32_t>(counts->size());
    return out;
}

core::Result<std::vector<std::uint32_t>> GpuDrivenFeature::readVisibility() const
{
    std::vector<std::uint32_t> flags(slotCount_);
    if (!stats_.valid() || slotCount_ == 0)
    {
        return flags;
    }
    auto bytes = stats_.read(kStatsWords * sizeof(std::uint32_t), std::uint64_t{slotCount_} * sizeof(std::uint32_t));
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::memcpy(flags.data(), bytes->data(), flags.size() * sizeof(std::uint32_t));
    return flags;
}

core::Result<std::vector<GpuCommandCount>> GpuDrivenFeature::readCommandCounts() const
{
    std::vector<GpuCommandCount> out;
    if (!args_.valid() || commandCount_ == 0)
    {
        return out;
    }
    auto bytes = args_.read(0, std::uint64_t{commandCount_} * kCommandStride);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::vector<rhi::DrawIndexedIndirectCommand> commands(commandCount_);
    std::memcpy(commands.data(), bytes->data(), commands.size() * kCommandStride);
    for (std::uint32_t c = 0; c < commandCount_; ++c)
    {
        if (commands[c].instanceCount > 0)
        {
            out.push_back({commandKeys_[c], commands[c].instanceCount});
        }
    }
    return out;
}

core::Result<std::vector<float>> GpuDrivenFeature::readHiz(std::uint32_t level) const
{
    if (hiz_.isNull() || level >= hizLevels_)
    {
        return std::vector<float>{};
    }
    auto bytes = device_.readTexture(hiz_, {.mipLevel = level});
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::vector<float> texels(bytes->size() / sizeof(float));
    std::memcpy(texels.data(), bytes->data(), texels.size() * sizeof(float));
    return texels;
}

core::Result<GpuDrivenFeature *> enableGpuDriven(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                 const std::filesystem::path &shaderDirectory, const InstanceRenderer &meshes)
{
    auto feature = GpuDrivenFeature::create(device, library, pipelines, shaderDirectory, meshes);
    if (!feature)
    {
        return std::unexpected(feature.error());
    }
    auto added = renderer.addFeature(std::move(*feature));
    if (!added)
    {
        return std::unexpected(added.error());
    }
    renderer.setDepthAsTextureEnabled(true); // la Hi-Z échantillonne la profondeur de la scène
    return static_cast<GpuDrivenFeature *>(*added);
}

void disableGpuDriven(SceneRenderer &renderer) { renderer.removeFeature(GpuDrivenFeature::kName); }

} // namespace orosmo::renderer
