// Backend OpenGL 4.3+ du RHI (privé : seul endroit avec glad/GLFW pour le rendu). Voir src/rhi/README.md.
#pragma once

#include "engine/rhi/device.hpp"

#include <glad/glad.h>

#include <array>
#include <string>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace engine::rhi::gl
{

// Emplacement plat des liaisons : (groupe, binding) -> groupe * kBindingsPerGroup + binding (docs/03 §4, RHI-07).
// Les shaders OpenGL écrits à la main utilisent ces numéros dans `layout(binding = N)` ; le pipeline de shaders (lot B)
// convertit `set`/`binding` du GLSL 450 commun vers cet emplacement.
inline constexpr std::uint32_t kFramesInFlight = 2;
// Unité de texture réservée aux opérations du périphérique (création, envoi, mips) : juste après les emplacements du modèle
// de liaison, elle n'écrase jamais une texture liée par `setBindGroup` (unités 0 à 23).
inline constexpr GLuint kScratchTextureUnit = kGroupCount * kBindingsPerGroup;
inline constexpr std::uint32_t kMaxAttachments = 8;

constexpr std::uint32_t flatBinding(std::uint32_t group, std::uint32_t binding) noexcept
{
    return group * kBindingsPerGroup + binding;
}

struct BufferRes
{
    GLuint id = 0;
    BufferDesc desc;
};

struct TextureRes
{
    GLuint id = 0;
    GLenum target = 0;
    TextureDesc desc;
    std::uint32_t levels = 1;
    std::uint32_t layers = 1; // couches réelles (6 pour un cube)
    bool defaultFramebuffer = false; // backbuffer d'une fenêtre : framebuffer 0, non échantillonnable
};

struct SamplerRes
{
    GLuint id = 0;
};

struct ShaderRes
{
    GLuint id = 0;
    ShaderStage stage = ShaderStage::kVertex;
};

struct PipelineRes
{
    bool compute = false;
    bool usesStorage = false; // le programme déclare un bloc de stockage ou une image (lu à l'édition de liens)
    GLuint program = 0;
    GLuint vao = 0;
    GLenum topology = GL_TRIANGLES;
    RasterState raster{};
    DepthState depth{};
    std::vector<ColorTargetState> colorTargets;
    Format depthFormat = Format::kUndefined;
    std::array<std::uint32_t, 16> strides{}; // par binding de sommets
    std::array<bool, 16> bindingUsed{};
};

struct BindGroupRes
{
    std::vector<BindGroupEntry> entries;
};

struct QueryPoolRes
{
    std::vector<GLuint> ids;
    std::vector<bool> written;
    std::string debugName; // posé sur chaque requête à sa première écriture (l'objet GL n'existe qu'à ce moment)
};

struct FboKey
{
    std::array<GLuint, kMaxAttachments + 1> texture{}; // le dernier : profondeur
    std::array<std::uint32_t, kMaxAttachments + 1> mip{};
    std::array<std::uint32_t, kMaxAttachments + 1> layer{};
    friend bool operator==(const FboKey &, const FboKey &) = default;
};

// Région résolue d'une texture : `size.depth` = nombre de couches (tableau, cube) ou de tranches (3D).
struct SliceRegion
{
    std::uint32_t mip = 0;
    std::uint32_t layer = 0; // première couche (tableau, cube)
    Offset3D offset{};
    Extent3D size{};
};

Result<SliceRegion> resolveRegion(const TextureRes &texture, const TextureCopy &copy);

class GlEncoder;

class GlDevice final : public Device
{
  public:
    ~GlDevice() override;

    static Result<std::unique_ptr<GlDevice>> create(const DeviceDesc &desc);

    const Capabilities &capabilities() const noexcept override { return caps_; }

    Result<BufferHandle> createBuffer(const BufferDesc &desc, std::span<const std::byte> initial) override;
    Result<TextureHandle> createTexture(const TextureDesc &desc) override;
    Result<SamplerHandle> createSampler(const SamplerDesc &desc) override;
    Result<ShaderHandle> createShader(const ShaderDesc &desc) override;
    Result<PipelineHandle> createGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
    Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc &desc) override;
    Result<BindGroupHandle> createBindGroup(const BindGroupDesc &desc) override;
    Result<QueryPoolHandle> createQueryPool(std::uint32_t count, const char *debugName) override;

    void destroy(BufferHandle handle) override;
    void destroy(TextureHandle handle) override;
    void destroy(SamplerHandle handle) override;
    void destroy(ShaderHandle handle) override;
    void destroy(PipelineHandle handle) override;
    void destroy(BindGroupHandle handle) override;
    void destroy(QueryPoolHandle handle) override;

    Result<TextureDesc> textureDesc(TextureHandle texture) const override;
    Result<> updateBuffer(BufferHandle buffer, std::uint64_t offset, std::span<const std::byte> data) override;
    Result<> updateTexture(TextureHandle texture, const TextureCopy &region, std::span<const std::byte> data) override;
    Result<std::vector<std::byte>> readBuffer(BufferHandle buffer, std::uint64_t offset, std::uint64_t size) override;
    Result<std::vector<std::byte>> readTexture(TextureHandle texture, const TextureCopy &region) override;
    Result<bool> timestampsReady(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) override;
    Result<std::vector<std::uint64_t>> readTimestamps(QueryPoolHandle pool, std::uint32_t first, std::uint32_t count) override;

    std::unique_ptr<CommandEncoder> createCommandEncoder() override;
    Result<> submit(std::unique_ptr<CommandEncoder> encoder) override;

    Result<TextureHandle> beginFrame() override;
    Result<> present() override;
    Result<> resize(Extent2D size) override;
    void setVSync(bool enabled) override;
    void waitIdle() override;

    std::uint64_t validationErrorCount() const noexcept override { return validationErrors_.load(); }
    DeviceStats stats() const noexcept override;

    // ── Usage interne (GlEncoder) ────────────────────────────────────────────────────────────────────────────────
    BufferRes *get(BufferHandle h) { return buffers_.get(h); }
    TextureRes *get(TextureHandle h) { return textures_.get(h); }
    SamplerRes *get(SamplerHandle h) { return samplers_.get(h); }
    PipelineRes *get(PipelineHandle h) { return pipelines_.get(h); }
    BindGroupRes *get(BindGroupHandle h) { return bindGroups_.get(h); }
    QueryPoolRes *get(QueryPoolHandle h) { return queryPools_.get(h); }

    // Transferts de texels. `source`/`destination` : adresse mémoire, ou décalage dans le tampon PIXEL_UNPACK/PACK lié.
    void uploadRegion(const TextureRes &texture, const SliceRegion &region, std::uint32_t rowLength, std::uintptr_t source);
    Result<> readRegion(const TextureRes &texture, const SliceRegion &region, std::uint32_t rowLength, std::uintptr_t destination);

    GLuint framebufferFor(const FboKey &key, std::uint32_t colorCount, const std::array<const TextureRes *, kMaxAttachments + 1> &attachments);
    GLuint scratchFramebuffer() const noexcept { return scratchFbo_; }
    GLuint pushConstantBuffer() const noexcept { return pushBuffer_; }
    GLuint emptyVertexArray() const noexcept { return emptyVao_; }
    // Lie une texture sur l'unité réservée (kScratchTextureUnit) pour la créer, l'envoyer ou en générer les mips.
    static void bindScratchTexture(GLenum target, GLuint id);
    static void labelQuery(const QueryPoolRes &pool, std::uint32_t index);

    // Tests du backend : liaisons du VAO d'un pipeline (tampon de sommets 0, tampon d'indices), lues dans l'état GL.
    struct VertexArrayBindings
    {
        GLuint vertexBuffer0 = 0;
        GLuint indexBuffer = 0;
    };
    Result<VertexArrayBindings> inspectVertexArray(PipelineHandle pipeline);
    void onValidationMessage(bool isError) noexcept;

  private:
    GlDevice() = default;

    Result<> init(const DeviceDesc &desc);
    void queueRelease(std::function<void()> release);
    void flushReleases(bool all);
    void eraseFramebuffersUsing(GLuint textureId);
    Result<TextureHandle> createTextureImpl(const TextureDesc &desc);
    void releaseTexture(const TextureRes &texture);

    Result<> refreshWindowBackbuffer();

    GLFWwindow *window_ = nullptr;
    bool ownsWindow_ = true;  // false : fenêtre adoptée, détruite par son créateur
    bool windowed_ = false;
    bool glReady_ = false;
    bool debugCallbackInstalled_ = false;
    Capabilities caps_;
    Extent2D backbufferSize_{1, 1};
    TextureHandle backbuffer_;
    std::uint64_t frame_ = 0;
    std::atomic<std::uint64_t> validationErrors_{0};

    HandlePool<BufferTag, BufferRes> buffers_;
    HandlePool<TextureTag, TextureRes> textures_;
    HandlePool<SamplerTag, SamplerRes> samplers_;
    HandlePool<ShaderTag, ShaderRes> shaders_;
    HandlePool<PipelineTag, PipelineRes> pipelines_;
    HandlePool<BindGroupTag, BindGroupRes> bindGroups_;
    HandlePool<QueryPoolTag, QueryPoolRes> queryPools_;

    struct Pending
    {
        std::uint64_t frame;
        std::function<void()> release;
    };
    std::vector<Pending> pending_;
    // Une clôture par image présentée : une ressource détruite pendant l'image N n'est libérée qu'une fois les
    // kFramesInFlight images suivantes présentées ET la clôture de l'image N franchie par le GPU (docs/03 §3, règle 2).
    struct FrameFence
    {
        std::uint64_t frame;
        GLsync sync;
    };
    std::vector<FrameFence> fences_;
    std::vector<std::pair<FboKey, GLuint>> framebuffers_;
    GLuint scratchFbo_ = 0;
    GLuint pushBuffer_ = 0;
    GLuint emptyVao_ = 0;
    GLint maxImageUnits_ = 0;

    friend class GlEncoder;
};

Result<std::unique_ptr<Device>> createGlDevice(const DeviceDesc &desc);
void applyWindowHints(const DeviceDesc &desc);

} // namespace engine::rhi::gl
