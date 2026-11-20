// Tables de traduction RHI -> OpenGL (formats, états, énumérations).
#pragma once

#include "orosmo/rhi/types.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <bit>
#include <cstdint>

namespace orosmo::rhi::gl
{

struct GlFormat
{
    GLenum internal = 0; // format interne
    GLenum format = 0;   // format de transfert
    GLenum type = 0;     // type de transfert
    bool imageCompatible = false; // utilisable comme image de stockage (glBindImageTexture)
};

inline GlFormat glFormatOf(Format format) noexcept
{
    switch (format)
    {
        case Format::kR8Unorm: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, true};
        case Format::kRG8Unorm: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, true};
        case Format::kRGBA8Unorm: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true};
        // BGRA8 : stocké RGBA8 ; l'ordre des octets est converti au transfert, la valeur des texels est conservée.
        case Format::kBGRA8Unorm: return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, false};
        case Format::kRGBA8Srgb: return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false};
        case Format::kR16Float: return {GL_R16F, GL_RED, GL_HALF_FLOAT, true};
        case Format::kRG16Float: return {GL_RG16F, GL_RG, GL_HALF_FLOAT, true};
        case Format::kRGBA16Float: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, true};
        case Format::kR32Float: return {GL_R32F, GL_RED, GL_FLOAT, true};
        case Format::kRG32Float: return {GL_RG32F, GL_RG, GL_FLOAT, true};
        case Format::kRGBA32Float: return {GL_RGBA32F, GL_RGBA, GL_FLOAT, true};
        case Format::kR32Uint: return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, true};
        case Format::kD32Float: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, false};
        case Format::kD24UnormS8Uint: return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false};
        case Format::kUndefined: break;
    }
    return {};
}

inline GLenum glTarget(TextureDimension dim) noexcept
{
    switch (dim)
    {
        case TextureDimension::k2D: return GL_TEXTURE_2D;
        case TextureDimension::k2DArray: return GL_TEXTURE_2D_ARRAY;
        case TextureDimension::kCube: return GL_TEXTURE_CUBE_MAP;
        case TextureDimension::k3D: return GL_TEXTURE_3D;
    }
    return GL_TEXTURE_2D;
}

inline GLenum glFilter(Filter f) noexcept { return f == Filter::kNearest ? GL_NEAREST : GL_LINEAR; }

inline GLenum glMinFilter(Filter min, std::optional<Filter> mip) noexcept
{
    if (!mip)
    {
        return glFilter(min);
    }
    if (min == Filter::kNearest)
    {
        return *mip == Filter::kNearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
    }
    return *mip == Filter::kNearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
}

inline GLenum glAddress(AddressMode m) noexcept
{
    switch (m)
    {
        case AddressMode::kRepeat: return GL_REPEAT;
        case AddressMode::kMirroredRepeat: return GL_MIRRORED_REPEAT;
        case AddressMode::kClampToEdge: return GL_CLAMP_TO_EDGE;
        case AddressMode::kClampToBorder: return GL_CLAMP_TO_BORDER;
    }
    return GL_REPEAT;
}

inline GLenum glCompare(CompareOp op) noexcept
{
    switch (op)
    {
        case CompareOp::kNever: return GL_NEVER;
        case CompareOp::kLess: return GL_LESS;
        case CompareOp::kEqual: return GL_EQUAL;
        case CompareOp::kLessEqual: return GL_LEQUAL;
        case CompareOp::kGreater: return GL_GREATER;
        case CompareOp::kNotEqual: return GL_NOTEQUAL;
        case CompareOp::kGreaterEqual: return GL_GEQUAL;
        case CompareOp::kAlways: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

inline GLenum glBlendFactor(BlendFactor f) noexcept
{
    switch (f)
    {
        case BlendFactor::kZero: return GL_ZERO;
        case BlendFactor::kOne: return GL_ONE;
        case BlendFactor::kSrcColor: return GL_SRC_COLOR;
        case BlendFactor::kOneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::kDstColor: return GL_DST_COLOR;
        case BlendFactor::kOneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::kSrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::kOneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::kDstAlpha: return GL_DST_ALPHA;
        case BlendFactor::kOneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

inline GLenum glBlendOp(BlendOp op) noexcept
{
    switch (op)
    {
        case BlendOp::kAdd: return GL_FUNC_ADD;
        case BlendOp::kSubtract: return GL_FUNC_SUBTRACT;
        case BlendOp::kReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::kMin: return GL_MIN;
        case BlendOp::kMax: return GL_MAX;
    }
    return GL_FUNC_ADD;
}

inline GLenum glTopology(PrimitiveTopology t) noexcept
{
    switch (t)
    {
        case PrimitiveTopology::kTriangleList: return GL_TRIANGLES;
        case PrimitiveTopology::kTriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::kLineList: return GL_LINES;
        case PrimitiveTopology::kLineStrip: return GL_LINE_STRIP;
        case PrimitiveTopology::kPointList: return GL_POINTS;
    }
    return GL_TRIANGLES;
}

inline GLenum glShaderStage(ShaderStage s) noexcept
{
    switch (s)
    {
        case ShaderStage::kVertex: return GL_VERTEX_SHADER;
        case ShaderStage::kFragment: return GL_FRAGMENT_SHADER;
        case ShaderStage::kCompute: return GL_COMPUTE_SHADER;
    }
    return GL_VERTEX_SHADER;
}

struct GlVertexFormat
{
    GLint components;
    GLenum type;
    bool normalized;
    bool integer;
};

inline GlVertexFormat glVertexFormat(VertexFormat f) noexcept
{
    switch (f)
    {
        case VertexFormat::kFloat1: return {1, GL_FLOAT, false, false};
        case VertexFormat::kFloat2: return {2, GL_FLOAT, false, false};
        case VertexFormat::kFloat3: return {3, GL_FLOAT, false, false};
        case VertexFormat::kFloat4: return {4, GL_FLOAT, false, false};
        case VertexFormat::kUint1: return {1, GL_UNSIGNED_INT, false, true};
        case VertexFormat::kUint2: return {2, GL_UNSIGNED_INT, false, true};
        case VertexFormat::kUint3: return {3, GL_UNSIGNED_INT, false, true};
        case VertexFormat::kUint4: return {4, GL_UNSIGNED_INT, false, true};
        case VertexFormat::kUnorm8x4: return {4, GL_UNSIGNED_BYTE, true, false};
    }
    return {4, GL_FLOAT, false, false};
}

// Nombre de niveaux d'une chaîne complète de mips.
inline std::uint32_t fullMipCount(std::uint32_t w, std::uint32_t h, std::uint32_t d) noexcept
{
    return static_cast<std::uint32_t>(std::bit_width(std::max({w, h, d, 1u})));
}

inline std::uint32_t mipDim(std::uint32_t base, std::uint32_t mip) noexcept { return std::max(1u, base >> mip); }

} // namespace orosmo::rhi::gl
