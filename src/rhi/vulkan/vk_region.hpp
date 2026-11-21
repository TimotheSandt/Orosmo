// Région d'une texture (privé au backend Vulkan) : même contrat que le backend OpenGL (`TextureCopy` -> région résolue).
#pragma once

#include "vk_device.hpp"

#include <algorithm>

namespace orosmo::rhi::vk
{

inline std::uint32_t mipDim(std::uint32_t base, std::uint32_t level) noexcept { return std::max(1u, base >> level); }

inline std::uint32_t fullMipCount(std::uint32_t w, std::uint32_t h, std::uint32_t d) noexcept
{
    std::uint32_t m = std::max({w, h, d, 1u});
    std::uint32_t levels = 1;
    while (m > 1)
    {
        m >>= 1;
        ++levels;
    }
    return levels;
}

// Région résolue : `extent.depth` = tranches pour une 3D, 1 sinon ; `layerCount` = couches (tableau, cube), 1 sinon.
struct ResolvedRegion
{
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    std::uint32_t layerCount = 1;
    Offset3D offset{};
    Extent3D extent{1, 1, 1};
    // Octets d'une copie à lignes serrées.
    std::uint64_t tightBytes(std::uint32_t bytesPerTexel) const noexcept
    {
        return static_cast<std::uint64_t>(extent.width) * extent.height * extent.depth * layerCount * bytesPerTexel;
    }
};

Result<ResolvedRegion> resolveRegion(const TextureRes &texture, const TextureCopy &copy);

// `VkBufferImageCopy` pour cette région ; `bufferRowLength` en texels (0 : lignes serrées). Aspect : profondeur seule pour D/S.
VkBufferImageCopy bufferImageCopy(const TextureRes &texture, const ResolvedRegion &region, std::uint64_t bufferOffset, std::uint32_t bufferRowLength);

} // namespace orosmo::rhi::vk
