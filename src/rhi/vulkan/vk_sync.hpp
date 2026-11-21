// Barrières (privé au backend Vulkan). Stratégie du backend (README.md) : barrières CONSERVATRICES entre commandes (tout contre tout),
// transitions de disposition PRÉCISES (disposition de repos <-> disposition de la commande).
#pragma once

#include "vk_util.hpp"

namespace orosmo::rhi::vk
{

// Dépendance mémoire complète : tout ce qui a été enregistré avant est fini et visible avant tout ce qui suit.
inline void memoryBarrierAll(VkCommandBuffer commandBuffer)
{
    auto barrier = makeStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    auto dependency = makeStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

// Transition de disposition d'une image (toute ou une plage), avec la même dépendance complète. `from` UNDEFINED : contenu perdu.
inline void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                            std::uint32_t baseMip = 0, std::uint32_t mipCount = VK_REMAINING_MIP_LEVELS, std::uint32_t baseLayer = 0,
                            std::uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS)
{
    auto barrier = makeStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, baseMip, mipCount, baseLayer, layerCount};
    auto dependency = makeStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

} // namespace orosmo::rhi::vk
