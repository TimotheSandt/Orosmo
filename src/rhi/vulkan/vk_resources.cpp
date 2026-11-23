// Tampons, textures, échantillonneurs, requêtes ; mises à jour et lectures synchrones (voir README.md, « Ressources »).
#include "vk_device.hpp"
#include "vk_region.hpp"
#include "vk_sync.hpp"
#include "vk_translate.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <numeric>
#include <span>

namespace orosmo::rhi::vk
{

using orosmo::core::ErrorCode;
using orosmo::core::makeError;

namespace
{

// Au-delà, le lot d'envois en attente est soumis sans attendre (la mémoire de transfert ne s'accumule pas).
constexpr std::uint64_t kMaxBatchBytes = 64ull * 1024 * 1024;

VkBufferUsageFlags bufferUsageFlags(BufferUsage usage) noexcept
{
    // Les copies de service (envois, lectures) passent par des transferts : tout tampon est source et destination.
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, BufferUsage::kVertex))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::kIndex))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::kUniform))
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::kStorage))
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::kIndirect))
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return flags;
}

// Disposition de repos d'une texture selon ses usages (README.md, « Dispositions de repos »).
VkImageLayout restLayoutFor(TextureUsage usage, bool depth) noexcept
{
    if (hasFlag(usage, TextureUsage::kStorage))
        return VK_IMAGE_LAYOUT_GENERAL;
    if (hasFlag(usage, TextureUsage::kSampled))
        return depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (hasFlag(usage, TextureUsage::kRenderTarget))
        return depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return VK_IMAGE_LAYOUT_GENERAL; // copies seulement
}

VkImageUsageFlags imageUsageFlags(TextureUsage usage, bool depth) noexcept
{
    VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // envois, lectures, mips
    if (hasFlag(usage, TextureUsage::kSampled))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, TextureUsage::kStorage))
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (hasFlag(usage, TextureUsage::kRenderTarget))
        flags |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return flags;
}

} // namespace

// ── Région d'une texture ─────────────────────────────────────────────────────

Result<ResolvedRegion> resolveRegion(const TextureRes &texture, const TextureCopy &copy)
{
    ResolvedRegion r;
    r.mip = copy.mipLevel;
    r.offset = copy.offset;
    if (copy.mipLevel >= texture.levels)
        return makeError(ErrorCode::kOutOfRange, "texture region: mip level out of range (" + texture.desc.debugName + ")");
    const std::uint32_t lw = mipDim(texture.desc.size.width, copy.mipLevel);
    const std::uint32_t lh = mipDim(texture.desc.size.height, copy.mipLevel);
    const bool is3D = texture.desc.dimension == TextureDimension::k3D;
    const bool layered = texture.desc.dimension == TextureDimension::k2DArray || texture.desc.dimension == TextureDimension::kCube;
    const std::uint32_t ld = is3D ? mipDim(texture.desc.size.depth, copy.mipLevel) : 1;
    if (layered)
    {
        r.layer = copy.layer;
        if (copy.layer >= texture.layers)
            return makeError(ErrorCode::kOutOfRange, "texture region: layer out of range (" + texture.desc.debugName + ")");
    }
    Extent3D size = copy.size;
    if (size.width == 0 && size.height == 0 && size.depth == 0)
    {
        size = {lw - static_cast<std::uint32_t>(std::max(copy.offset.x, 0)), lh - static_cast<std::uint32_t>(std::max(copy.offset.y, 0)),
                is3D ? ld - static_cast<std::uint32_t>(std::max(copy.offset.z, 0)) : (layered ? texture.layers - copy.layer : 1)};
    }
    if (!is3D && copy.offset.z != 0)
        return makeError(ErrorCode::kInvalidArgument, "texture region: a non-zero z offset is only valid for 3D textures (" + texture.desc.debugName + ")");
    if (size.width == 0 || size.height == 0 || size.depth == 0 || copy.offset.x < 0 || copy.offset.y < 0 || copy.offset.z < 0)
        return makeError(ErrorCode::kInvalidArgument, "texture region: empty or negative region (" + texture.desc.debugName + ")");
    const bool fits = static_cast<std::uint64_t>(copy.offset.x) + size.width <= lw && static_cast<std::uint64_t>(copy.offset.y) + size.height <= lh &&
                      (is3D ? static_cast<std::uint64_t>(copy.offset.z) + size.depth <= ld
                            : (layered ? static_cast<std::uint64_t>(copy.layer) + size.depth <= texture.layers : size.depth == 1));
    if (!fits)
        return makeError(ErrorCode::kOutOfRange, "texture region exceeds the level (" + texture.desc.debugName + ")");
    if (is3D)
    {
        r.extent = size;
    }
    else
    {
        r.extent = {size.width, size.height, 1};
        r.layerCount = layered ? size.depth : 1;
    }
    return r;
}

Result<> VulkanDevice::checkDepthCopy(const TextureRes &texture, const char *what) const
{
    // Repli D24S8 -> D32S8 : les octets D24 (4 octets par texel) seraient lus comme D32F, donc faux sans erreur. Refusé.
    if (texture.desc.format == Format::kD24UnormS8Uint && texture.format != VK_FORMAT_D24_UNORM_S8_UINT)
        return makeError(ErrorCode::kUnsupported,
                         std::string(what) + ": D24S8 copies are not supported when the device replaces it with D32S8 (" + texture.desc.debugName + ")");
    return {};
}

VkBufferImageCopy bufferImageCopy(const TextureRes &texture, const ResolvedRegion &region, std::uint64_t bufferOffset, std::uint32_t bufferRowLength)
{
    VkBufferImageCopy copy{};
    copy.bufferOffset = bufferOffset;
    copy.bufferRowLength = bufferRowLength;
    copy.bufferImageHeight = 0;
    // Profondeur seule pour D/S (le RHI ne copie pas le pochoir).
    copy.imageSubresource.aspectMask = (texture.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT) : texture.aspect;
    copy.imageSubresource.mipLevel = region.mip;
    copy.imageSubresource.baseArrayLayer = region.layer;
    copy.imageSubresource.layerCount = region.layerCount;
    copy.imageOffset = {region.offset.x, region.offset.y, region.offset.z};
    copy.imageExtent = {region.extent.width, region.extent.height, region.extent.depth};
    return copy;
}

// ── Tampons ──────────────────────────────────────────────────────────────────

Result<BufferHandle> VulkanDevice::createBuffer(const BufferDesc &desc, std::span<const std::byte> initial)
{
    if (desc.size == 0)
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: size is 0 (" + desc.debugName + ")");
    if (desc.usage == BufferUsage::kNone)
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: no usage flag (" + desc.debugName + ")");
    if (initial.size() > desc.size)
        return makeError(ErrorCode::kInvalidArgument, "createBuffer: initial data larger than the buffer (" + desc.debugName + ")");
    collectReleases();

    auto info = makeStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = desc.size;
    info.usage = bufferUsageFlags(desc.usage);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    switch (desc.memory)
    {
        case MemoryKind::kGpuOnly: alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; break;
        case MemoryKind::kUpload:
            alloc.usage = VMA_MEMORY_USAGE_AUTO;
            alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case MemoryKind::kReadback:
            alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }
    BufferRes res;
    res.desc = desc;
    VmaAllocationInfo allocInfo{};
    if (auto r = fromVk(vmaCreateBuffer(allocator_, &info, &alloc, &res.buffer, &res.allocation, &allocInfo),
                        ("createBuffer: cannot allocate " + std::to_string(desc.size) + " bytes (" + desc.debugName + ")").c_str());
        !r)
        return std::unexpected(r.error());
    res.mapped = allocInfo.pMappedData;
    setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(res.buffer), desc.debugName);

    const BufferHandle handle = buffers_.add(res);
    if (!initial.empty())
    {
        if (res.mapped != nullptr)
        {
            // Aucun GPU ne l'utilise encore : écriture directe.
            std::memcpy(res.mapped, initial.data(), initial.size());
            vmaFlushAllocation(allocator_, res.allocation, 0, initial.size());
        }
        else if (auto r = updateBuffer(handle, 0, initial); !r)
        {
            destroy(handle);
            return std::unexpected(r.error());
        }
    }
    return handle;
}

Result<> VulkanDevice::updateBuffer(BufferHandle handle, std::uint64_t offset, std::span<const std::byte> data)
{
    BufferRes *buffer = buffers_.get(handle);
    if (buffer == nullptr)
        return makeError(ErrorCode::kNotFound, "updateBuffer: stale or null buffer handle");
    if (offset > buffer->desc.size || data.size() > buffer->desc.size - offset)
        return makeError(ErrorCode::kOutOfRange, "updateBuffer: range exceeds the buffer (" + buffer->desc.debugName + ")");
    if (data.empty())
        return {};
    const bool upload = buffer->desc.memory == MemoryKind::kUpload && buffer->mapped != nullptr;
    if (upload && buffer->gpuUse <= completedSerial())
    {
        // Tampon « écrit par le CPU, lu par le GPU » que plus aucun travail du GPU n'utilise (soumission finie, aucune copie en
        // attente) : écriture directe dans sa mémoire mappée, sans transfert ni barrière (les écritures de l'hôte faites avant une
        // soumission lui sont visibles). Encore utilisé : copie ordonnée ci-dessous, comme pour kGpuOnly (même résultat).
        std::memcpy(static_cast<std::byte *>(buffer->mapped) + offset, data.data(), data.size());
        vmaFlushAllocation(allocator_, buffer->allocation, offset, data.size());
        ++counters_.directWrites;
        return {};
    }
    // Copie ordonnée avec les soumissions (comme glBufferSubData) : place dans l'anneau de transfert, copie dans le lot joint à la
    // prochaine soumission ; une barrière seulement si la plage recouvre une copie précédente du lot.
    UploadSpace space;
    auto commandBuffer = stageUpload(data, 16, space);
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    batchCopyBarrier(buffer->buffer, offset, data.size());
    const VkBufferCopy region{space.offset, offset, data.size()};
    vkCmdCopyBuffer(*commandBuffer, space.buffer, buffer->buffer, 1, &region);
    if (upload)
    {
        buffer->gpuUse = lastSubmitted_ + 1; // la copie part avec la prochaine soumission
        ++counters_.orderedUploadWrites;
    }
    if (batchBytes_ > kMaxBatchBytes)
        return flushPending();
    return {};
}

// Tampon de lecture : libéré tout de suite si la copie est finie ou n'est jamais partie ; sinon (attente échouée après
// soumission acceptée) gardé jusqu'à la série acceptée.
void VulkanDevice::releaseReadback(const Staging &staging, bool immediateRelease)
{
    if (immediateRelease)
        destroyStaging(staging);
    else
        queueRelease([this, staging]() { destroyStaging(staging); }, false);
}

Result<std::vector<std::byte>> VulkanDevice::readBuffer(BufferHandle handle, std::uint64_t offset, std::uint64_t size)
{
    BufferRes *buffer = buffers_.get(handle);
    if (buffer == nullptr)
        return makeError(ErrorCode::kNotFound, "readBuffer: stale or null buffer handle");
    if (offset > buffer->desc.size || size > buffer->desc.size - offset)
        return makeError(ErrorCode::kOutOfRange, "readBuffer: range exceeds the buffer (" + buffer->desc.debugName + ")");
    std::vector<std::byte> out(size);
    if (size == 0)
        return out;
    if (buffer->mapped != nullptr)
    {
        if (auto r = flushPending(); !r)
            return std::unexpected(r.error());
        if (auto r = waitSerial(lastSubmitted_); !r)
            return std::unexpected(r.error());
        vmaInvalidateAllocation(allocator_, buffer->allocation, offset, size);
        std::memcpy(out.data(), static_cast<const std::byte *>(buffer->mapped) + offset, size);
        return out;
    }
    auto staging = makeStaging(size, true);
    if (!staging)
        return std::unexpected(staging.error());
    const VkBuffer source = buffer->buffer;
    bool accepted = false;
    auto r = immediate(
        [&](VkCommandBuffer cb)
        {
            memoryBarrierAll(cb);
            const VkBufferCopy region{offset, 0, size};
            vkCmdCopyBuffer(cb, source, staging->buffer, 1, &region);
            memoryBarrierAll(cb);
        },
        &accepted);
    if (r)
    {
        vmaInvalidateAllocation(allocator_, staging->allocation, 0, size);
        std::memcpy(out.data(), staging->mapped, size);
    }
    releaseReadback(*staging, r.has_value() || !accepted);
    if (!r)
        return std::unexpected(r.error());
    return out;
}

// ── Textures ─────────────────────────────────────────────────────────────────

Result<TextureHandle> VulkanDevice::createTexture(const TextureDesc &requested)
{
    TextureDesc desc = requested;
    const std::string &name = desc.debugName;
    if (desc.format == Format::kUndefined || vkFormat(desc.format) == VK_FORMAT_UNDEFINED)
        return makeError(ErrorCode::kInvalidArgument, "createTexture: undefined format (" + name + ")");
    if (desc.usage == TextureUsage::kNone)
        return makeError(ErrorCode::kInvalidArgument, "createTexture: no usage flag (" + name + ")");
    if (desc.size.width == 0 || desc.size.height == 0 || desc.size.depth == 0)
        return makeError(ErrorCode::kInvalidArgument, "createTexture: empty extent (" + name + ")");
    const bool depth = formatInfo(desc.format).isDepth();
    if (hasFlag(desc.usage, TextureUsage::kStorage) && depth)
        return makeError(ErrorCode::kUnsupported, std::string("createTexture: format ") + toString(desc.format) + " cannot be a storage image (" + name + ")");
    // Les copies de service (envois, lectures) exigent source et destination de transfert : un format qui ne les offre pas est refusé.
    const TextureUsage wanted = desc.usage | TextureUsage::kCopySrc | TextureUsage::kCopyDst;
    if (!formatSupports(desc.format, wanted))
    {
        if (hasFlag(desc.usage, TextureUsage::kStorage) && !formatSupports(desc.format, TextureUsage::kStorage))
            return makeError(ErrorCode::kUnsupported, std::string("createTexture: format ") + toString(desc.format) + " cannot be a storage image (" + name + ")");
        return makeError(ErrorCode::kUnsupported, std::string("createTexture: format ") + toString(desc.format) + " is not supported with these usages (" + name + ")");
    }

    std::uint32_t layers = 1;
    switch (desc.dimension)
    {
        case TextureDimension::k2D: desc.size.depth = 1; break;
        case TextureDimension::k2DArray:
            layers = std::max(desc.layers, 1u);
            desc.size.depth = 1;
            break;
        case TextureDimension::kCube:
            layers = 6;
            desc.size.depth = 1;
            if (desc.size.width != desc.size.height)
                return makeError(ErrorCode::kInvalidArgument, "createTexture: cube faces must be square (" + name + ")");
            break;
        case TextureDimension::k3D: break;
    }
    desc.layers = layers;
    const std::uint32_t maxDim = desc.dimension == TextureDimension::k3D ? caps_.max3DTextureSize : caps_.maxTextureSize;
    if (std::max({desc.size.width, desc.size.height, desc.size.depth}) > maxDim)
        return makeError(ErrorCode::kOutOfRange, "createTexture: extent exceeds the device limit (" + name + ")");
    if (layers > properties_.limits.maxImageArrayLayers)
        return makeError(ErrorCode::kOutOfRange, "createTexture: more array layers than the device allows (" + name + ")");
    const std::uint32_t full = fullMipCount(desc.size.width, desc.size.height, desc.dimension == TextureDimension::k3D ? desc.size.depth : 1);
    const std::uint32_t levels = desc.mipLevels == 0 ? full : desc.mipLevels;
    if (levels > full)
        return makeError(ErrorCode::kInvalidArgument, "createTexture: more mip levels than the extent allows (" + name + ")");
    desc.mipLevels = levels;
    collectReleases();

    TextureRes res;
    res.desc = desc;
    res.format = vkFormat(desc.format);
    res.aspect = aspectOf(desc.format);
    res.levels = levels;
    res.layers = layers;
    res.restLayout = restLayoutFor(desc.usage, depth);

    auto info = makeStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    if (desc.dimension == TextureDimension::kCube)
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (desc.dimension == TextureDimension::k3D && hasFlag(desc.usage, TextureUsage::kRenderTarget))
        info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT; // une tranche d'une 3D est une cible de rendu
    info.imageType = toVkImageType(desc.dimension);
    info.format = res.format;
    info.extent = {desc.size.width, desc.size.height, desc.dimension == TextureDimension::k3D ? desc.size.depth : 1};
    info.mipLevels = levels;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = imageUsageFlags(desc.usage, depth);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (hasFlag(desc.usage, TextureUsage::kRenderTarget) || hasFlag(desc.usage, TextureUsage::kStorage))
        alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT; // cibles et images de stockage : mémoire propre
    if (auto r = fromVk(vmaCreateImage(allocator_, &info, &alloc, &res.image, &res.allocation, nullptr),
                        ("createTexture: cannot allocate the image (" + name + ")").c_str());
        !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(res.image), name);
    const TextureHandle handle = textures_.add(std::move(res));
    markPendingInitialization(handle);
    return handle;
}

Result<VkImageView> VulkanDevice::imageView(TextureHandle handle, const ImageViewKey &key)
{
    TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
        return makeError(ErrorCode::kInvalidArgument, "imageView: stale or null texture handle");
    for (const auto &[existing, view] : texture->views)
    {
        if (existing == key)
            return view;
    }
    // Une vue 2D d'une texture 3D (image 2D_ARRAY_COMPATIBLE) désigne une tranche du niveau : ses « couches » sont les tranches.
    const bool slices = texture->desc.dimension == TextureDimension::k3D && key.type != VK_IMAGE_VIEW_TYPE_3D;
    const std::uint32_t layerLimit = slices ? mipDim(texture->desc.size.depth, key.baseMip) : texture->layers;
    if (key.baseMip + key.mipCount > texture->levels || key.baseLayer + key.layerCount > layerLimit || (slices && key.mipCount != 1))
        return makeError(ErrorCode::kOutOfRange, "imageView: range exceeds the texture (" + texture->desc.debugName + ")");
    auto info = makeStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    info.image = texture->image;
    info.viewType = key.type;
    info.format = texture->format;
    info.subresourceRange.aspectMask = key.aspect != 0 ? key.aspect : texture->aspect;
    info.subresourceRange.baseMipLevel = key.baseMip;
    info.subresourceRange.levelCount = key.mipCount;
    info.subresourceRange.baseArrayLayer = key.baseLayer;
    info.subresourceRange.layerCount = key.layerCount;
    VkImageView view = VK_NULL_HANDLE;
    if (auto r = fromVk(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView"); !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view), texture->desc.debugName);
    texture->views.emplace_back(key, view);
    return view;
}

Result<VkImageView> VulkanDevice::fullView(TextureHandle handle)
{
    const TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
        return makeError(ErrorCode::kInvalidArgument, "fullView: stale or null texture handle");
    ImageViewKey key;
    key.mipCount = texture->levels;
    key.layerCount = texture->desc.dimension == TextureDimension::k3D ? 1 : texture->layers;
    key.type = toVkViewType(texture->desc.dimension);
    // Échantillonner un format profondeur-pochoir ne lit que la profondeur.
    if ((texture->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
        key.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    return imageView(handle, key);
}

Result<> VulkanDevice::updateTexture(TextureHandle handle, const TextureCopy &copy, std::span<const std::byte> data)
{
    TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
        return makeError(ErrorCode::kNotFound, "updateTexture: stale or null texture handle");
    if (handle == backbuffer_)
        return makeError(ErrorCode::kInvalidArgument, "updateTexture: the backbuffer cannot be written by upload; render into it");
    if (auto r = checkDepthCopy(*texture, "updateTexture"); !r)
        return r;
    auto region = resolveRegion(*texture, copy);
    if (!region)
        return std::unexpected(region.error());
    // Depuis la mémoire du processeur, les lignes sont serrées : `bufferRowLength` et `bufferOffset` ne concernent que les copies
    // tampon <-> texture (contrat de TextureCopy).
    const std::uint32_t bpt = formatInfo(texture->desc.format).bytesPerTexel;
    const std::uint64_t needed = region->tightBytes(bpt);
    if (data.size() < needed)
        return makeError(ErrorCode::kInvalidArgument, "updateTexture: " + std::to_string(data.size()) + " bytes given, " + std::to_string(needed) +
                                                          " needed (" + texture->desc.debugName + ")");
    // Décalage dans le tampon de transfert : multiple de la taille d'un texel et de 4 (vkCmdCopyBufferToImage).
    UploadSpace space;
    auto commandBuffer = stageUpload(data.first(needed), std::lcm<std::uint64_t>(std::max(bpt, 1u), 16), space);
    if (!commandBuffer)
        return std::unexpected(commandBuffer.error());
    const VkBufferImageCopy imageCopy = bufferImageCopy(*texture, *region, space.offset, 0);
    transitionImage(*commandBuffer, texture->image, texture->aspect, texture->restLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(*commandBuffer, space.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
    transitionImage(*commandBuffer, texture->image, texture->aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture->restLayout);
    if (batchBytes_ > kMaxBatchBytes)
        return flushPending();
    return {};
}

Result<std::vector<std::byte>> VulkanDevice::readTexture(TextureHandle handle, const TextureCopy &copy)
{
    TextureRes *texture = textures_.get(handle);
    if (texture == nullptr)
        return makeError(ErrorCode::kNotFound, "readTexture: stale or null texture handle");
    if (auto r = checkDepthCopy(*texture, "readTexture"); !r)
        return std::unexpected(r.error());
    auto region = resolveRegion(*texture, copy);
    if (!region)
        return std::unexpected(region.error());
    const std::uint32_t bpt = formatInfo(texture->desc.format).bytesPerTexel;
    const std::uint64_t bytes = region->tightBytes(bpt);
    auto staging = makeStaging(bytes, true);
    if (!staging)
        return std::unexpected(staging.error());
    const VkBufferImageCopy imageCopy = bufferImageCopy(*texture, *region, 0, 0);
    const TextureRes snapshot = *texture; // image, disposition, aspect (la texture ne bouge pas pendant l'appel)
    bool accepted = false;
    auto r = immediate(
        [&](VkCommandBuffer cb)
        {
            transitionImage(cb, snapshot.image, snapshot.aspect, snapshot.restLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            vkCmdCopyImageToBuffer(cb, snapshot.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->buffer, 1, &imageCopy);
            transitionImage(cb, snapshot.image, snapshot.aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, snapshot.restLayout);
            memoryBarrierAll(cb);
        },
        &accepted);
    std::vector<std::byte> out;
    if (r)
    {
        vmaInvalidateAllocation(allocator_, staging->allocation, 0, bytes);
        out.resize(bytes);
        std::memcpy(out.data(), staging->mapped, bytes);
    }
    releaseReadback(*staging, r.has_value() || !accepted);
    if (!r)
        return std::unexpected(r.error());
    return out;
}

// ── Échantillonneurs ────────────────────────────────────────────────────────

Result<SamplerHandle> VulkanDevice::createSampler(const SamplerDesc &desc)
{
    if (desc.maxAnisotropy > 1.0f && !caps_.anisotropy)
        return makeError(ErrorCode::kUnsupported, "createSampler: anisotropic filtering needs Capabilities::anisotropy (" + desc.debugName + ")");
    auto info = makeStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    info.magFilter = toVk(desc.magFilter);
    info.minFilter = toVk(desc.minFilter);
    info.mipmapMode = toVkMipmapMode(desc.mipFilter.value_or(Filter::kNearest));
    info.addressModeU = toVk(desc.addressU);
    info.addressModeV = toVk(desc.addressV);
    info.addressModeW = toVk(desc.addressW);
    info.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = std::min(std::max(desc.maxAnisotropy, 1.0f), properties_.limits.maxSamplerAnisotropy);
    info.compareEnable = desc.compare.has_value() ? VK_TRUE : VK_FALSE;
    info.compareOp = toVk(desc.compare.value_or(CompareOp::kAlways));
    info.minLod = desc.minLod;
    // Pas de mips (mipFilter vide) : un seul niveau, comme OpenGL.
    // Sans mipFilter, OpenGL reste au niveau 0 mais minifie avec minFilter : en Vulkan, le filtre de minification ne s'applique que
    // si le LOD calculé dépasse 0, donc la plage va de minLod à minLod + 0,25 (mipmapMode NEAREST arrondit toujours au niveau 0).
    info.maxLod = desc.mipFilter.has_value() ? std::min(desc.maxLod, VK_LOD_CLAMP_NONE) : desc.minLod + 0.25f;
    // Couleurs de bord : les trois valeurs fixes de Vulkan (noir transparent, noir opaque, blanc opaque) ; la plus proche est prise.
    const auto &c = desc.borderColor;
    if (c[3] < 0.5f)
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if (c[0] + c[1] + c[2] >= 1.5f)
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    else
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    SamplerRes res;
    if (auto r = fromVk(vkCreateSampler(device_, &info, nullptr, &res.sampler), "vkCreateSampler"); !r)
        return std::unexpected(r.error());
    setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(res.sampler), desc.debugName);
    return samplers_.add(res);
}

// ── Requêtes de temps ──────────────────────────────────────────────────────

Result<QueryPoolHandle> VulkanDevice::createQueryPool(std::uint32_t count, const char *debugName)
{
    if (!caps_.timestampQueries)
        return makeError(ErrorCode::kUnsupported, "createQueryPool: the device has no timestamp queries");
    if (count == 0)
        return makeError(ErrorCode::kInvalidArgument, "createQueryPool: count is 0");
    auto info = makeStruct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = count;
    QueryPoolRes res;
    res.count = count;
    res.debugName = debugName != nullptr ? debugName : "";
    if (auto r = fromVk(vkCreateQueryPool(device_, &info, nullptr, &res.pool), "vkCreateQueryPool"); !r)
        return std::unexpected(r.error());
    vkResetQueryPool(device_, res.pool, 0, count); // « non disponible » tant qu'aucun horodatage n'est écrit
    setObjectName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<std::uint64_t>(res.pool), res.debugName);
    return queryPools_.add(std::move(res));
}

Result<bool> VulkanDevice::timestampsReady(QueryPoolHandle handle, std::uint32_t first, std::uint32_t count)
{
    QueryPoolRes *pool = queryPools_.get(handle);
    if (pool == nullptr)
        return makeError(ErrorCode::kNotFound, "timestampsReady: stale or null query pool handle");
    if (first > pool->count || count > pool->count - first)
        return makeError(ErrorCode::kOutOfRange, "timestampsReady: range exceeds the pool");
    if (count == 0)
        return true;
    // (valeur, disponibilité) par requête ; quelques requêtes (cas courant : début et fin d'une passe) sans allocation.
    std::array<std::uint64_t, 16> small{};
    std::vector<std::uint64_t> large;
    std::span<std::uint64_t> data = std::span(small).first(std::min<std::size_t>(small.size(), static_cast<std::size_t>(count) * 2));
    if (static_cast<std::size_t>(count) * 2 > small.size())
    {
        large.resize(static_cast<std::size_t>(count) * 2);
        data = large;
    }
    const VkResult result = vkGetQueryPoolResults(device_, pool->pool, first, count, data.size_bytes(), data.data(), 2 * sizeof(std::uint64_t),
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY)
        return std::unexpected(fromVk(result, "vkGetQueryPoolResults").error());
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (data[static_cast<std::size_t>(i) * 2 + 1] == 0)
            return false;
    }
    return true;
}

Result<std::vector<std::uint64_t>> VulkanDevice::readTimestamps(QueryPoolHandle handle, std::uint32_t first, std::uint32_t count)
{
    QueryPoolRes *pool = queryPools_.get(handle);
    if (pool == nullptr)
        return makeError(ErrorCode::kNotFound, "readTimestamps: stale or null query pool handle");
    if (first > pool->count || count > pool->count - first)
        return makeError(ErrorCode::kOutOfRange, "readTimestamps: range exceeds the pool");
    std::vector<std::uint64_t> values(count);
    if (count == 0)
        return values;
    // Attend les horodatages écrits : un horodatage jamais écrit ne se résoudra pas (kBusy, pas de blocage éternel). Seuls des
    // encodeurs soumis écrivent des horodatages (jamais le lot de transferts) : déjà prêts, ils se lisent sans soumission ni attente.
    auto ready = timestampsReady(handle, first, count);
    if (!ready)
        return std::unexpected(ready.error());
    if (!*ready)
    {
        if (auto r = waitSerial(lastSubmitted_); !r)
            return std::unexpected(r.error());
        ready = timestampsReady(handle, first, count);
        if (!ready)
            return std::unexpected(ready.error());
        if (!*ready)
            return makeError(ErrorCode::kBusy, "readTimestamps: a timestamp was never written");
    }
    const VkResult result = vkGetQueryPoolResults(device_, pool->pool, first, count, values.size() * sizeof(std::uint64_t), values.data(), sizeof(std::uint64_t),
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS)
        return std::unexpected(fromVk(result, "vkGetQueryPoolResults").error());
    const double period = properties_.limits.timestampPeriod; // nanosecondes par tic
    for (std::uint64_t &v : values)
        v = static_cast<std::uint64_t>(static_cast<double>(v) * period);
    return values;
}

} // namespace orosmo::rhi::vk
