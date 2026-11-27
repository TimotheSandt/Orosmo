#include "orosmo/renderer/forward_plus.hpp"

#include "orosmo/renderer/scene_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
// Taille maximale d'un tampon de clusters : 256 Mio (au-delà, les réglages sont refusés plutôt qu'une allocation hasardeuse).
constexpr std::uint64_t kMaxClusterBufferBytes = 256ull * 1024ull * 1024ull;

std::uint64_t gridBytes(const ForwardPlusSettings &s) { return sizeof(GpuClusterHeader) + std::uint64_t{s.clusterCount()} * sizeof(std::uint32_t); }
std::uint64_t listBytes(const ForwardPlusSettings &s)
{
    return std::uint64_t{s.clusterCount()} * std::uint64_t{s.maxLightsPerCluster} * sizeof(std::uint32_t);
}
} // namespace

float lightRange(const Light &light) noexcept
{
    const float peak = std::max({light.color.x, light.color.y, light.color.z}) * light.strength * 1.5f;
    const float k = peak / kLightCutoff - 1.0f;
    if (k <= 0.0f)
    {
        return 0.0f;
    }
    if (light.a > 0.0f)
    {
        const float root = std::sqrt(light.b * light.b + 4.0f * light.a * k);
        // b > 0 : -b + root s'annule quand 4ak << b² (petites portées) ; la forme conjuguée 2k / (b + root) est stable.
        const float range = light.b > 0.0f ? 2.0f * k / (light.b + root) : (-light.b + root) / (2.0f * light.a);
        return std::min(range, kLightMaxRange);
    }
    if (light.b > 0.0f)
    {
        return std::min(k / light.b, kLightMaxRange);
    }
    return kLightMaxRange;
}

float lightAttenuation(float distance, float a, float b, float range) noexcept
{
    if (distance >= range)
    {
        return 0.0f;
    }
    const float x = distance / range;
    const float x2 = x * x;
    const float window = 1.0f - x2 * x2;
    return window * window / (a * distance * distance + b * distance + 1.0f);
}

std::uint32_t clusterSlice(const ForwardPlusSettings &settings, float viewDepth) noexcept
{
    if (viewDepth <= settings.nearDepth || settings.gridZ == 0)
    {
        return 0;
    }
    const float scale = static_cast<float>(settings.gridZ) / std::log(settings.farDepth / settings.nearDepth);
    const float s = std::floor(std::log(viewDepth / settings.nearDepth) * scale);
    return static_cast<std::uint32_t>(std::clamp(s, 0.0f, static_cast<float>(settings.gridZ - 1)));
}

core::Result<> validateSettings(const ForwardPlusSettings &s)
{
    if (s.gridX == 0 || s.gridY == 0 || s.gridZ == 0 || s.maxLightsPerCluster == 0 || s.maxLights == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "ForwardPlusSettings: the grid and the budgets must not be empty");
    }
    if (!(s.nearDepth > 0.0f) || !(s.farDepth > s.nearDepth) || !std::isfinite(s.farDepth))
    {
        return makeError(ErrorCode::kInvalidArgument, "ForwardPlusSettings: expected 0 < nearDepth < farDepth (finite)");
    }
    // Chaque dimension est bornée avant toute multiplication (2³¹ × 2³¹ × 4 repasse par zéro en 64 bits), puis chaque produit partiel.
    constexpr std::uint64_t kMaxClusters = 1u << 20;
    if (s.gridX > kMaxClusters || s.gridY > kMaxClusters || s.gridZ > kMaxClusters || std::uint64_t{s.gridX} * s.gridY > kMaxClusters)
    {
        return makeError(ErrorCode::kInvalidArgument, "ForwardPlusSettings: the cluster grid exceeds " + std::to_string(kMaxClusters) + " clusters");
    }
    const std::uint64_t clusters = std::uint64_t{s.gridX} * s.gridY * s.gridZ; // gridX * gridY <= 2^20 et gridZ <= 2^20 : tient en 64 bits
    if (clusters > kMaxClusters || clusters * s.maxLightsPerCluster * sizeof(std::uint32_t) > kMaxClusterBufferBytes)
    {
        return makeError(ErrorCode::kInvalidArgument, "ForwardPlusSettings: the cluster buffers would exceed 256 MiB (" + std::to_string(clusters) +
                                                          " clusters x " + std::to_string(s.maxLightsPerCluster) + " lights)");
    }
    return {};
}

ClusterStats summarizeClusterCounts(std::span<const std::uint32_t> counts, std::uint32_t maxLightsPerCluster) noexcept
{
    ClusterStats stats;
    stats.clusters = static_cast<std::uint32_t>(counts.size());
    for (const std::uint32_t c : counts)
    {
        stats.emptyClusters += c == 0 ? 1u : 0u;
        stats.overflowingClusters += c > maxLightsPerCluster ? 1u : 0u;
        stats.maxLightsInCluster = std::max(stats.maxLightsInCluster, c);
        stats.lightReferences += std::min(c, maxLightsPerCluster);
    }
    stats.averageLightsPerCluster = counts.empty() ? 0.0 : static_cast<double>(stats.lightReferences) / static_cast<double>(counts.size());
    return stats;
}

bool ForwardPlusLighting::supportedBy(const rhi::Capabilities &caps) noexcept
{
    // Liaisons 1 (lumières), 3 et 4 du groupe 0 : emplacements plats 1, 3 et 4 sur OpenGL (groupe × 8 + liaison).
    return caps.compute && caps.maxStorageBufferBindings > kClusterLightsBinding && caps.maxComputeInvocations >= kClusterWorkgroupSize &&
           caps.maxComputeWorkGroupSize[0] >= kClusterWorkgroupSize;
}

core::Result<std::unique_ptr<ForwardPlusLighting>> ForwardPlusLighting::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                                const std::filesystem::path &shaderDirectory,
                                                                                const ForwardPlusSettings &settings)
{
    if (!supportedBy(device.capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "ForwardPlusLighting: compute shaders and 5 storage buffer bindings are required");
    }
    if (auto r = validateSettings(settings); !r)
    {
        return std::unexpected(r.error());
    }
    auto program = library.loadCompute(shaderDirectory / "light_clusters.comp");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    std::unique_ptr<ForwardPlusLighting> feature(new ForwardPlusLighting(device, pipelines));
    feature->program_ = *program;
    feature->settings_ = settings;
    return feature;
}

core::Result<> ForwardPlusLighting::setSettings(const ForwardPlusSettings &settings)
{
    if (auto r = validateSettings(settings); !r)
    {
        return r;
    }
    settings_ = settings;
    buffersDirty_ = true;
    return {};
}

core::Result<> ForwardPlusLighting::ensureBuffers()
{
    if (!buffersDirty_ && grid_.valid() && lightLists_.valid())
    {
        return {};
    }
    const ForwardPlusSettings s = settings_;
    rhi::BufferDesc gridDesc;
    gridDesc.size = gridBytes(s);
    gridDesc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc;
    gridDesc.debugName = "forwardplus.grid";
    GpuClusterHeader header{};
    header.grid[0] = s.gridX;
    header.grid[1] = s.gridY;
    header.grid[2] = s.gridZ;
    header.grid[3] = s.maxLightsPerCluster;
    header.depth[0] = s.nearDepth;
    header.depth[1] = s.farDepth;
    header.depth[2] = static_cast<float>(s.gridZ) / std::log(s.farDepth / s.nearDepth);
    header.limits[0] = s.maxLights;
    // Les nombres de lumières partent de 0 : une lecture avant la première image ne voit aucun cluster rempli.
    std::vector<std::byte> initial(gridDesc.size, std::byte{0});
    std::memcpy(initial.data(), &header, sizeof header);
    auto grid = GpuBuffer::create(device_, gridDesc, initial);
    if (!grid)
    {
        return std::unexpected(grid.error());
    }
    rhi::BufferDesc listDesc;
    listDesc.size = listBytes(s);
    listDesc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopySrc;
    listDesc.debugName = "forwardplus.lights";
    auto lists = GpuBuffer::create(device_, listDesc);
    if (!lists)
    {
        return std::unexpected(lists.error());
    }
    grid_ = std::move(*grid);
    lightLists_ = std::move(*lists);
    bufferSettings_ = s; // les lectures décrivent ces tampons, pas les réglages en attente
    buffersDirty_ = false;
    return {};
}

core::Result<> ForwardPlusLighting::prepare(const FrameData &frame)
{
    (void)frame;
    return ensureBuffers();
}

std::optional<std::uint64_t> ForwardPlusLighting::passSignature() const
{
    std::uint64_t signature = mixPassSignature(0, grid_.handle());
    signature = mixPassSignature(signature, lightLists_.handle());
    return mixPassSignature(signature, std::uint64_t{settings_.clusterCount()});
}

core::Result<> ForwardPlusLighting::declarePasses(RenderGraphBuilder &builder)
{
    if (auto r = ensureBuffers(); !r)
    {
        return r;
    }
    if (auto r = builder.addFrameBinding({.binding = kClusterGridBinding, .type = rhi::BindingType::kStorageBuffer, .buffer = grid_.handle()}); !r)
    {
        return r;
    }
    if (auto r = builder.addFrameBinding({.binding = kClusterLightsBinding, .type = rhi::BindingType::kStorageBuffer, .buffer = lightLists_.handle()});
        !r)
    {
        return r;
    }
    RenderGraph &graph = builder.graph();
    const RgBuffer grid = graph.importBuffer("forwardplus.grid", grid_.handle(), {.size = grid_.size(), .usage = grid_.desc().usage});
    const RgBuffer lists = graph.importBuffer("forwardplus.lights", lightLists_.handle(), {.size = lightLists_.size(), .usage = lightLists_.desc().usage});
    const std::uint32_t groups = (settings_.clusterCount() + kClusterWorkgroupSize - 1) / kClusterWorkgroupSize;
    graph
        .addPass("forwardplus.clusters", PassType::kGeneric,
                 [this, groups](RenderPassContext &ctx) -> core::Result<>
                 {
                     if (ctx.frame() == nullptr || ctx.frame()->group.isNull())
                     {
                         return makeError(ErrorCode::kInvalidArgument, "forwardplus.clusters: the frame has no group 0 (lights, FrameData)");
                     }
                     auto pipeline = pipelines_.compute(*program_);
                     if (!pipeline)
                     {
                         return std::unexpected(pipeline.error());
                     }
                     rhi::CommandEncoder &e = ctx.encoder();
                     e.bindComputePipeline(*pipeline);
                     e.setBindGroup(0, ctx.frame()->group); // lumières 1, FrameData 2, grille 3, listes 4
                     e.dispatch(groups, 1, 1);
                     return {};
                 })
        .write(grid)
        .write(lists);
    // La passe de scène lit les clusters : la passe compute s'exécute avant elle (et après la pré-passe, qui ne les touche pas).
    builder.addSceneInput(grid);
    builder.addSceneInput(lists);
    return {};
}

core::Result<std::vector<std::uint32_t>> ForwardPlusLighting::readClusterLightCounts() const
{
    if (!grid_.valid())
    {
        return std::vector<std::uint32_t>{};
    }
    const std::uint32_t clusters = bufferSettings_.clusterCount();
    auto bytes = grid_.read(sizeof(GpuClusterHeader), std::uint64_t{clusters} * sizeof(std::uint32_t));
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::vector<std::uint32_t> counts(clusters);
    std::memcpy(counts.data(), bytes->data(), counts.size() * sizeof(std::uint32_t));
    return counts;
}

core::Result<std::vector<std::uint32_t>> ForwardPlusLighting::readClusterLights(std::uint32_t cluster) const
{
    if (cluster >= bufferSettings_.clusterCount())
    {
        return makeError(ErrorCode::kOutOfRange, "readClusterLights: cluster " + std::to_string(cluster) + " >= " + std::to_string(bufferSettings_.clusterCount()));
    }
    if (!grid_.valid() || !lightLists_.valid())
    {
        return std::vector<std::uint32_t>{};
    }
    auto countBytes = grid_.read(sizeof(GpuClusterHeader) + std::uint64_t{cluster} * sizeof(std::uint32_t), sizeof(std::uint32_t));
    if (!countBytes)
    {
        return std::unexpected(countBytes.error());
    }
    std::uint32_t count = 0;
    std::memcpy(&count, countBytes->data(), sizeof count);
    count = std::min(count, bufferSettings_.maxLightsPerCluster);
    std::vector<std::uint32_t> lights(count);
    if (count == 0)
    {
        return lights;
    }
    auto listBytesRead = lightLists_.read(std::uint64_t{cluster} * bufferSettings_.maxLightsPerCluster * sizeof(std::uint32_t), std::uint64_t{count} * sizeof(std::uint32_t));
    if (!listBytesRead)
    {
        return std::unexpected(listBytesRead.error());
    }
    std::memcpy(lights.data(), listBytesRead->data(), lights.size() * sizeof(std::uint32_t));
    return lights;
}

core::Result<ClusterStats> ForwardPlusLighting::readClusterStats() const
{
    auto counts = readClusterLightCounts();
    if (!counts)
    {
        return std::unexpected(counts.error());
    }
    return summarizeClusterCounts(*counts, bufferSettings_.maxLightsPerCluster);
}

core::Result<ForwardPlusLighting *> enableForwardPlus(SceneRenderer &renderer, rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                      const std::filesystem::path &shaderDirectory, const ForwardPlusSettings &settings)
{
    auto feature = ForwardPlusLighting::create(device, library, pipelines, shaderDirectory, settings);
    if (!feature)
    {
        return std::unexpected(feature.error());
    }
    auto added = renderer.addFeature(std::move(*feature));
    if (!added)
    {
        return std::unexpected(added.error());
    }
    renderer.setDepthPrepass(true);
    return static_cast<ForwardPlusLighting *>(*added);
}

void disableForwardPlus(SceneRenderer &renderer)
{
    renderer.removeFeature(ForwardPlusLighting::kName);
    renderer.setDepthPrepass(false);
}

std::vector<Define> ForwardPlusLighting::shaderDefines() const { return forwardPlusDefines(); }

std::vector<Define> forwardPlusDefines(bool clusterDebug)
{
    std::vector<Define> defines{{"OROSMO_FORWARD_PLUS", "1"}};
    if (clusterDebug)
    {
        defines.emplace_back("OROSMO_CLUSTER_DEBUG", "1");
    }
    return defines;
}

} // namespace orosmo::renderer
