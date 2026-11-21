#include "orosmo/renderer/scene_renderer.hpp"

#include "orosmo/renderer/image.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{
float halton(std::uint32_t index, std::uint32_t base)
{
    float result = 0.0f;
    float denominator = 1.0f;
    while (index > 0)
    {
        denominator *= static_cast<float>(base);
        result += static_cast<float>(index % base) / denominator;
        index /= base;
    }
    return result;
}

glm::vec2 haltonJitter(std::uint32_t index, std::uint32_t sequenceLength, int renderWidth, int renderHeight)
{
    const std::uint32_t sampleIndex = (index % sequenceLength) + 1;
    return {(halton(sampleIndex, 2) - 0.5f) * 2.0f / static_cast<float>(renderWidth),
            (halton(sampleIndex, 3) - 0.5f) * 2.0f / static_cast<float>(renderHeight)};
}
} // namespace

core::Result<std::unique_ptr<SceneRenderer>> SceneRenderer::create(rhi::Device &device, ShaderLibrary &library, PipelineCache &pipelines,
                                                                   const SceneRendererConfig &config)
{
    std::unique_ptr<SceneRenderer> renderer(new SceneRenderer(device, library, pipelines));
    auto blit = FullscreenBlit::create(device, library, pipelines, config.shaderDirectory);
    if (!blit)
    {
        return std::unexpected(blit.error());
    }
    renderer->blit_ = std::move(*blit);
    auto program = library.loadGraphics(config.shaderDirectory / "mesh.vert", config.shaderDirectory / "mesh.frag");
    if (!program)
    {
        return std::unexpected(program.error());
    }
    renderer->meshProgram_ = *program;
    renderer->shaderDirectory_ = config.shaderDirectory;
    rhi::BufferDesc cb;
    cb.size = sizeof(CameraData);
    cb.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    cb.debugName = "camera";
    auto camera = GpuBuffer::create(device, cb);
    if (!camera)
    {
        return std::unexpected(camera.error());
    }
    renderer->cameraBuffer_ = std::move(*camera);
    auto lights = LightManager::create(device, 1);
    if (!lights)
    {
        return std::unexpected(lights.error());
    }
    renderer->defaultLights_ = std::move(*lights);
    if (auto r = renderer->defaultLights_.upload(); !r)
    {
        return std::unexpected(r.error());
    }
    renderer->registerUpscaleMode(std::make_unique<BilinearBlitUpscaleMode>());
    renderer->setActiveUpscaleMode("bilinear-blit");
    return renderer;
}

SceneRenderer::~SceneRenderer()
{
    if (frameGenMode_)
    {
        frameGenMode_->shutdown();
    }
    if (activeUpscaleMode_ != nullptr)
    {
        activeUpscaleMode_->shutdown();
    }
    device_.destroy(frameGroup_);
}

void SceneRenderer::setOutputResolution(int width, int height) noexcept
{
    outputWidth_ = width;
    outputHeight_ = height;
}

void SceneRenderer::setRenderScale(float scale) noexcept
{
    if (scale <= 0.0f || scale > 1.0f)
    {
        return;
    }
    renderScale_ = scale;
    upscalingEnabled_ = scale < 1.0f;
}

void SceneRenderer::setJitterSequenceLength(std::uint32_t length) noexcept
{
    jitterSequenceLength_ = length > 0 ? length : 1;
    jitterIndex_ = jitterIndex_ % jitterSequenceLength_;
}

void SceneRenderer::registerUpscaleMode(std::unique_ptr<IUpscaleMode> mode)
{
    if (mode != nullptr)
    {
        upscaleModes_.push_back(std::move(mode));
    }
}

bool SceneRenderer::setActiveUpscaleMode(const std::string &name)
{
    IUpscaleMode *candidate = nullptr;
    if (name != "disabled")
    {
        for (auto &mode : upscaleModes_)
        {
            // Premier mode de ce nom qui est supporté (deux modes peuvent porter le même nom, pour des capacités différentes).
            if (mode && mode->name() == name && mode->supports(device_.capabilities()))
            {
                candidate = mode.get();
                break;
            }
        }
        // Inconnu, non supporté ou qui ne s'initialise pas : refusé, le mode actif reste en place.
        if (candidate == nullptr)
        {
            return false;
        }
        if (candidate == activeUpscaleMode_)
        {
            return true;
        }
        if (!candidate->initialize(device_))
        {
            return false;
        }
    }
    if (activeUpscaleMode_ != nullptr && activeUpscaleMode_ != candidate)
    {
        activeUpscaleMode_->shutdown();
    }
    activeUpscaleMode_ = candidate;
    return true;
}

std::string SceneRenderer::activeUpscaleMode() const { return activeUpscaleMode_ == nullptr ? std::string("disabled") : activeUpscaleMode_->name(); }

std::vector<std::string> SceneRenderer::registeredUpscaleModes() const
{
    std::vector<std::string> names;
    for (const auto &mode : upscaleModes_)
    {
        if (mode)
        {
            names.push_back(mode->name());
        }
    }
    return names;
}

UpscaleRequirements SceneRenderer::activeUpscaleModeRequirements() const noexcept
{
    return activeUpscaleMode_ != nullptr ? activeUpscaleMode_->requirements() : UpscaleRequirements{};
}

void SceneRenderer::addPostProcessPass(std::unique_ptr<IPostProcessPass> pass)
{
    if (pass)
    {
        postProcessPasses_.push_back(std::move(pass));
    }
}

void SceneRenderer::removePostProcessPass(const std::string &name)
{
    std::erase_if(postProcessPasses_, [&](const std::unique_ptr<IPostProcessPass> &p) { return p && p->name() == name; });
}

core::Result<> SceneRenderer::setFrameGenerationMode(std::unique_ptr<IFrameGenerationMode> mode)
{
    // Le candidat est vérifié et initialisé AVANT d'arrêter le mode actif : un refus ne laisse pas le renderer sans mode.
    if (mode != nullptr)
    {
        if (!mode->isSupported(device_.capabilities()))
        {
            return makeError(ErrorCode::kUnsupported, "setFrameGenerationMode: '" + mode->description().name + "' is not supported by this device");
        }
        if (!mode->initialize(device_))
        {
            return makeError(ErrorCode::kUnsupported, "setFrameGenerationMode: '" + mode->description().name + "' failed to initialize");
        }
    }
    if (frameGenMode_)
    {
        frameGenMode_->shutdown();
    }
    frameGenMode_ = std::move(mode);
    generatedFrame_ = {};
    return {};
}

bool SceneRenderer::needsJitter() const noexcept { return modeRuns() && activeUpscaleMode_->requirements().needsJitter; }

bool SceneRenderer::needsHistory() const noexcept
{
    return (modeRuns() && activeUpscaleMode_->requirements().needsHistory) || (frameGenMode_ != nullptr && frameGenMode_->description().needsHistory);
}

core::Result<> SceneRenderer::rebuildFrameGroup()
{
    LightManager &lights = lights_ != nullptr ? *lights_ : defaultLights_;
    if (!frameGroup_.isNull() && groupCamera_ == cameraBuffer_.handle() && groupLights_ == lights.buffer().handle())
    {
        return {};
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = cameraBuffer_.handle(), .size = sizeof(CameraData)});
    desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageBuffer, .buffer = lights.buffer().handle()});
    desc.debugName = "scene.frame";
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    device_.destroy(frameGroup_);
    frameGroup_ = *group;
    groupCamera_ = cameraBuffer_.handle();
    groupLights_ = lights.buffer().handle();
    return {};
}

core::Result<> SceneRenderer::beginFrame(rhi::CommandEncoder &encoder, const FrameOptions &options)
{
    if (passOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::beginFrame: the previous frame was not ended");
    }
    if (outputWidth_ <= 0 || outputHeight_ <= 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::beginFrame: set a non-empty output resolution first");
    }
    if (scaled())
    {
        frameWidth_ = std::max(1, static_cast<int>(static_cast<float>(outputWidth_) * renderScale_));
        frameHeight_ = std::max(1, static_cast<int>(static_cast<float>(outputHeight_) * renderScale_));
    }
    else
    {
        frameWidth_ = outputWidth_;
        frameHeight_ = outputHeight_;
    }
    if (jitterMode_ == JitterMode::kAuto)
    {
        if (needsJitter()) // seul un mode exécuté qui demande le jitter le reçoit
        {
            currentJitter_ = haltonJitter(jitterIndex_, jitterSequenceLength_, frameWidth_, frameHeight_);
            jitterIndex_ = (jitterIndex_ + 1) % jitterSequenceLength_;
        }
        else
        {
            currentJitter_ = {0.0f, 0.0f};
            jitterIndex_ = 0;
        }
    }

    RenderTargetInfo info;
    info.size = {static_cast<std::uint32_t>(frameWidth_), static_cast<std::uint32_t>(frameHeight_)};
    info.colorFormats = {rhi::Format::kRGBA8Unorm};
    if (motionVectorsEnabled_)
    {
        info.colorFormats.push_back(rhi::Format::kRG16Float);
    }
    info.depth = true;
    info.depthSampled = depthAsTextureEnabled_;
    info.debugName = "scene";
    if (!sceneTarget_.valid())
    {
        auto target = RenderTarget::create(device_, info);
        if (!target)
        {
            return std::unexpected(target.error());
        }
        sceneTarget_ = std::move(*target);
    }
    else
    {
        const RenderTargetInfo &now = sceneTarget_.info();
        if (now.colorFormats != info.colorFormats || now.depthSampled != info.depthSampled || now.size.width != info.size.width ||
            now.size.height != info.size.height)
        {
            if (auto r = sceneTarget_.reconfigure(info); !r)
            {
                return r;
            }
        }
    }
    if (auto r = rebuildFrameGroup(); !r)
    {
        return r;
    }
    PassOptions pass;
    pass.clearColor = options.clearColor;
    pass.depthLoad = options.clearDepth ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad;
    pass.clearDepth = 0.0f; // reverse-Z (REN-02) : 0 est le plus lointain
    encoder_ = &encoder;
    encoder.beginRenderPass(sceneTarget_.passDesc(pass));
    passOpen_ = true;
    return {};
}

core::Result<> SceneRenderer::setCamera(Camera &camera)
{
    if (!cameraBuffer_.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::setCamera: not created");
    }
    camera.setJitter(currentJitter_);
    cameraOrigin_ = cameraRelative_ ? camera.position() : glm::vec3(0.0f);
    const CameraData data = camera.data(cameraRelative_);
    return cameraBuffer_.update(0, std::as_bytes(std::span(&data, 1)));
}

core::Result<> SceneRenderer::draw(Mesh &mesh, const Camera &, bool lines)
{
    GraphicsState state;
    state.vertexBuffers = mesh.vertexBuffers();
    state.vertexAttributes = mesh.vertexAttributes();
    state.raster.cull = rhi::CullMode::kNone;
    state.raster.fill = lines ? rhi::FillMode::kLine : rhi::FillMode::kFill;
    // Reverse-Z (REN-02) : le plus proche a la plus GRANDE profondeur. Le passage fil de fer redessine la même géométrie : à
    // profondeur égale, ses arêtes doivent passer.
    state.depth = {true, true, lines ? rhi::CompareOp::kGreaterEqual : rhi::CompareOp::kGreater};
    state.depthFormat = rhi::Format::kD32Float;
    state.colorTargets.clear();
    for (std::size_t i = 0; i < sceneTarget_.info().colorFormats.size(); ++i)
    {
        rhi::ColorTargetState t;
        t.format = sceneTarget_.info().colorFormats[i];
        // Les shaders de maillage n'écrivent que la couleur : les cibles supplémentaires (vecteurs de mouvement) restent intactes.
        t.writeMask = i == 0 ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone;
        state.colorTargets.push_back(t);
    }
    state.debugName = mesh.program()->name() + (lines ? ".lines" : "");
    auto pipeline = pipelines_.graphics(*mesh.program(), state);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    // Les lumières ont pu changer depuis beginFrame (setLights pendant l'image) : le groupe suit avant chaque dessin.
    if (auto r = rebuildFrameGroup(); !r)
    {
        return r;
    }
    rhi::CommandEncoder &e = *encoder_;
    e.setPipeline(*pipeline);
    e.setBindGroup(0, frameGroup_);
    if (!mesh.textureGroup().isNull())
    {
        e.setBindGroup(1, mesh.textureGroup());
    }
    e.setBindGroup(2, mesh.modelGroup());
    // Taille de la révision qui a construit CE pipeline (après un rechargement qui ne se lie pas, c'est l'ancienne).
    const std::uint32_t pushSize = pipelines_.pushConstantSize(*pipeline);
    if (pushSize > 0)
    {
        std::vector<std::byte> push(pushSize, std::byte{0});
        if (const std::size_t n = std::min<std::size_t>(pushSize, mesh.pushConstants().size()); n > 0) // memcpy(…, nullptr, 0) est un comportement indéfini
        {
            std::memcpy(push.data(), mesh.pushConstants().data(), n);
        }
        if (pushSize >= sizeof(std::int32_t))
        {
            const std::int32_t flag = lines ? 1 : 0; // convention mesh.frag : premier entier = fil de fer
            std::memcpy(push.data(), &flag, sizeof flag);
        }
        e.setPushConstants(push);
    }
    e.setVertexBuffer(0, mesh.vertexBuffer());
    if (mesh.instanced())
    {
        e.setVertexBuffer(1, mesh.instanceBuffer());
    }
    e.setIndexBuffer(mesh.indexBuffer(), rhi::IndexType::kUint32);
    e.drawIndexed(mesh.indexCount(), mesh.instanceCount());
    return {};
}

core::Result<> SceneRenderer::drawMesh(Mesh &mesh, const Camera &camera)
{
    if (!passOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawMesh: no frame is open");
    }
    if (!mesh.valid() || !mesh.program() || mesh.program()->isCompute())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawMesh: the mesh is empty or has no graphics program");
    }
    if (auto r = mesh.uploadTransform(cameraOrigin_); !r)
    {
        return r;
    }
    if (auto r = draw(mesh, camera, false); !r)
    {
        return r;
    }
    return camera.wireframe() ? draw(mesh, camera, true) : core::Result<>{};
}

core::Result<> SceneRenderer::drawDebug(const DebugDraw &debug)
{
    if (!debug.enabled() || debug.lineCount() == 0)
    {
        return {};
    }
    if (!passOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawDebug: no frame is open");
    }
    if (!debugProgram_)
    {
        auto program = library_.loadGraphics(shaderDirectory_ / "debug_line.vert", shaderDirectory_ / "debug_line.frag");
        if (!program)
        {
            return std::unexpected(program.error());
        }
        debugProgram_ = *program;
    }
    if (auto r = rebuildFrameGroup(); !r)
    {
        return r;
    }
    // Rendu relatif à la caméra : les positions sont ramenées à l'origine de la caméra avant l'envoi.
    std::vector<float> vertices = debug.vertices();
    if (cameraRelative_)
    {
        for (std::size_t v = 0; v < vertices.size(); v += DebugDraw::kFloatsPerVertex)
        {
            vertices[v] -= cameraOrigin_.x;
            vertices[v + 1] -= cameraOrigin_.y;
            vertices[v + 2] -= cameraOrigin_.z;
        }
    }
    const std::uint64_t bytes = vertices.size() * sizeof(float);
    if (!debugBuffer_.valid() || debugBuffer_.size() < bytes)
    {
        rhi::BufferDesc desc;
        desc.size = std::max<std::uint64_t>(bytes, 64 * 1024);
        desc.usage = rhi::BufferUsage::kVertex | rhi::BufferUsage::kCopyDst;
        desc.debugName = "debug.lines";
        auto buffer = GpuBuffer::create(device_, desc);
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        debugBuffer_ = std::move(*buffer);
    }
    if (auto r = debugBuffer_.update(0, std::as_bytes(std::span(vertices))); !r)
    {
        return r;
    }
    GraphicsState state;
    state.topology = rhi::PrimitiveTopology::kLineList;
    // assign(1, ...) et non `= {{...}}` : GCC 13 en -O3 avec ASan signale à tort -Warray-bounds sur la liste d'un élément.
    state.vertexBuffers.assign(1, rhi::VertexBufferLayout{.binding = 0,
                                                          .stride = static_cast<std::uint32_t>(DebugDraw::kFloatsPerVertex * sizeof(float)),
                                                          .stepRate = rhi::VertexStepRate::kVertex});
    state.vertexAttributes = {{.location = 0, .binding = 0, .format = rhi::VertexFormat::kFloat3, .offset = 0},
                              {.location = 1, .binding = 0, .format = rhi::VertexFormat::kFloat4, .offset = 12}};
    state.raster.cull = rhi::CullMode::kNone;
    state.depth = {debug.depthTest(), false, rhi::CompareOp::kGreaterEqual}; // reverse-Z ; n'écrit pas la profondeur
    state.depthFormat = rhi::Format::kD32Float;
    state.colorTargets.clear();
    for (std::size_t i = 0; i < sceneTarget_.info().colorFormats.size(); ++i)
    {
        rhi::ColorTargetState t;
        t.format = sceneTarget_.info().colorFormats[i];
        t.writeMask = i == 0 ? rhi::ColorWriteMask::kAll : rhi::ColorWriteMask::kNone;
        state.colorTargets.push_back(t);
    }
    state.debugName = "debug.lines";
    auto pipeline = pipelines_.graphics(*debugProgram_, state);
    if (!pipeline)
    {
        return std::unexpected(pipeline.error());
    }
    rhi::CommandEncoder &e = *encoder_;
    e.setPipeline(*pipeline);
    e.setBindGroup(0, frameGroup_);
    e.setVertexBuffer(0, debugBuffer_.handle());
    e.draw(static_cast<std::uint32_t>(vertices.size() / DebugDraw::kFloatsPerVertex));
    return {};
}

core::Result<> SceneRenderer::endFrame(const UpscaleTarget &output)
{
    if (!passOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::endFrame: no frame is open");
    }
    encoder_->endRenderPass();
    passOpen_ = false;
    generatedFrame_ = {}; // pas de handle d'une image précédente si cette fin d'image échoue avant la génération
    FrameContext context{device_, *encoder_, *blit_};

    for (const auto &pass : postProcessPasses_)
    {
        if (auto r = pass->process(context, sceneTarget_); !r)
        {
            return r;
        }
    }
    // Historique : alloué (ou redimensionné) AVANT ses consommateurs, qui y lisent l'image précédente ; la couleur courante n'y
    // est copiée qu'après eux. Une cible neuve ou redimensionnée ne contient pas d'image précédente : resetHistory.
    const bool history = needsHistory();
    bool historyFresh = false;
    if (history)
    {
        const std::uint32_t w = static_cast<std::uint32_t>(frameWidth_);
        const std::uint32_t h = static_cast<std::uint32_t>(frameHeight_);
        if (!historyTarget_.valid())
        {
            RenderTargetInfo info;
            info.size = {w, h};
            info.colorFormats = {rhi::Format::kRGBA8Unorm};
            info.depth = false;
            info.debugName = "scene.history";
            auto target = RenderTarget::create(device_, info);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            historyTarget_ = std::move(*target);
            historyFresh = true;
        }
        else if (historyTarget_.width() != w || historyTarget_.height() != h)
        {
            if (auto r = historyTarget_.resize(w, h); !r)
            {
                return r;
            }
            historyFresh = true;
        }
    }
    // L'historique n'est « l'image précédente » que s'il a été écrit avec succès à l'image précédente, à la même taille. Il est
    // marqué invalide tout de suite : seule une copie réussie à la fin de CETTE image le revalide (un retour anticipé le laisse
    // invalide, de même qu'une image qui n'en a pas besoin).
    const bool previousValid = history && historyValid_ && !historyFresh;
    historyValid_ = false;
    const bool resetHistory = resetHistoryNextFrame_ || !previousValid;
    const rhi::TextureHandle previous = previousValid ? historyTarget_.color(0) : rhi::TextureHandle{};

    if (modeRuns())
    {
        if (activeUpscaleMode_->advanced())
        {
            auto *advanced = static_cast<IAdvancedUpscaleMode *>(activeUpscaleMode_); // advanced() : pas de RTTI
            UpscaleInput input;
            input.color = sceneTarget_.color(0);
            input.depth = sceneTarget_.depth();
            input.motionVectors = (motionVectorsEnabled_ && sceneTarget_.colorCount() > 1) ? sceneTarget_.color(1) : rhi::TextureHandle{};
            input.historyColor = previous;
            input.renderResolution = {static_cast<float>(frameWidth_), static_cast<float>(frameHeight_)};
            input.outputResolution = {static_cast<float>(outputWidth_), static_cast<float>(outputHeight_)};
            input.jitter = currentJitter_;
            input.deltaTimeSeconds = deltaTime_;
            input.resetHistory = resetHistory;
            UpscaleOutput out;
            out.output = output.texture;
            if (auto r = advanced->execute(context, input, out); !r)
            {
                return r;
            }
        }
        else if (auto r = activeUpscaleMode_->upscale(context, sceneTarget_, output); !r)
        {
            return r;
        }
    }
    else if (!scaled())
    {
        // Sans mise à l'échelle (ou sans mode), l'ancien rendu allait droit à l'écran : on garde une copie 1:1.
        if (auto r = blit_->copy(*encoder_, sceneTarget_.color(0), blit_->nearestSampler(), output.texture, output.format, "present"); !r)
        {
            return r;
        }
    }
    if (frameGenMode_ != nullptr)
    {
        FrameGenerationInput input;
        input.currentColor = sceneTarget_.color(0);
        input.previousColor = previous;
        input.depth = sceneTarget_.depth();
        input.motionVectors = (motionVectorsEnabled_ && sceneTarget_.colorCount() > 1) ? sceneTarget_.color(1) : rhi::TextureHandle{};
        input.renderResolution = {static_cast<float>(frameWidth_), static_cast<float>(frameHeight_)};
        input.outputResolution = {static_cast<float>(outputWidth_), static_cast<float>(outputHeight_)};
        input.jitter = currentJitter_;
        input.deltaTimeSeconds = deltaTime_;
        input.resetHistory = resetHistory;
        FrameGenerationOutput out;
        if (auto r = frameGenMode_->generateFrame(context, input, out); !r)
        {
            return r;
        }
        generatedFrame_ = out.generatedFrame;
    }
    if (history)
    {
        if (auto r = blit_->copy(*encoder_, sceneTarget_.color(0), blit_->nearestSampler(), historyTarget_.color(0), rhi::Format::kRGBA8Unorm, "history");
            !r)
        {
            return r;
        }
        historyValid_ = true;
        resetHistoryNextFrame_ = false; // la demande de réinitialisation a été transmise avec un historique neuf
    }
    encoder_ = nullptr;
    return {};
}

core::Result<> SceneRenderer::saveColorPng(const std::filesystem::path &path) const
{
    if (!sceneTarget_.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::saveColorPng: nothing was rendered yet");
    }
    return savePng(device_, sceneTarget_.color(0), path);
}

} // namespace orosmo::renderer
