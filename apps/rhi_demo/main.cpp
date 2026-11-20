// Démo M2 (lot A) : le RHI et son backend OpenGL, hors écran.
//   1. un shader de calcul remplit une image de stockage 256x256 (RHI-09, RHI-16) ;
//   2. une passe de rendu dessine cette image en plein écran (groupe de liaison, échantillonneur) puis trois triangles instanciés
//      mélangés (attributs par instance, mélange alpha), le tout dans une cible RGBA8 et une cible R32UI (MRT, RHI-15) ;
//   3. les durées GPU viennent de requêtes d'horodatage (RHI-11) ; l'image est écrite en PNG ; le nombre de messages d'erreur de
//      la couche de validation doit être 0.
//
//   make rhi-demo                       construit et lance (écrit rhi-demo.png)
//   bin/rhi_demo --out /chemin/image.png
#include <engine/rhi/device.hpp>

#include <engine/renderer/image.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

using namespace engine::rhi;

namespace
{

constexpr std::uint32_t kSize = 512;

constexpr const char *kFullscreenVs = R"(#version 430 core
layout(location = 0) out vec2 uv;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";

constexpr const char *kTextureFs = R"(#version 430 core
layout(binding = 8) uniform sampler2D image;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(location = 1) out uint objectId;
void main() { color = texture(image, uv); objectId = 0u; })";

constexpr const char *kTriangleVs = R"(#version 430 core
layout(location = 0) in vec2 corner;        // par sommet
layout(location = 1) in vec3 offset;        // par instance : xy = décalage, z = taille
layout(location = 2) in vec4 tint;          // par instance
layout(location = 0) out vec4 vTint;
void main() { gl_Position = vec4(offset.xy + corner * offset.z, 0.0, 1.0); vTint = tint; })";

constexpr const char *kTriangleFs = R"(#version 430 core
layout(location = 0) in vec4 vTint;
layout(location = 0) out vec4 color;
layout(location = 1) out uint objectId;
void main() { color = vTint; objectId = 7u; })";

constexpr const char *kComputeCs = R"(#version 430 core
layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba8, binding = 0) uniform image2D target;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(p) + 0.5) / 256.0;
    float rings = 0.5 + 0.5 * sin(40.0 * length(uv - 0.5));
    imageStore(target, p, vec4(uv.x * rings, uv.y * rings, 0.35 + 0.3 * rings, 1.0));
})";

template <typename T> std::span<const std::byte> bytesOf(const std::vector<T> &v) { return std::as_bytes(std::span(v)); }

int fail(const char *what, const engine::core::Error &error)
{
    std::fprintf(stderr, "rhi_demo: %s: %s\n", what, error.describe().c_str());
    return 1;
}

#define TRY(var, expr, what)                  \
    auto var = (expr);                        \
    if (!var)                                 \
    {                                         \
        return fail(what, var.error());       \
    }

} // namespace

int main(int argc, char **argv)
{
    std::string outPath = "rhi-demo.png";
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--out")
        {
            outPath = argv[i + 1];
        }
    }

    DeviceDesc desc;
    desc.surface.size = {kSize, kSize};
    TRY(created, createDevice(desc), "createDevice");
    Device &device = **created;
    const Capabilities &caps = device.capabilities();
    std::printf("RHI %s | %s | %s\n", toString(caps.backend), caps.deviceName.c_str(), caps.driverVersion.c_str());
    std::printf("  texture max %u, MRT %u, compute groupe max %u x %u x %u (%u invocations), alignement UBO %llu\n", caps.maxTextureSize,
                caps.maxColorAttachments, caps.maxComputeWorkGroupSize[0], caps.maxComputeWorkGroupSize[1], caps.maxComputeWorkGroupSize[2],
                caps.maxComputeInvocations, static_cast<unsigned long long>(caps.minUniformAlignment));

    auto shader = [&](ShaderStage stage, const char *source, const char *name) -> Result<ShaderHandle>
    {
        ShaderDesc d;
        d.stage = stage;
        d.code = std::as_bytes(std::span(source, std::strlen(source)));
        d.debugName = name;
        return device.createShader(d);
    };

    // ── 1. image de stockage remplie par un shader de calcul ──────────────────────────────────────────────────────
    TextureDesc imageDesc;
    imageDesc.size = {256, 256, 1};
    imageDesc.usage = TextureUsage::kStorage | TextureUsage::kSampled;
    imageDesc.debugName = "demo.procedural";
    TRY(image, device.createTexture(imageDesc), "createTexture(image)");
    TRY(cs, shader(ShaderStage::kCompute, kComputeCs, "demo.compute"), "createShader(cs)");
    TRY(computePipe, device.createComputePipeline({.compute = *cs, .debugName = "demo.compute"}), "createComputePipeline");
    BindGroupDesc storageGroup;
    storageGroup.entries = {{.binding = 0, .type = BindingType::kStorageImage, .texture = *image}};
    TRY(storageBind, device.createBindGroup(storageGroup), "createBindGroup(storage)");

    // ── 2. cibles MRT, pipelines, géométrie ────────────────────────────────────────────────────────────────────────
    TextureDesc colorDesc;
    colorDesc.size = {kSize, kSize, 1};
    colorDesc.usage = TextureUsage::kRenderTarget | TextureUsage::kCopySrc;
    colorDesc.debugName = "demo.color";
    TRY(color, device.createTexture(colorDesc), "createTexture(color)");
    colorDesc.format = Format::kR32Uint;
    colorDesc.debugName = "demo.objectId";
    TRY(ids, device.createTexture(colorDesc), "createTexture(ids)");

    TRY(sampler, device.createSampler({.minFilter = Filter::kLinear, .magFilter = Filter::kLinear, .mipFilter = std::nullopt,
                                       .addressU = AddressMode::kClampToEdge, .addressV = AddressMode::kClampToEdge}),
        "createSampler");
    BindGroupDesc sampledGroup;
    sampledGroup.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = *image, .sampler = *sampler}};
    TRY(sampledBind, device.createBindGroup(sampledGroup), "createBindGroup(sampled)");

    const std::vector<ColorTargetState> targets{ColorTargetState{.format = Format::kRGBA8Unorm},
                                                ColorTargetState{.format = Format::kR32Uint}};
    TRY(fullVs, shader(ShaderStage::kVertex, kFullscreenVs, "demo.fullscreen.vs"), "createShader(fullscreen vs)");
    TRY(texFs, shader(ShaderStage::kFragment, kTextureFs, "demo.texture.fs"), "createShader(texture fs)");
    GraphicsPipelineDesc background;
    background.vertex = *fullVs;
    background.fragment = *texFs;
    background.colorTargets = targets;
    background.raster.cull = CullMode::kNone;
    background.debugName = "demo.background";
    TRY(backgroundPipe, device.createGraphicsPipeline(background), "createGraphicsPipeline(background)");

    TRY(triVs, shader(ShaderStage::kVertex, kTriangleVs, "demo.triangle.vs"), "createShader(triangle vs)");
    TRY(triFs, shader(ShaderStage::kFragment, kTriangleFs, "demo.triangle.fs"), "createShader(triangle fs)");
    GraphicsPipelineDesc triangles;
    triangles.vertex = *triVs;
    triangles.fragment = *triFs;
    triangles.vertexBuffers = {{.binding = 0, .stride = 8, .stepRate = VertexStepRate::kVertex},
                               {.binding = 1, .stride = 28, .stepRate = VertexStepRate::kInstance}};
    triangles.vertexAttributes = {{.location = 0, .binding = 0, .format = VertexFormat::kFloat2, .offset = 0},
                                  {.location = 1, .binding = 1, .format = VertexFormat::kFloat3, .offset = 0},
                                  {.location = 2, .binding = 1, .format = VertexFormat::kFloat4, .offset = 12}};
    triangles.colorTargets = targets;
    triangles.colorTargets[0].blend = {true, BlendFactor::kSrcAlpha, BlendFactor::kOneMinusSrcAlpha, BlendOp::kAdd,
                                       BlendFactor::kOne, BlendFactor::kOneMinusSrcAlpha, BlendOp::kAdd};
    triangles.raster.cull = CullMode::kNone;
    triangles.debugName = "demo.triangles";
    TRY(trianglePipe, device.createGraphicsPipeline(triangles), "createGraphicsPipeline(triangles)");

    const std::vector<float> corners{0.0f, 1.0f, -0.866f, -0.5f, 0.866f, -0.5f};
    const std::vector<float> instances{ // décalage xy, taille, couleur rgba
        -0.45f, -0.25f, 0.35f, 1.0f, 0.2f, 0.1f, 0.85f,  //
        0.45f,  -0.25f, 0.35f, 0.1f, 0.9f, 0.3f, 0.85f,  //
        0.0f,   0.35f,  0.35f, 0.2f, 0.4f, 1.0f, 0.85f};
    BufferDesc vbDesc{.size = corners.size() * 4, .usage = BufferUsage::kVertex, .debugName = "demo.corners"};
    TRY(cornerBuffer, device.createBuffer(vbDesc, bytesOf(corners)), "createBuffer(corners)");
    BufferDesc instDesc{.size = instances.size() * 4, .usage = BufferUsage::kVertex, .debugName = "demo.instances"};
    TRY(instanceBuffer, device.createBuffer(instDesc, bytesOf(instances)), "createBuffer(instances)");
    TRY(pool, device.createQueryPool(4, "demo.timing"), "createQueryPool");

    // ── 3. une image ──────────────────────────────────────────────────────────────────────────────────────────────
    auto encoder = device.createCommandEncoder();
    encoder->beginDebugGroup("frame");
    encoder->writeTimestamp(*pool, 0);
    encoder->bindComputePipeline(*computePipe);
    encoder->setBindGroup(0, *storageBind);
    encoder->dispatch(16, 16, 1);
    encoder->writeTimestamp(*pool, 1);

    RenderPassDesc pass;
    pass.colors = {{.texture = *color, .clearColor = {0.02f, 0.02f, 0.04f, 1.0f}}, {.texture = *ids, .clearColor = {0, 0, 0, 0}}};
    pass.debugName = "demo.main";
    encoder->beginRenderPass(pass);
    encoder->setPipeline(*backgroundPipe);
    encoder->setBindGroup(1, *sampledBind);
    encoder->draw(3);
    encoder->setPipeline(*trianglePipe);
    encoder->setVertexBuffer(0, *cornerBuffer);
    encoder->setVertexBuffer(1, *instanceBuffer);
    encoder->draw(3, 3);
    encoder->endRenderPass();
    encoder->writeTimestamp(*pool, 2);
    encoder->endDebugGroup();
    if (auto submitted = device.submit(std::move(encoder)); !submitted)
    {
        return fail("submit", submitted.error());
    }
    if (auto presented = device.present(); !presented)
    {
        return fail("present", presented.error());
    }

    TRY(times, device.readTimestamps(*pool, 0, 3), "readTimestamps");
    std::printf("  GPU : calcul %.3f ms, passe de rendu %.3f ms\n", static_cast<double>((*times)[1] - (*times)[0]) * 1e-6,
                static_cast<double>((*times)[2] - (*times)[1]) * 1e-6);

    TRY(idPixels, device.readTexture(*ids, {.offset = {kSize / 2, kSize / 2 + 80, 0}, .size = {1, 1, 1}}), "readTexture(ids)");
    std::uint32_t centerId = 0;
    std::memcpy(&centerId, idPixels->data(), 4);
    std::printf("  identifiant d'objet au centre : %u (attendu 7 : un triangle)\n", centerId);

    // Une seule implémentation de l'export (ordre des lignes, erreurs) : celle du renderer.
    if (auto saved = engine::renderer::savePng(device, *color, outPath); !saved)
    {
        return fail("savePng", saved.error());
    }
    std::printf("  image : %s\n", outPath.c_str());

    const DeviceStats stats = device.stats();
    std::printf("  ressources vivantes : %u tampons, %u textures, %u pipelines ; messages de validation : %llu\n", stats.buffers, stats.textures,
                stats.pipelines, static_cast<unsigned long long>(device.validationErrorCount()));
    return device.validationErrorCount() == 0 && centerId == 7 ? 0 : 1;
}
