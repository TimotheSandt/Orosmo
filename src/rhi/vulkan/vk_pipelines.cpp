// Shaders (réflexion SPIR-V), dispositions de descripteurs, pipelines, groupes de liaison, cache de pipelines (RHI-06).
// Voir README.md, « Shaders, pipelines et groupes de liaison » et « Descripteurs ».
#include "vk_device.hpp"
#include "vk_translate.hpp"
#include "vk_util.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/core/system.hpp>

#include <spirv_cross/spirv_cross.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

namespace
{

constexpr std::uint32_t kSpirvMagic = 0x07230203u;
constexpr std::uint32_t kSetsPerPool = 256;

// ── Réflexion SPIR-V ─────────────────────────────────────────────────────────

spv::ExecutionModel executionModelOf(ShaderStage stage) noexcept
{
    switch (stage)
    {
        case ShaderStage::kVertex: return spv::ExecutionModelVertex;
        case ShaderStage::kFragment: return spv::ExecutionModelFragment;
        case ShaderStage::kCompute: return spv::ExecutionModelGLCompute;
    }
    return spv::ExecutionModelVertex;
}

// Lit l'interface d'un module SPIR-V : descripteurs (groupe, liaison, type, nombre), push constants, taille de groupe (compute),
// entrées du sommet. Mêmes règles que la réflexion du compilateur de shaders (shader_compiler.cpp) : trois groupes de huit
// liaisons, pas de textures et d'échantillonneurs séparés, pas de tableau de ressources sans taille.
Result<ShaderInterface> reflectSpirv(const std::vector<std::uint32_t> &words, ShaderStage stage, const std::string &entryPoint, const std::string &name)
{
    ShaderInterface out;
    try
    {
        spirv_cross::Compiler compiler(words);
        bool found = false;
        for (const spirv_cross::EntryPoint &e : compiler.get_entry_points_and_stages())
        {
            if (e.name == entryPoint && e.execution_model == executionModelOf(stage))
            {
                compiler.set_entry_point(e.name, e.execution_model);
                found = true;
            }
        }
        if (!found)
            return makeError(ErrorCode::kInvalidArgument, "createShader (" + name + "): no entry point '" + entryPoint + "' for this stage");

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        std::string problem;
        auto add = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list, BindingType type)
        {
            for (const spirv_cross::Resource &r : list)
            {
                ReflectedDescriptor d;
                d.set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
                d.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
                d.type = type;
                const spirv_cross::SPIRType &t = compiler.get_type(r.type_id);
                std::uint64_t count = 1;
                for (std::size_t i = 0; i < t.array.size(); ++i)
                {
                    if (t.array[i] == 0 || !t.array_size_literal[i])
                    {
                        problem = "'" + r.name + "' is an unsized or specialization-sized array of resources";
                        count = 1;
                        break;
                    }
                    count *= t.array[i];
                }
                if (d.set >= kGroupCount || d.binding >= kBindingsPerGroup || d.binding + count > kBindingsPerGroup)
                {
                    problem = "'" + r.name + "' uses set " + std::to_string(d.set) + ", binding " + std::to_string(d.binding) + ": the RHI has " +
                              std::to_string(kGroupCount) + " groups of " + std::to_string(kBindingsPerGroup) + " bindings";
                    continue;
                }
                d.count = static_cast<std::uint32_t>(count);
                out.descriptors.push_back(d);
            }
        };
        add(resources.uniform_buffers, BindingType::kUniformBuffer);
        add(resources.storage_buffers, BindingType::kStorageBuffer);
        add(resources.sampled_images, BindingType::kSampledTexture);
        add(resources.storage_images, BindingType::kStorageImage);
        if (!resources.separate_images.empty() || !resources.separate_samplers.empty())
            problem = "separate textures and samplers are not supported: declare a combined 'sampler2D' (RHI binding model)";
        if (!problem.empty())
            return makeError(ErrorCode::kInvalidArgument, "createShader (" + name + "): " + problem);
        std::ranges::sort(out.descriptors, [](const ReflectedDescriptor &a, const ReflectedDescriptor &b)
                          { return a.set != b.set ? a.set < b.set : a.binding < b.binding; });
        for (std::size_t i = 1; i < out.descriptors.size(); ++i)
        {
            const ReflectedDescriptor &a = out.descriptors[i - 1];
            const ReflectedDescriptor &b = out.descriptors[i];
            if (a.set == b.set && b.binding < a.binding + a.count)
                return makeError(ErrorCode::kInvalidArgument, "createShader (" + name + "): two resources overlap in set " + std::to_string(b.set) +
                                                                  ", binding " + std::to_string(b.binding));
        }
        if (!resources.push_constant_buffers.empty())
        {
            const auto &pc = resources.push_constant_buffers.front();
            out.pushConstantSize = static_cast<std::uint32_t>(compiler.get_declared_struct_size(compiler.get_type(pc.base_type_id)));
        }
        if (stage == ShaderStage::kCompute)
        {
            for (std::uint32_t i = 0; i < 3; ++i)
                out.localSize[i] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
        }
        auto variables = [&](const spirv_cross::SmallVector<spirv_cross::Resource> &list)
        {
            std::vector<ShaderInterface::StageVariable> vars;
            for (const spirv_cross::Resource &r : list)
            {
                if (!compiler.has_decoration(r.id, spv::DecorationLocation))
                    continue;
                const spirv_cross::SPIRType &t = compiler.get_type(r.type_id);
                ShaderInterface::StageVariable v;
                v.location = compiler.get_decoration(r.id, spv::DecorationLocation);
                v.baseType = static_cast<std::uint32_t>(t.basetype);
                v.vecSize = t.vecsize;
                v.columns = t.columns;
                v.arraySize = t.array.empty() ? 1u : t.array[0];
                v.name = r.name;
                vars.push_back(std::move(v));
            }
            return vars;
        };
        if (stage == ShaderStage::kVertex)
        {
            for (const spirv_cross::Resource &r : resources.stage_inputs)
            {
                if (compiler.has_decoration(r.id, spv::DecorationLocation))
                    out.inputLocations.push_back(compiler.get_decoration(r.id, spv::DecorationLocation));
            }
            out.stageOutputs = variables(resources.stage_outputs);
        }
        if (stage == ShaderStage::kFragment)
            out.stageInputs = variables(resources.stage_inputs);
    }
    catch (const spirv_cross::CompilerError &e)
    {
        return makeError(ErrorCode::kParse, "createShader (" + name + "): SPIR-V reflection failed: " + e.what());
    }
    return out;
}

// Fusionne les descripteurs de plusieurs étages en liaisons par groupe (étages réunis). Deux étages qui déclarent la même liaison
// avec un autre type ou un autre nombre : kInvalidArgument.
Result<std::array<std::vector<DescriptorBinding>, kGroupCount>> mergeInterfaces(std::span<const ShaderRes *const> shaders, const std::string &name)
{
    std::array<std::vector<DescriptorBinding>, kGroupCount> groups;
    for (const ShaderRes *shader : shaders)
    {
        for (const ReflectedDescriptor &d : shader->interface.descriptors)
        {
            auto &list = groups[d.set];
            auto it = std::find_if(list.begin(), list.end(), [&](const DescriptorBinding &b) { return b.binding == d.binding; });
            if (it == list.end())
            {
                list.push_back({d.binding, d.type, d.count, static_cast<VkShaderStageFlags>(toVk(shader->stage))});
                continue;
            }
            if (it->type != d.type || it->count != d.count)
                return makeError(ErrorCode::kInvalidArgument, "pipeline (" + name + "): set " + std::to_string(d.set) + ", binding " + std::to_string(d.binding) +
                                                                  " has different types in two stages");
            it->stages |= toVk(shader->stage);
        }
    }
    for (auto &list : groups)
    {
        std::ranges::sort(list, [](const DescriptorBinding &a, const DescriptorBinding &b) { return a.binding < b.binding; });
        // Une liaison tableau de n éléments occupe les entrées b .. b+n-1 : deux déclarations ne doivent pas se recouvrir.
        for (std::size_t i = 1; i < list.size(); ++i)
        {
            if (static_cast<std::uint64_t>(list[i - 1].binding) + list[i - 1].count > list[i].binding)
                return makeError(ErrorCode::kInvalidArgument, "pipeline (" + name + "): binding " + std::to_string(list[i].binding) +
                                                                  " overlaps the array that starts at binding " + std::to_string(list[i - 1].binding));
        }
    }
    return groups;
}

// ── Cache de pipelines sur disque (RHI-06) ──────────────────────────────────────

std::filesystem::path pipelineCacheDirectory()
{
    if (const char *dir = std::getenv("ENGINE_VULKAN_PIPELINE_CACHE_DIR"); dir != nullptr && *dir != '\0')
        return std::filesystem::path(dir);
    const std::filesystem::path base = core::system::userDataDirectory();
    if (base.empty())
        return {};
    return base / "Orosmo" / "pipeline_cache";
}

// En-tête d'un cache de pipelines (VkPipelineCacheHeaderVersionOne) : les données ne sont reprises que pour le même pilote et le
// même GPU. Un fichier tronqué, d'un autre GPU ou d'une autre version du pilote est ignoré.
bool pipelineCacheMatches(const std::vector<char> &data, const VkPhysicalDeviceProperties &props)
{
    if (data.size() < sizeof(VkPipelineCacheHeaderVersionOne))
        return false;
    VkPipelineCacheHeaderVersionOne header{};
    std::memcpy(&header, data.data(), sizeof header);
    return header.headerSize >= sizeof header && header.headerSize <= data.size() && header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header.vendorID == props.vendorID && header.deviceID == props.deviceID &&
           std::memcmp(header.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

// Dispositions de groupe et de pipeline d'un ensemble de shaders.
struct LayoutResult
{
    std::array<const SetLayout *, kGroupCount> sets{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::uint32_t pushSize = 0;
    VkShaderStageFlags pushStages = 0;
};

} // namespace

Result<> VulkanDevice::initPipelines()
{
    std::vector<char> data;
    if (const std::filesystem::path dir = pipelineCacheDirectory(); !dir.empty())
    {
        char name[64];
        std::snprintf(name, sizeof name, "vulkan_%04x_%04x.bin", properties_.vendorID, properties_.deviceID);
        pipelineCachePath_ = (dir / name).string();
        std::ifstream in(pipelineCachePath_, std::ios::binary);
        if (in)
        {
            data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (!pipelineCacheMatches(data, properties_))
            {
                LOG_INFO("Vulkan: pipeline cache ", pipelineCachePath_, " belongs to another GPU or driver: ignored");
                data.clear();
            }
        }
    }
    auto info = makeStruct<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_) != VK_SUCCESS)
    {
        // Données refusées par le pilote : on repart d'un cache vide.
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        if (auto r = fromVk(vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_), "vkCreatePipelineCache"); !r)
        {
            pipelineCache_ = VK_NULL_HANDLE;
            return r;
        }
    }
    pipelineCacheLoadedBytes_ = info.initialDataSize;
    return {};
}

void VulkanDevice::shutdownPipelines() noexcept
{
    if (pipelineCache_ != VK_NULL_HANDLE)
    {
        std::size_t size = 0;
        if (!pipelineCachePath_.empty() && vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) == VK_SUCCESS && size > 0)
        {
            std::vector<char> data(size);
            if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) == VK_SUCCESS)
            {
                // Écrit à côté puis renomme : un autre processus ne lit jamais un fichier à moitié écrit.
                std::error_code ec;
                const std::filesystem::path path(pipelineCachePath_);
                std::filesystem::create_directories(path.parent_path(), ec);
                const std::filesystem::path temp = path.string() + ".tmp" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
                {
                    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                    out.write(data.data(), static_cast<std::streamsize>(size));
                    out.close(); // vide le tampon : une erreur d'écriture différée doit être vue avant le renommage
                    if (!out)
                        ec = std::make_error_code(std::errc::io_error);
                }
                if (!ec)
                    std::filesystem::rename(temp, path, ec);
                if (ec)
                {
                    std::error_code ignored;
                    std::filesystem::remove(temp, ignored);
                    LOG_WARNING("Vulkan: cannot write the pipeline cache ", pipelineCachePath_);
                }
            }
        }
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
    }
    for (auto &[key, layout] : pipelineLayouts_)
        vkDestroyPipelineLayout(device_, layout, nullptr);
    pipelineLayouts_.clear();
    for (auto &layout : setLayouts_)
        vkDestroyDescriptorSetLayout(device_, layout->layout, nullptr);
    setLayouts_.clear();
}

// ── Shaders ──────────────────────────────────────────────────────────────────

Result<ShaderHandle> VulkanDevice::createShader(const ShaderDesc &desc)
{
    if (desc.language != ShaderLanguage::kSpirv)
        return makeError(ErrorCode::kUnsupported, "createShader: the Vulkan backend only accepts SPIR-V (use createShaderFromGlsl) (" + desc.debugName + ")");
    if (desc.code.empty() || desc.code.size() % 4 != 0)
        return makeError(ErrorCode::kInvalidArgument, "createShader: SPIR-V code is empty or not a whole number of words (" + desc.debugName + ")");
    std::vector<std::uint32_t> words(desc.code.size() / 4);
    std::memcpy(words.data(), desc.code.data(), desc.code.size());
    if (words[0] != kSpirvMagic)
        return makeError(ErrorCode::kInvalidArgument, "createShader: not SPIR-V (bad magic number) (" + desc.debugName + ")");
    auto interface = reflectSpirv(words, desc.stage, desc.entryPoint, desc.debugName);
    if (!interface)
        return std::unexpected(interface.error());

    auto info = makeStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = desc.code.size();
    info.pCode = words.data();
    ShaderRes res;
    if (auto r = fromVk(vkCreateShaderModule(device_, &info, nullptr, &res.module), "vkCreateShaderModule"); !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<std::uint64_t>(res.module), desc.debugName);
    res.stage = desc.stage;
    res.entryPoint = desc.entryPoint;
    res.interface = std::move(*interface);
    return shaders_.add(std::move(res));
}

// ── Dispositions ─────────────────────────────────────────────────────────────

Result<const SetLayout *> VulkanDevice::setLayoutFor(const std::vector<DescriptorBinding> &bindings)
{
    for (const auto &existing : setLayouts_)
    {
        if (existing->bindings == bindings)
            return existing.get();
    }
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    std::uint32_t dynamicCount = 0;
    for (const DescriptorBinding &b : bindings)
    {
        VkDescriptorSetLayoutBinding vb{};
        vb.binding = b.binding;
        vb.descriptorType = toVk(b.type);
        vb.descriptorCount = b.count;
        vb.stageFlags = b.stages;
        vkBindings.push_back(vb);
        if (isBufferDescriptor(b.type))
            dynamicCount += b.count;
    }
    auto info = makeStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    info.bindingCount = static_cast<std::uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();
    auto layout = std::make_unique<SetLayout>();
    if (auto r = fromVk(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout->layout), "vkCreateDescriptorSetLayout"); !r)
        return std::unexpected(r.error());
    layout->bindings = bindings;
    layout->dynamicCount = dynamicCount;
    layout->id = nextSetLayoutId_++;
    setLayouts_.push_back(std::move(layout));
    return setLayouts_.back().get();
}

Result<VkPipelineLayout> VulkanDevice::pipelineLayoutFor(const std::array<const SetLayout *, kGroupCount> &sets, std::uint32_t pushSize,
                                                         VkShaderStageFlags pushStages)
{
    // Groupes contigus pour Vulkan : un groupe inutilisé avant un groupe utilisé reçoit la disposition vide.
    std::uint32_t count = 0;
    for (std::uint32_t g = 0; g < kGroupCount; ++g)
    {
        if (sets[g] != nullptr)
            count = g + 1;
    }
    std::vector<VkDescriptorSetLayout> layouts;
    std::vector<std::uint64_t> key;
    for (std::uint32_t g = 0; g < count; ++g)
    {
        const SetLayout *layout = sets[g];
        if (layout == nullptr)
        {
            auto empty = setLayoutFor({});
            if (!empty)
                return std::unexpected(empty.error());
            layout = *empty;
        }
        layouts.push_back(layout->layout);
        key.push_back(layout->id);
    }
    key.push_back(0); // séparateur (aucune disposition n'a l'identifiant 0) : groupes, puis push constants
    key.push_back(pushSize);
    key.push_back(pushStages);
    for (const auto &[existing, layout] : pipelineLayouts_)
    {
        if (existing == key)
            return layout;
    }
    // Les décalages dynamiques sont comptés sur toute la disposition de pipeline (limites du périphérique).
    std::uint32_t dynamicUniform = 0;
    std::uint32_t dynamicStorage = 0;
    for (const SetLayout *set : sets)
    {
        if (set == nullptr)
            continue;
        for (const DescriptorBinding &b : set->bindings)
            (b.type == BindingType::kUniformBuffer ? dynamicUniform : dynamicStorage) += isBufferDescriptor(b.type) ? b.count : 0;
    }
    if (dynamicUniform > properties_.limits.maxDescriptorSetUniformBuffersDynamic || dynamicStorage > properties_.limits.maxDescriptorSetStorageBuffersDynamic)
        return makeError(ErrorCode::kOutOfRange, "pipeline layout: more dynamic uniform (" + std::to_string(dynamicUniform) + ") or storage (" +
                                                     std::to_string(dynamicStorage) + ") buffer descriptors than the device allows");
    const VkPushConstantRange range{pushStages, 0, pushSize};
    auto info = makeStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    info.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    info.pushConstantRangeCount = pushSize > 0 ? 1 : 0;
    info.pPushConstantRanges = pushSize > 0 ? &range : nullptr;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (auto r = fromVk(vkCreatePipelineLayout(device_, &info, nullptr, &layout), "vkCreatePipelineLayout"); !r)
        return std::unexpected(r.error());
    pipelineLayouts_.emplace_back(std::move(key), layout);
    return layout;
}

// ── Pipelines ────────────────────────────────────────────────────────────────

namespace
{
Result<LayoutResult> layoutsFor(VulkanDevice &device, std::span<const ShaderRes *const> shaders, const std::string &name)
{
    auto groups = mergeInterfaces(shaders, name);
    if (!groups)
        return std::unexpected(groups.error());
    LayoutResult out;
    for (std::uint32_t g = 0; g < kGroupCount; ++g)
    {
        if ((*groups)[g].empty())
            continue;
        auto layout = device.setLayoutFor((*groups)[g]);
        if (!layout)
            return std::unexpected(layout.error());
        out.sets[g] = *layout;
    }
    for (const ShaderRes *shader : shaders)
    {
        if (shader->interface.pushConstantSize > 0)
        {
            out.pushSize = std::max(out.pushSize, shader->interface.pushConstantSize);
            out.pushStages |= toVk(shader->stage);
        }
    }
    if (out.pushSize > device.capabilities().maxPushConstantSize)
        return makeError(ErrorCode::kOutOfRange, "pipeline (" + name + "): push constant block larger than Capabilities::maxPushConstantSize");
    auto layout = device.pipelineLayoutFor(out.sets, out.pushSize, out.pushStages);
    if (!layout)
        return std::unexpected(layout.error());
    out.layout = *layout;
    return out;
}

bool sameBlend(const ColorTargetState &a, const ColorTargetState &b) noexcept
{
    return a.blend.enabled == b.blend.enabled && a.blend.srcColor == b.blend.srcColor && a.blend.dstColor == b.blend.dstColor &&
           a.blend.colorOp == b.blend.colorOp && a.blend.srcAlpha == b.blend.srcAlpha && a.blend.dstAlpha == b.blend.dstAlpha &&
           a.blend.alphaOp == b.blend.alphaOp && a.writeMask == b.writeMask;
}
} // namespace

Result<PipelineHandle> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc &desc)
{
    const ShaderRes *vs = shaders_.get(desc.vertex);
    const ShaderRes *fs = shaders_.get(desc.fragment);
    if (vs == nullptr || vs->stage != ShaderStage::kVertex || fs == nullptr || fs->stage != ShaderStage::kFragment)
        return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: needs a live vertex and a live fragment shader (" + desc.debugName + ")");
    if (desc.colorTargets.size() > caps_.maxColorAttachments)
        return makeError(ErrorCode::kOutOfRange, "createGraphicsPipeline: too many color targets (" + desc.debugName + ")");
    for (const ColorTargetState &t : desc.colorTargets)
    {
        if (formatInfo(t.format).bytesPerTexel == 0 || formatInfo(t.format).isDepth())
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: invalid color target format (" + desc.debugName + ")");
        if (!formatSupports(t.format, TextureUsage::kRenderTarget))
            return makeError(ErrorCode::kUnsupported, std::string("createGraphicsPipeline: format ") + toString(t.format) + " cannot be a render target (" +
                                                          desc.debugName + ")");
        if (t.blend.enabled && formatInfo(t.format).kind == FormatKind::kUint)
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: blending on an integer target (" + desc.debugName + ")");
        if (t.blend.enabled)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, vkFormat(t.format), &props);
            if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) == 0)
                return makeError(ErrorCode::kUnsupported, std::string("createGraphicsPipeline: format ") + toString(t.format) + " cannot be blended (" +
                                                              desc.debugName + ")");
        }
        if (!features_.independentBlend && !sameBlend(t, desc.colorTargets.front()))
            return makeError(ErrorCode::kUnsupported, "createGraphicsPipeline: per-target blending needs independentBlend (" + desc.debugName + ")");
    }
    if (desc.depthFormat != Format::kUndefined && !formatInfo(desc.depthFormat).isDepth())
        return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: depthFormat is not a depth format (" + desc.debugName + ")");
    if (desc.raster.fill == FillMode::kLine && !caps_.wireframe)
        return makeError(ErrorCode::kUnsupported, "createGraphicsPipeline: FillMode::kLine needs Capabilities::wireframe (" + desc.debugName + ")");

    PipelineRes res;
    std::vector<VkVertexInputBindingDescription> bindings;
    for (const VertexBufferLayout &vb : desc.vertexBuffers)
    {
        if (vb.binding >= kMaxVertexBindings)
            return makeError(ErrorCode::kOutOfRange, "createGraphicsPipeline: vertex buffer binding >= 16 (" + desc.debugName + ")");
        if (res.vertexBindingUsed[vb.binding])
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: vertex buffer binding declared twice (" + desc.debugName + ")");
        res.vertexBindingUsed[vb.binding] = true;
        bindings.push_back({vb.binding, vb.stride, vb.stepRate == VertexStepRate::kInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute &a : desc.vertexAttributes)
    {
        if (a.binding >= kMaxVertexBindings || !res.vertexBindingUsed[a.binding])
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: attribute uses an undeclared vertex buffer binding (" + desc.debugName + ")");
        attributes.push_back({a.location, a.binding, toVk(a.format), a.offset});
    }
    // Une entrée du shader de sommets sans attribut n'a pas de valeur définie en Vulkan (OpenGL lit une constante) : refusé.
    for (std::uint32_t location : vs->interface.inputLocations)
    {
        if (std::none_of(desc.vertexAttributes.begin(), desc.vertexAttributes.end(), [&](const VertexAttribute &a) { return a.location == location; }))
            return makeError(ErrorCode::kInvalidArgument, "createGraphicsPipeline: vertex shader input location " + std::to_string(location) +
                                                              " has no vertex attribute (" + desc.debugName + ")");
    }

    // « Édition de liens » : chaque entrée du fragment reçoit une sortie du sommet de même emplacement et de même type.
    for (const ShaderInterface::StageVariable &in : fs->interface.stageInputs)
    {
        auto out = std::find_if(vs->interface.stageOutputs.begin(), vs->interface.stageOutputs.end(),
                                [&](const ShaderInterface::StageVariable &v) { return v.location == in.location; });
        if (out == vs->interface.stageOutputs.end() || out->baseType != in.baseType || out->vecSize != in.vecSize || out->columns != in.columns ||
            out->arraySize != in.arraySize)
            return makeError(ErrorCode::kParse, "link (" + desc.debugName + "): fragment input '" + in.name + "' (location " + std::to_string(in.location) +
                                                    ") has no vertex output of the same type");
    }

    const ShaderRes *stagesRes[] = {vs, fs};
    auto layouts = layoutsFor(*this, stagesRes, desc.debugName);
    if (!layouts)
        return std::unexpected(layouts.error());

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (std::size_t i = 0; i < 2; ++i)
    {
        stages[i] = makeStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[i].stage = toVk(stagesRes[i]->stage);
        stages[i].module = stagesRes[i]->module;
        stages[i].pName = stagesRes[i]->entryPoint.c_str();
    }

    auto vertexInput = makeStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    auto inputAssembly = makeStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    inputAssembly.topology = toVk(desc.topology);

    auto viewport = makeStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    auto raster = makeStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = toVk(desc.raster.fill);
    raster.cullMode = toVk(desc.raster.cull);
    raster.frontFace = toVk(desc.raster.frontFace);
    raster.lineWidth = 1.0f;

    auto multisample = makeStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    auto depthStencil = makeStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    const bool hasDepth = desc.depthFormat != Format::kUndefined;
    depthStencil.depthTestEnable = hasDepth && desc.depth.test ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = hasDepth && desc.depth.write ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = desc.depth.test ? toVk(desc.depth.compare) : VK_COMPARE_OP_ALWAYS;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    std::vector<VkPipelineColorBlendAttachmentState> blends;
    std::vector<VkFormat> colorFormats;
    for (const ColorTargetState &t : desc.colorTargets)
    {
        VkPipelineColorBlendAttachmentState b{};
        b.blendEnable = t.blend.enabled ? VK_TRUE : VK_FALSE;
        b.srcColorBlendFactor = toVk(t.blend.srcColor);
        b.dstColorBlendFactor = toVk(t.blend.dstColor);
        b.colorBlendOp = toVk(t.blend.colorOp);
        b.srcAlphaBlendFactor = toVk(t.blend.srcAlpha);
        b.dstAlphaBlendFactor = toVk(t.blend.dstAlpha);
        b.alphaBlendOp = toVk(t.blend.alphaOp);
        b.colorWriteMask = toVk(t.writeMask);
        blends.push_back(b);
        colorFormats.push_back(vkFormat(t.format));
        res.colorFormats.push_back(t.format);
    }
    auto blend = makeStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = static_cast<std::uint32_t>(blends.size());
    blend.pAttachments = blends.data();

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic = makeStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    auto rendering = makeStruct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    if (hasDepth)
    {
        rendering.depthAttachmentFormat = vkFormat(desc.depthFormat);
        if ((aspectOf(desc.depthFormat) & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
            rendering.stencilAttachmentFormat = rendering.depthAttachmentFormat;
    }

    auto info = makeStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layouts->layout;
    if (auto r = fromVk(vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &res.pipeline),
                        ("vkCreateGraphicsPipelines (" + desc.debugName + ")").c_str());
        !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(res.pipeline), desc.debugName);
    res.layout = layouts->layout;
    res.compute = false;
    res.sets = layouts->sets;
    res.pushConstantSize = layouts->pushSize;
    res.pushStages = layouts->pushStages;
    res.depthFormat = desc.depthFormat;
    res.topology = desc.topology;
    return pipelines_.add(std::move(res));
}

Result<PipelineHandle> VulkanDevice::createComputePipeline(const ComputePipelineDesc &desc)
{
    const ShaderRes *cs = shaders_.get(desc.compute);
    if (cs == nullptr || cs->stage != ShaderStage::kCompute)
        return makeError(ErrorCode::kInvalidArgument, "createComputePipeline: needs a live compute shader (" + desc.debugName + ")");
    const ShaderRes *stagesRes[] = {cs};
    auto layouts = layoutsFor(*this, stagesRes, desc.debugName);
    if (!layouts)
        return std::unexpected(layouts.error());
    auto info = makeStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    info.stage = makeStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = cs->module;
    info.stage.pName = cs->entryPoint.c_str();
    info.layout = layouts->layout;
    PipelineRes res;
    if (auto r = fromVk(vkCreateComputePipelines(device_, pipelineCache_, 1, &info, nullptr, &res.pipeline),
                        ("vkCreateComputePipelines (" + desc.debugName + ")").c_str());
        !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(res.pipeline), desc.debugName);
    res.layout = layouts->layout;
    res.compute = true;
    res.sets = layouts->sets;
    res.pushConstantSize = layouts->pushSize;
    res.pushStages = layouts->pushStages;
    res.localSize = cs->interface.localSize;
    return pipelines_.add(std::move(res));
}

// ── Groupes de liaison ───────────────────────────────────────────────────────

Result<BindGroupHandle> VulkanDevice::createBindGroup(const BindGroupDesc &desc)
{
    // Mêmes validations (et codes d'erreur) que le backend OpenGL.
    std::uint32_t seen = 0;
    for (const BindGroupEntry &e : desc.entries)
    {
        const std::string where = " (bind group " + desc.debugName + ", binding " + std::to_string(e.binding) + ")";
        if (e.binding >= kBindingsPerGroup)
            return makeError(ErrorCode::kOutOfRange, "createBindGroup: binding >= kBindingsPerGroup" + where);
        if ((seen & (1u << e.binding)) != 0)
            return makeError(ErrorCode::kAlreadyExists, "createBindGroup: duplicate binding" + where);
        seen |= 1u << e.binding;
        switch (e.type)
        {
            case BindingType::kUniformBuffer:
            case BindingType::kStorageBuffer:
            {
                const BufferRes *b = buffers_.get(e.buffer);
                if (b == nullptr)
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null buffer" + where);
                const BufferUsage need = e.type == BindingType::kUniformBuffer ? BufferUsage::kUniform : BufferUsage::kStorage;
                if (!hasFlag(b->desc.usage, need))
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: buffer lacks the Uniform/Storage usage" + where);
                const std::uint64_t align = e.type == BindingType::kUniformBuffer ? caps_.minUniformAlignment : caps_.minStorageAlignment;
                if (e.offset % align != 0 || e.offset > b->desc.size || e.size > b->desc.size - e.offset)
                    return makeError(ErrorCode::kOutOfRange, "createBindGroup: offset misaligned or range exceeds the buffer" + where);
                break;
            }
            case BindingType::kSampledTexture:
            {
                const TextureRes *t = textures_.get(e.texture);
                if (t == nullptr || samplers_.get(e.sampler) == nullptr)
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null texture/sampler" + where);
                if (!hasFlag(t->desc.usage, TextureUsage::kSampled))
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: texture lacks the Sampled usage" + where);
                break;
            }
            case BindingType::kStorageImage:
            {
                const TextureRes *t = textures_.get(e.texture);
                if (t == nullptr)
                    return makeError(ErrorCode::kNotFound, "createBindGroup: stale or null texture" + where);
                if (!hasFlag(t->desc.usage, TextureUsage::kStorage))
                    return makeError(ErrorCode::kInvalidArgument, "createBindGroup: texture lacks the Storage usage" + where);
                break;
            }
        }
    }
    BindGroupRes res;
    res.entries = desc.entries;
    res.debugName = desc.debugName;
    return bindGroups_.add(std::move(res));
}

Result<std::pair<VkDescriptorSet, VkDescriptorPool>> VulkanDevice::allocateDescriptorSet(VkDescriptorSetLayout layout)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (!descriptorPools_.empty())
        {
            auto info = makeStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            info.descriptorPool = descriptorPools_.back();
            info.descriptorSetCount = 1;
            info.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult result = vkAllocateDescriptorSets(device_, &info, &set);
            if (result == VK_SUCCESS)
                return std::pair{set, descriptorPools_.back()};
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                return std::unexpected(fromVk(result, "vkAllocateDescriptorSets").error());
        }
        // Réserve pleine (ou aucune) : une nouvelle. Les anciennes restent ; leurs jeux sont libérés avec leurs groupes.
        const VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSetsPerPool * 2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kSetsPerPool * 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool * 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetsPerPool},
        };
        auto info = makeStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = kSetsPerPool;
        info.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
        info.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (auto r = fromVk(vkCreateDescriptorPool(device_, &info, nullptr, &pool), "vkCreateDescriptorPool"); !r)
            return std::unexpected(r.error());
        descriptorPools_.push_back(pool);
    }
    return makeError(ErrorCode::kOutOfRange, "vkAllocateDescriptorSets: a fresh descriptor pool is too small for this layout");
}

Result<VkDescriptorSet> VulkanDevice::descriptorSetFor(BindGroupHandle handle, const SetLayout &layout)
{
    BindGroupRes *group = bindGroups_.get(handle);
    if (group == nullptr)
        return makeError(ErrorCode::kNotFound, "bind group: stale or null handle");
    // Un jeu en cache référence des vues et des tampons : si une ressource du groupe a été détruite depuis, il est périmé.
    std::uint64_t imageStamp = 0xcbf29ce484222325ull;
    for (const BindGroupEntry &e : group->entries)
    {
        if (e.type == BindingType::kSampledTexture || e.type == BindingType::kStorageImage)
        {
            if (const TextureRes *t = textures_.get(e.texture); t != nullptr)
                imageStamp = (imageStamp ^ std::hash<VkImage>{}(t->image)) * 0x100000001b3ull;
        }
        bool alive = true;
        switch (e.type)
        {
            case BindingType::kUniformBuffer:
            case BindingType::kStorageBuffer: alive = buffers_.get(e.buffer) != nullptr; break;
            case BindingType::kSampledTexture: alive = textures_.get(e.texture) != nullptr && samplers_.get(e.sampler) != nullptr; break;
            case BindingType::kStorageImage: alive = textures_.get(e.texture) != nullptr; break;
        }
        if (!alive)
            return makeError(ErrorCode::kNotFound, "bind group: a resource of the group (" + group->debugName + ", binding " + std::to_string(e.binding) +
                                                       ") was destroyed");
    }
    for (const auto &cached : group->sets)
    {
        if (cached.layoutId == layout.id && cached.imageStamp == imageStamp)
            return cached.set;
    }

    // Remplit les descripteurs à partir des entrées du groupe : élément i d'une liaison tableau `b` = entrée `b + i`.
    struct Write
    {
        std::uint32_t binding;
        std::uint32_t element;
        VkDescriptorType type;
        VkDescriptorBufferInfo buffer;
        VkDescriptorImageInfo image;
    };
    std::vector<Write> writes;
    for (const DescriptorBinding &b : layout.bindings)
    {
        for (std::uint32_t i = 0; i < b.count; ++i)
        {
            const std::uint32_t binding = b.binding + i;
            const std::string where = " (bind group " + group->debugName + ", binding " + std::to_string(binding) + ")";
            auto it = std::find_if(group->entries.begin(), group->entries.end(), [&](const BindGroupEntry &e) { return e.binding == binding; });
            if (it == group->entries.end())
                return makeError(ErrorCode::kInvalidArgument, "bind group: the pipeline needs a binding the group does not provide" + where);
            if (it->type != b.type)
                return makeError(ErrorCode::kInvalidArgument, "bind group: the binding type differs from the shader's" + where);
            Write w{b.binding, i, toVk(b.type), {}, {}};
            switch (b.type)
            {
                case BindingType::kUniformBuffer:
                case BindingType::kStorageBuffer:
                {
                    const BufferRes *buffer = buffers_.get(it->buffer);
                    if (buffer == nullptr)
                        return makeError(ErrorCode::kNotFound, "bind group: a buffer of the group was destroyed" + where);
                    // Taille 0 : jusqu'à la fin du tampon, décalage dynamique déduit au moment de la liaison (VK_WHOLE_SIZE).
                    w.buffer = {buffer->buffer, it->offset, it->size != 0 ? it->size : VK_WHOLE_SIZE};
                    break;
                }
                case BindingType::kSampledTexture:
                {
                    const SamplerRes *sampler = samplers_.get(it->sampler);
                    const TextureRes *texture = textures_.get(it->texture);
                    if (sampler == nullptr || texture == nullptr)
                        return makeError(ErrorCode::kNotFound, "bind group: a texture or sampler of the group was destroyed" + where);
                    const VkImageLayout rest = texture->restLayout;
                    auto view = fullView(it->texture);
                    if (!view)
                        return std::unexpected(view.error());
                    w.image = {sampler->sampler, *view, rest};
                    break;
                }
                case BindingType::kStorageImage:
                {
                    const TextureRes *texture = textures_.get(it->texture);
                    if (texture == nullptr)
                        return makeError(ErrorCode::kNotFound, "bind group: a texture of the group was destroyed" + where);
                    if (it->mipLevel >= texture->levels)
                        return makeError(ErrorCode::kOutOfRange, "bind group: storage image mip level out of range" + where);
                    ImageViewKey key;
                    key.baseMip = it->mipLevel;
                    key.mipCount = 1;
                    key.layerCount = texture->desc.dimension == TextureDimension::k3D ? 1 : texture->layers;
                    switch (texture->desc.dimension)
                    {
                        case TextureDimension::k2D: key.type = VK_IMAGE_VIEW_TYPE_2D; break;
                        case TextureDimension::k2DArray: key.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
                        // imageCube côté shader (comme une liaison « layered » d'OpenGL) : une vue 2D_ARRAY y est refusée
                        // (VUID-vkCmdDispatch-viewType-07752).
                        case TextureDimension::kCube: key.type = VK_IMAGE_VIEW_TYPE_CUBE; break;
                        case TextureDimension::k3D: key.type = VK_IMAGE_VIEW_TYPE_3D; break;
                    }
                    auto view = imageView(it->texture, key);
                    if (!view)
                        return std::unexpected(view.error());
                    w.image = {VK_NULL_HANDLE, *view, VK_IMAGE_LAYOUT_GENERAL};
                    break;
                }
            }
            writes.push_back(w);
        }
    }

    auto allocated = allocateDescriptorSet(layout.layout);
    if (!allocated)
        return std::unexpected(allocated.error());
    std::vector<VkWriteDescriptorSet> vkWrites;
    vkWrites.reserve(writes.size());
    for (const Write &w : writes)
    {
        auto write = makeStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = allocated->first;
        write.dstBinding = w.binding;
        write.dstArrayElement = w.element;
        write.descriptorCount = 1;
        write.descriptorType = w.type;
        if (w.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || w.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            write.pBufferInfo = &w.buffer;
        else
            write.pImageInfo = &w.image;
        vkWrites.push_back(write);
    }
    if (!vkWrites.empty())
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(vkWrites.size()), vkWrites.data(), 0, nullptr);
    group = bindGroups_.get(handle); // aucune adresse gardée d'avant les appels qui ont pu toucher les tables
    setObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<std::uint64_t>(allocated->first), group->debugName);
    group->sets.push_back({layout.id, imageStamp, allocated->first, allocated->second});
    return allocated->first;
}

} // namespace orosmo::rhi::vk
