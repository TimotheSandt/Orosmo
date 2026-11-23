// RHI-01, RHI-05, RHI-15, RHI-16 : types de l'interface graphique. Aucun type d'API (GLuint, VkBuffer...) ici.
#pragma once

#include "orosmo/rhi/handle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace orosmo::rhi
{

enum class Backend : std::uint8_t
{
    kVulkan,
    kOpenGL,
    kMetal,
};

const char *toString(Backend backend) noexcept;

// ── Masques de bits ─────────────────────────────────────────────────────────────────────────────────────────────
template <typename E> concept FlagEnum = std::is_enum_v<E> && requires { E::kNone; };

template <FlagEnum E> constexpr E operator|(E a, E b) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <FlagEnum E> constexpr E operator&(E a, E b) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
template <FlagEnum E> constexpr bool hasFlag(E value, E flag) noexcept { return (value & flag) == flag && flag != E::kNone; }

// ── Géométrie ───────────────────────────────────────────────────────────────────────────────────────────────────
struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    friend constexpr bool operator==(const Extent2D &, const Extent2D &) noexcept = default;
};

struct Extent3D
{
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
    friend constexpr bool operator==(const Extent3D &, const Extent3D &) noexcept = default;
};

struct Offset3D
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

// Origine en haut à gauche, Y vers le bas : y est la distance depuis le haut de la cible (repère du RHI, A26 ; voir
// src/rhi/README.md, section « Conventions »). Pour une cible de 100 pixels de haut, la bande haute de 20 pixels est y = 0.
struct Rect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Viewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

// ── Formats ─────────────────────────────────────────────────────────────────────────────────────────────────────
// Formats portables garantis (RHI-16 : RG16F, RGBA16F, R32F, R32UI). Pas de RGB8 : non portable sur Vulkan/Metal.
enum class Format : std::uint8_t
{
    kUndefined = 0,
    kR8Unorm,
    kRG8Unorm,
    kRGBA8Unorm,
    kBGRA8Unorm,
    kRGBA8Srgb,
    kR16Float,
    kRG16Float,
    kRGBA16Float,
    kR32Float,
    kRG32Float,
    kRGBA32Float,
    kR32Uint,
    kD32Float,
    kD24UnormS8Uint,
};

enum class FormatKind : std::uint8_t
{
    kUnorm,
    kSrgb,
    kFloat,
    kUint,
    kDepth,
    kDepthStencil,
};

struct FormatInfo
{
    std::uint32_t bytesPerTexel = 0;
    std::uint32_t channels = 0;
    FormatKind kind = FormatKind::kUnorm;
    bool isDepth() const noexcept { return kind == FormatKind::kDepth || kind == FormatKind::kDepthStencil; }
};

FormatInfo formatInfo(Format format) noexcept;
const char *toString(Format format) noexcept;

// ── Buffers ─────────────────────────────────────────────────────────────────────────────────────────────────────
enum class BufferUsage : std::uint16_t
{
    kNone = 0,
    kVertex = 1 << 0,
    kIndex = 1 << 1,
    kUniform = 1 << 2,
    kStorage = 1 << 3,
    kIndirect = 1 << 4,
    kCopySrc = 1 << 5,
    kCopyDst = 1 << 6,
};

// docs/03 §3 règle 6.
//
// Contrat de `kUpload` (Device::updateBuffer) : le backend peut écrire DIRECTEMENT dans la mémoire du tampon, au moment de l'appel,
// sans copie ordonnée avec les soumissions (Vulkan : mémoire mappée en permanence ; OpenGL : glBufferSubData, qui reste ordonné).
// L'appelant ne réécrit donc jamais une plage que le GPU peut encore lire : travail soumis mais pas forcément fini (images en vol),
// ou encodeur déjà enregistré qui la lit. Usage type : un anneau par image en vol (une plage par image, réécrite seulement quand
// l'image qui l'a lue est finie : `beginFrame` garantit au plus 2 images d'avance). Pour réécrire une plage en cours d'usage,
// utiliser `kGpuOnly` (copie ordonnée).
enum class MemoryKind : std::uint8_t
{
    kGpuOnly,  // mis à jour par copie ou updateBuffer (copie ordonnée avec les soumissions, comme glBufferSubData)
    kUpload,   // écrit par le CPU, lu par le GPU ; updateBuffer écrit directement (contrat ci-dessus)
    kReadback, // écrit par le GPU, lu par le CPU
};

struct BufferDesc
{
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::kNone;
    MemoryKind memory = MemoryKind::kGpuOnly;
    std::string debugName{}; // docs/03 §3 règle 8 : visible dans RenderDoc
};

// ── Textures et échantillonneurs ────────────────────────────────────────────────────────────────────────────────
enum class TextureDimension : std::uint8_t
{
    k2D,
    k2DArray,
    kCube, // 6 couches
    k3D,
};

enum class TextureUsage : std::uint8_t
{
    kNone = 0,
    kSampled = 1 << 0,
    kStorage = 1 << 1,      // image de stockage (compute)
    kRenderTarget = 1 << 2, // attachement couleur ou profondeur
    kCopySrc = 1 << 3,
    kCopyDst = 1 << 4,
};

struct TextureDesc
{
    TextureDimension dimension = TextureDimension::k2D;
    Format format = Format::kRGBA8Unorm;
    Extent3D size{1, 1, 1}; // depth = profondeur pour k3D, ignoré sinon
    std::uint32_t mipLevels = 1; // 0 = chaîne complète
    std::uint32_t layers = 1;    // k2DArray ; 6 pour kCube (forcé)
    TextureUsage usage = TextureUsage::kSampled;
    std::string debugName{};
};

enum class Filter : std::uint8_t
{
    kNearest,
    kLinear,
};

enum class AddressMode : std::uint8_t
{
    kRepeat,
    kMirroredRepeat,
    kClampToEdge,
    kClampToBorder,
};

enum class CompareOp : std::uint8_t
{
    kNever,
    kLess,
    kEqual,
    kLessEqual,
    kGreater,
    kNotEqual,
    kGreaterEqual,
    kAlways,
};

struct SamplerDesc
{
    Filter minFilter = Filter::kLinear;
    Filter magFilter = Filter::kLinear;
    std::optional<Filter> mipFilter = Filter::kLinear; // std::nullopt : pas de mips
    AddressMode addressU = AddressMode::kRepeat;
    AddressMode addressV = AddressMode::kRepeat;
    AddressMode addressW = AddressMode::kRepeat;
    float maxAnisotropy = 1.0f; // > 1 exige Capabilities::anisotropy
    std::optional<CompareOp> compare{}; // échantillonneur de comparaison (ombres)
    std::array<float, 4> borderColor{0.0f, 0.0f, 0.0f, 0.0f};
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    std::string debugName{};
};

// ── Shaders et pipelines ────────────────────────────────────────────────────────────────────────────────────────
enum class ShaderStage : std::uint8_t
{
    kVertex,
    kFragment,
    kCompute,
};

enum class ShaderLanguage : std::uint8_t
{
    kGlsl,  // source GLSL 430 (OpenGL) ; sera produit par SPIRV-Cross à partir du GLSL 450 commun (RHI-12, lot B)
    kSpirv, // Vulkan ; OpenGL seulement avec GL_ARB_gl_spirv (Capabilities::spirvShaders)
};

struct ShaderDesc
{
    ShaderStage stage = ShaderStage::kVertex;
    ShaderLanguage language = ShaderLanguage::kGlsl;
    std::span<const std::byte> code{}; // copié par createShader
    std::string entryPoint = "main";
    std::string debugName{};
};

enum class VertexFormat : std::uint8_t
{
    kFloat1,
    kFloat2,
    kFloat3,
    kFloat4,
    kUint1,
    kUint2,
    kUint3,
    kUint4,
    kUnorm8x4, // 4 octets normalisés en [0,1]
};

enum class VertexStepRate : std::uint8_t
{
    kVertex,
    kInstance,
};

struct VertexBufferLayout
{
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
    VertexStepRate stepRate = VertexStepRate::kVertex;
};

struct VertexAttribute
{
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    VertexFormat format = VertexFormat::kFloat3;
    std::uint32_t offset = 0;
};

enum class PrimitiveTopology : std::uint8_t
{
    kTriangleList,
    kTriangleStrip,
    kLineList,
    kLineStrip,
    kPointList,
};

enum class FillMode : std::uint8_t
{
    kFill,
    kLine, // exige Capabilities::wireframe
};

enum class CullMode : std::uint8_t
{
    kNone,
    kFront,
    kBack,
};

enum class FrontFace : std::uint8_t
{
    kCounterClockwise,
    kClockwise,
};

enum class BlendFactor : std::uint8_t
{
    kZero,
    kOne,
    kSrcColor,
    kOneMinusSrcColor,
    kDstColor,
    kOneMinusDstColor,
    kSrcAlpha,
    kOneMinusSrcAlpha,
    kDstAlpha,
    kOneMinusDstAlpha,
};

enum class BlendOp : std::uint8_t
{
    kAdd,
    kSubtract,
    kReverseSubtract,
    kMin,
    kMax,
};

struct BlendState
{
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::kOne;
    BlendFactor dstColor = BlendFactor::kZero;
    BlendOp colorOp = BlendOp::kAdd;
    BlendFactor srcAlpha = BlendFactor::kOne;
    BlendFactor dstAlpha = BlendFactor::kZero;
    BlendOp alphaOp = BlendOp::kAdd;
};

enum class ColorWriteMask : std::uint8_t
{
    kNone = 0,
    kR = 1,
    kG = 2,
    kB = 4,
    kA = 8,
    kAll = 15,
};

struct ColorTargetState
{
    Format format = Format::kRGBA8Unorm;
    BlendState blend{};
    ColorWriteMask writeMask = ColorWriteMask::kAll;
};

struct RasterState
{
    FillMode fill = FillMode::kFill;
    CullMode cull = CullMode::kBack;
    FrontFace frontFace = FrontFace::kCounterClockwise;
};

struct DepthState
{
    bool test = false;
    bool write = false;
    CompareOp compare = CompareOp::kLess;
};

struct GraphicsPipelineDesc
{
    ShaderHandle vertex{};
    ShaderHandle fragment{};
    std::vector<VertexBufferLayout> vertexBuffers{};
    std::vector<VertexAttribute> vertexAttributes{};
    PrimitiveTopology topology = PrimitiveTopology::kTriangleList;
    RasterState raster{};
    DepthState depth{};
    // Une entrée par attachement couleur de la passe (MRT, RHI-15) ; chaque cible a son format, son mélange, son masque.
    std::vector<ColorTargetState> colorTargets{};
    Format depthFormat = Format::kUndefined; // kUndefined : pas de profondeur
    std::string debugName{};
};

struct ComputePipelineDesc
{
    ShaderHandle compute{};
    std::string debugName{};
};

// ── Liaison des ressources (docs/03 §4) ─────────────────────────────────────────────────────────────────────────
// Trois groupes fixes. Un backend OpenGL convertit (groupe, binding) en emplacement plat : voir kBindingsPerGroup.
inline constexpr std::uint32_t kGroupCount = 3;
inline constexpr std::uint32_t kBindingsPerGroup = 8;
// Emplacement plat de l'UBO qui émule les push constants sur OpenGL (juste après les liaisons des trois groupes).
inline constexpr std::uint32_t kPushConstantBinding = kGroupCount * kBindingsPerGroup;

enum class BindingType : std::uint8_t
{
    kUniformBuffer,
    kStorageBuffer,
    kSampledTexture, // texture + échantillonneur (liés ensemble)
    kStorageImage,
};

struct BindGroupEntry
{
    std::uint32_t binding = 0; // < kBindingsPerGroup
    BindingType type = BindingType::kUniformBuffer;
    // tampons
    BufferHandle buffer{};
    std::uint64_t offset = 0;
    std::uint64_t size = 0; // 0 : jusqu'à la fin du tampon
    bool dynamicOffset = false; // consomme une valeur de `dynamicOffsets` dans setBindGroup, dans l'ordre des entrées
    // textures
    TextureHandle texture{};
    SamplerHandle sampler{}; // kSampledTexture
    std::uint32_t mipLevel = 0; // kStorageImage ; tableaux, cubes et 3D sont liés en entier (toutes les couches)
};

struct BindGroupDesc
{
    std::vector<BindGroupEntry> entries{};
    std::string debugName{};
};

// ── Passes de rendu (RHI-08, RHI-15) ────────────────────────────────────────────────────────────────────────────
enum class LoadOp : std::uint8_t
{
    kLoad,
    kClear,
    kDontCare,
};

enum class StoreOp : std::uint8_t
{
    kStore,
    kDontCare,
};

struct ColorAttachment
{
    TextureHandle texture{};
    std::uint32_t mipLevel = 0;
    std::uint32_t layer = 0;
    LoadOp load = LoadOp::kClear;
    StoreOp store = StoreOp::kStore;
    std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f}; // formats entiers : la valeur est convertie en entier
};

struct DepthAttachment
{
    TextureHandle texture{};
    LoadOp load = LoadOp::kClear;
    StoreOp store = StoreOp::kStore;
    float clearDepth = 1.0f;
    std::uint32_t clearStencil = 0;
};

struct RenderPassDesc
{
    std::vector<ColorAttachment> colors{}; // jusqu'à Capabilities::maxColorAttachments
    std::optional<DepthAttachment> depth{};
    std::string debugName{};
};

enum class IndexType : std::uint8_t
{
    kUint16,
    kUint32,
};

// Arguments des dessins indirects (même disposition que Vulkan et OpenGL).
struct DrawIndirectCommand
{
    std::uint32_t vertexCount;
    std::uint32_t instanceCount;
    std::uint32_t firstVertex;
    std::uint32_t firstInstance;
};

struct DrawIndexedIndirectCommand
{
    std::uint32_t indexCount;
    std::uint32_t instanceCount;
    std::uint32_t firstIndex;
    std::int32_t vertexOffset;
    std::uint32_t firstInstance;
};

struct DispatchIndirectCommand
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
};

struct BufferCopy
{
    std::uint64_t srcOffset = 0;
    std::uint64_t dstOffset = 0;
    std::uint64_t size = 0;
};

// Région d'une texture ; `bufferOffset` et `bufferRowLength` ne servent qu'aux copies tampon <-> texture.
struct TextureCopy
{
    std::uint32_t mipLevel = 0;
    std::uint32_t layer = 0; // couche (tableau, cube) ; ignoré pour k2D et k3D
    Offset3D offset{};
    Extent3D size{0, 0, 0}; // {0,0,0} : tout le niveau
    std::uint64_t bufferOffset = 0;
    std::uint32_t bufferRowLength = 0; // en texels ; 0 : lignes serrées
};

} // namespace orosmo::rhi
