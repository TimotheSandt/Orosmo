// Petits outils du backend Vulkan (privé).
#pragma once

#include <volk.h>

namespace orosmo::rhi::vk
{

// Structure Vulkan à zéro avec son `sType` : évite les avertissements d'initialisation partielle de `VkX x{sType}`.
template <typename T> constexpr T makeStruct(VkStructureType type) noexcept
{
    T value{};
    value.sType = type;
    return value;
}

} // namespace orosmo::rhi::vk
