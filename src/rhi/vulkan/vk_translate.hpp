// Conversions RHI -> Vulkan (privé au backend). Aucune logique : des tables. Voir README.md.
#pragma once

#include "orosmo/rhi/types.hpp"

#include <volk.h>

namespace orosmo::rhi::vk
{

constexpr VkFilter toVk(Filter f) noexcept { return f == Filter::kNearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

constexpr VkSamplerMipmapMode toVkMipmapMode(Filter f) noexcept
{
    return f == Filter::kNearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

constexpr VkSamplerAddressMode toVk(AddressMode m) noexcept
{
    switch (m)
    {
        case AddressMode::kRepeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::kMirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::kClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::kClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

constexpr VkCompareOp toVk(CompareOp op) noexcept
{
    switch (op)
    {
        case CompareOp::kNever: return VK_COMPARE_OP_NEVER;
        case CompareOp::kLess: return VK_COMPARE_OP_LESS;
        case CompareOp::kEqual: return VK_COMPARE_OP_EQUAL;
        case CompareOp::kLessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::kGreater: return VK_COMPARE_OP_GREATER;
        case CompareOp::kNotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::kGreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::kAlways: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

constexpr VkPrimitiveTopology toVk(PrimitiveTopology t) noexcept
{
    switch (t)
    {
        case PrimitiveTopology::kTriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::kTriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::kLineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::kLineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::kPointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

constexpr VkPolygonMode toVk(FillMode m) noexcept { return m == FillMode::kFill ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE; }

constexpr VkCullModeFlags toVk(CullMode m) noexcept
{
    switch (m)
    {
        case CullMode::kNone: return VK_CULL_MODE_NONE;
        case CullMode::kFront: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::kBack: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

// Le RHI garde y = +1 en haut : le viewport Vulkan est retourné (hauteur négative, voir README.md), ce qui conserve le sens
// d'enroulement de OpenGL : `kCounterClockwise` du RHI reste `COUNTER_CLOCKWISE`.
constexpr VkFrontFace toVk(FrontFace f) noexcept
{
    return f == FrontFace::kCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
}

constexpr VkBlendFactor toVk(BlendFactor f) noexcept
{
    switch (f)
    {
        case BlendFactor::kZero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::kOne: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::kSrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::kOneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::kDstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::kOneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::kSrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::kOneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::kDstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::kOneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

constexpr VkBlendOp toVk(BlendOp op) noexcept
{
    switch (op)
    {
        case BlendOp::kAdd: return VK_BLEND_OP_ADD;
        case BlendOp::kSubtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::kReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::kMin: return VK_BLEND_OP_MIN;
        case BlendOp::kMax: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

constexpr VkColorComponentFlags toVk(ColorWriteMask m) noexcept { return static_cast<VkColorComponentFlags>(m); } // mêmes bits R, G, B, A

constexpr VkFormat toVk(VertexFormat f) noexcept
{
    switch (f)
    {
        case VertexFormat::kFloat1: return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::kFloat2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::kFloat3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::kFloat4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::kUint1: return VK_FORMAT_R32_UINT;
        case VertexFormat::kUint2: return VK_FORMAT_R32G32_UINT;
        case VertexFormat::kUint3: return VK_FORMAT_R32G32B32_UINT;
        case VertexFormat::kUint4: return VK_FORMAT_R32G32B32A32_UINT;
        case VertexFormat::kUnorm8x4: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R32G32B32_SFLOAT;
}

constexpr VkIndexType toVk(IndexType t) noexcept { return t == IndexType::kUint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32; }

constexpr VkShaderStageFlagBits toVk(ShaderStage s) noexcept
{
    switch (s)
    {
        case ShaderStage::kVertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::kFragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::kCompute: return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

constexpr VkAttachmentLoadOp toVk(LoadOp op) noexcept
{
    switch (op)
    {
        case LoadOp::kLoad: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::kClear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::kDontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

constexpr VkAttachmentStoreOp toVk(StoreOp op) noexcept { return op == StoreOp::kStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE; }

// Descripteurs : tout tampon est « dynamique » (décalage fourni à chaque liaison ; 0 quand l'entrée n'est pas dynamique), de sorte
// que la disposition d'un groupe ne dépend que du shader et jamais de la façon dont le groupe de liaison a été rempli (README.md).
constexpr VkDescriptorType toVk(BindingType t) noexcept
{
    switch (t)
    {
        case BindingType::kUniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case BindingType::kStorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case BindingType::kSampledTexture: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case BindingType::kStorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}

constexpr bool isBufferDescriptor(BindingType t) noexcept { return t == BindingType::kUniformBuffer || t == BindingType::kStorageBuffer; }

constexpr VkImageType toVkImageType(TextureDimension d) noexcept { return d == TextureDimension::k3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D; }

constexpr VkImageViewType toVkViewType(TextureDimension d) noexcept
{
    switch (d)
    {
        case TextureDimension::k2D: return VK_IMAGE_VIEW_TYPE_2D;
        case TextureDimension::k2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureDimension::kCube: return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureDimension::k3D: return VK_IMAGE_VIEW_TYPE_3D;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

// Format RHI -> Vulkan. `d24s8Replacement` : format utilisé quand D24_UNORM_S8_UINT n'est pas pris en charge comme attachement.
constexpr VkFormat toVk(Format f, VkFormat d24s8Replacement = VK_FORMAT_D24_UNORM_S8_UINT) noexcept
{
    switch (f)
    {
        case Format::kUndefined: return VK_FORMAT_UNDEFINED;
        case Format::kR8Unorm: return VK_FORMAT_R8_UNORM;
        case Format::kRG8Unorm: return VK_FORMAT_R8G8_UNORM;
        case Format::kRGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::kBGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::kRGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::kR16Float: return VK_FORMAT_R16_SFLOAT;
        case Format::kRG16Float: return VK_FORMAT_R16G16_SFLOAT;
        case Format::kRGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::kR32Float: return VK_FORMAT_R32_SFLOAT;
        case Format::kRG32Float: return VK_FORMAT_R32G32_SFLOAT;
        case Format::kRGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::kR32Uint: return VK_FORMAT_R32_UINT;
        case Format::kD32Float: return VK_FORMAT_D32_SFLOAT;
        case Format::kD24UnormS8Uint: return d24s8Replacement;
    }
    return VK_FORMAT_UNDEFINED;
}

inline VkImageAspectFlags aspectOf(Format f) noexcept
{
    switch (f)
    {
        case Format::kD32Float: return VK_IMAGE_ASPECT_DEPTH_BIT;
        case Format::kD24UnormS8Uint: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

} // namespace orosmo::rhi::vk
