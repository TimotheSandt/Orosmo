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
    rhi::BufferDesc fb;
    fb.size = sizeof(FrameData);
    fb.usage = rhi::BufferUsage::kUniform | rhi::BufferUsage::kCopyDst;
    fb.debugName = "frame.data";
    auto frameBuffer = GpuBuffer::create(device, fb);
    if (!frameBuffer)
    {
        return std::unexpected(frameBuffer.error());
    }
    renderer->frameBuffer_ = std::move(*frameBuffer);
    renderer->frameResources_.buffer = renderer->frameBuffer_.handle();
    renderer->frameResources_.data = &renderer->frameData_;
    renderer->graph_.setFrameResources(&renderer->frameResources_);
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
    graph_.reset(); // les passes retiennent des pointeurs vers ce renderer et ses plugins
    features_.clear();
    device_.destroy(frameGroup_);
}

core::Result<RenderFeature *> SceneRenderer::addFeature(std::unique_ptr<RenderFeature> feature)
{
    if (feature == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::addFeature: null feature");
    }
    if (!feature->supports(device_.capabilities()))
    {
        return makeError(ErrorCode::kUnsupported, "SceneRenderer::addFeature: '" + feature->name() + "' is not supported by this device");
    }
    for (const auto &existing : features_)
    {
        if (existing->name() == feature->name())
        {
            return makeError(ErrorCode::kAlreadyExists, "SceneRenderer::addFeature: a feature named '" + feature->name() + "' is registered");
        }
    }
    features_.push_back(std::move(feature));
    return features_.back().get();
}

void SceneRenderer::removeFeature(const std::string &name)
{
    std::erase_if(features_, [&](const std::unique_ptr<RenderFeature> &f) { return f->name() == name; });
    featureFrameBindings_.clear(); // les liaisons d'un plugin retiré ne doivent plus entrer dans le groupe 0 (redéclarées à chaque image)
}

std::vector<std::string> SceneRenderer::featureNames() const
{
    std::vector<std::string> names;
    for (const auto &f : features_)
    {
        names.push_back(f->name());
    }
    return names;
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

core::Result<> SceneRenderer::setSceneColorFormat(rhi::Format format)
{
    if (format != rhi::Format::kRGBA8Unorm && format != rhi::Format::kRGBA16Float && format != rhi::Format::kRGBA32Float)
    {
        return makeError(ErrorCode::kInvalidArgument, std::string("SceneRenderer::setSceneColorFormat: unsupported format ") + rhi::toString(format));
    }
    sceneColorFormat_ = format;
    return {};
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

namespace
{
bool sameEntries(const std::vector<rhi::BindGroupEntry> &a, const std::vector<rhi::BindGroupEntry> &b)
{
    return std::ranges::equal(a, b,
                              [](const rhi::BindGroupEntry &x, const rhi::BindGroupEntry &y)
                              {
                                  return x.binding == y.binding && x.type == y.type && x.buffer == y.buffer && x.offset == y.offset &&
                                         x.size == y.size && x.dynamicOffset == y.dynamicOffset && x.texture == y.texture &&
                                         x.sampler == y.sampler && x.mipLevel == y.mipLevel;
                              });
}
} // namespace

core::Result<> SceneRenderer::rebuildFrameGroup(LightManager &lights)
{
    if (!frameGroup_.isNull() && groupCamera_ == cameraBuffer_.handle() && groupLights_ == lights.buffer().handle() &&
        sameEntries(groupFeatureBindings_, featureFrameBindings_))
    {
        return {};
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back({.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = cameraBuffer_.handle(), .size = sizeof(CameraData)});
    desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageBuffer, .buffer = lights.buffer().handle()});
    desc.entries.push_back({.binding = kFrameDataBinding, .type = rhi::BindingType::kUniformBuffer, .buffer = frameBuffer_.handle(), .size = sizeof(FrameData)});
    desc.entries.insert(desc.entries.end(), featureFrameBindings_.begin(), featureFrameBindings_.end());
    desc.debugName = "scene.frame";
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    device_.destroy(frameGroup_);
    frameGroup_ = *group;
    frameResources_.group = frameGroup_;
    groupCamera_ = cameraBuffer_.handle();
    groupLights_ = lights.buffer().handle();
    groupFeatureBindings_ = featureFrameBindings_;
    return {};
}

core::Result<> SceneRenderer::beginFrame(rhi::CommandEncoder &encoder, const FrameOptions &options)
{
    if (frameOpen_)
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
    info.colorFormats = {sceneColorFormat_};
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
    if (auto r = rebuildFrameGroup(activeLights()); !r)
    {
        return r;
    }
    frameOptions_ = options;
    prepassThisFrame_ = depthPrepass_;
    draws_.clear();
    debugVertices_.clear();
    encoder_ = &encoder;
    frameOpen_ = true;
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
    // Données par image (REN-03) : mêmes matrices que le tampon de la caméra.
    cameraView_ = cameraRelative_ ? camera.relativeViewMatrix() : camera.viewMatrix();
    cameraProjection_ = camera.projectionMatrix();
    cameraWorld_ = glm::dvec3(camera.position());
    const CameraData data = camera.data(cameraRelative_);
    return cameraBuffer_.update(0, std::as_bytes(std::span(&data, 1)));
}

core::Result<> SceneRenderer::record(Mesh &mesh, bool lines)
{
    GraphicsState state;
    state.vertexBuffers = mesh.vertexBuffers();
    state.vertexAttributes = mesh.vertexAttributes();
    state.raster.cull = rhi::CullMode::kNone;
    state.raster.fill = lines ? rhi::FillMode::kLine : rhi::FillMode::kFill;
    // Reverse-Z (REN-02) : le plus proche a la plus GRANDE profondeur. Le passage fil de fer redessine la même géométrie : à
    // profondeur égale, ses arêtes doivent passer.
    state.depth = {true, true, lines ? rhi::CompareOp::kGreaterEqual : rhi::CompareOp::kGreater};
    // Pré-passe (REN-04) : la scène retrouve la profondeur de la pré-passe (égale) sans la réécrire.
    const bool prepass = prepassThisFrame_ && !lines;
    if (prepass)
    {
        state.depth = {true, false, rhi::CompareOp::kGreaterEqual};
    }
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
    SceneDraw draw;
    draw.pipeline = *pipeline;
    if (prepass)
    {
        GraphicsState depthOnly = state;
        depthOnly.depth = {true, true, rhi::CompareOp::kGreater};
        for (rhi::ColorTargetState &t : depthOnly.colorTargets)
        {
            t.writeMask = rhi::ColorWriteMask::kNone; // mêmes attachements que la scène, aucune écriture de couleur
        }
        depthOnly.debugName += ".prepass";
        auto prepassPipeline = pipelines_.graphics(*mesh.program(), depthOnly);
        if (!prepassPipeline)
        {
            return std::unexpected(prepassPipeline.error());
        }
        draw.prepassPipeline = *prepassPipeline;
    }
    // Taille de la révision qui a construit CE pipeline (après un rechargement qui ne se lie pas, c'est l'ancienne). Les valeurs
    // sont celles de CE dessin : copiées maintenant.
    const std::uint32_t pushSize = pipelines_.pushConstantSize(*pipeline);
    if (pushSize > 0)
    {
        draw.pushConstants.assign(pushSize, std::byte{0});
        if (const std::size_t n = std::min<std::size_t>(pushSize, mesh.pushConstants().size()); n > 0) // memcpy(…, nullptr, 0) est un comportement indéfini
        {
            std::memcpy(draw.pushConstants.data(), mesh.pushConstants().data(), n);
        }
        if (pushSize >= sizeof(std::int32_t))
        {
            const std::int32_t flag = lines ? 1 : 0; // convention mesh.frag : premier entier = fil de fer
            std::memcpy(draw.pushConstants.data(), &flag, sizeof flag);
        }
    }
    draw.lights = &activeLights(); // les lumières choisies au moment du dessin, comme avant l'enregistrement différé
    draw.textureGroup = mesh.textureGroup();
    draw.modelGroup = mesh.modelGroup();
    draw.vertexBuffer = mesh.vertexBuffer();
    draw.instanceBuffer = mesh.instanced() ? mesh.instanceBuffer() : rhi::BufferHandle{};
    draw.indexBuffer = mesh.indexBuffer();
    draw.count = mesh.indexCount();
    draw.instanceCount = mesh.instanceCount();
    draws_.push_back(std::move(draw));
    return {};
}

core::Result<> SceneRenderer::drawMesh(Mesh &mesh, const Camera &camera)
{
    if (!frameOpen_)
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
    if (auto r = record(mesh, false); !r)
    {
        return r;
    }
    return camera.wireframe() ? record(mesh, true) : core::Result<>{};
}

core::Result<SceneDrawInfo> SceneRenderer::sceneDrawInfo() const
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::sceneDrawInfo: no frame is open");
    }
    return SceneDrawInfo{sceneTarget_.info().colorFormats, rhi::Format::kD32Float, cameraOrigin_};
}

core::Result<> SceneRenderer::drawCustom(SceneDrawCallback draw)
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawCustom: no frame is open");
    }
    if (!draw)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawCustom: empty callback");
    }
    SceneDraw recorded;
    recorded.lights = &activeLights();
    recorded.custom = std::move(draw);
    draws_.push_back(std::move(recorded));
    return {};
}

core::Result<> SceneRenderer::drawDebug(const DebugDraw &debug)
{
    if (!debug.enabled() || debug.lineCount() == 0)
    {
        return {};
    }
    if (!frameOpen_)
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
    // Les segments de toute l'image partagent un tampon, envoyé une fois à endFrame : chaque appel garde sa tranche.
    SceneDraw draw;
    draw.pipeline = *pipeline;
    draw.lights = &activeLights();
    draw.debugLines = true;
    draw.vertexOffset = debugVertices_.size() * sizeof(float);
    draw.count = static_cast<std::uint32_t>(vertices.size() / DebugDraw::kFloatsPerVertex);
    debugVertices_.insert(debugVertices_.end(), vertices.begin(), vertices.end());
    draws_.push_back(std::move(draw));
    return {};
}

core::Result<> SceneRenderer::uploadDebugLines()
{
    if (debugVertices_.empty())
    {
        return {};
    }
    const std::uint64_t bytes = debugVertices_.size() * sizeof(float);
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
    return debugBuffer_.update(0, std::as_bytes(std::span(debugVertices_)));
}

core::Result<> SceneRenderer::playDraws(rhi::CommandEncoder &e, bool prepass)
{
    for (const SceneDraw &draw : draws_)
    {
        if (prepass && draw.prepassPipeline.isNull())
        {
            continue; // dessin externe, lignes, fil de fer : seulement dans la passe de scène
        }
        // Les lumières ont pu changer pendant l'image (setLights après beginFrame) : le groupe suit avant chaque dessin.
        if (auto r = rebuildFrameGroup(*draw.lights); !r)
        {
            return r;
        }
        if (draw.custom)
        {
            if (auto r = draw.custom(e, frameGroup_); !r)
            {
                return r;
            }
            continue;
        }
        e.setPipeline(prepass ? draw.prepassPipeline : draw.pipeline);
        e.setBindGroup(0, frameGroup_);
        if (draw.debugLines)
        {
            e.setVertexBuffer(0, debugBuffer_.handle(), draw.vertexOffset);
            e.draw(draw.count);
            continue;
        }
        if (!draw.textureGroup.isNull())
        {
            e.setBindGroup(1, draw.textureGroup);
        }
        e.setBindGroup(2, draw.modelGroup);
        if (prepass && draw.pushConstants.size() >= sizeof(std::int32_t))
        {
            // Convention mesh.frag : premier entier = 2 dans la pré-passe (couleur masquée, le shader saute son éclairage).
            std::vector<std::byte> constants = draw.pushConstants;
            const std::int32_t flag = 2;
            std::memcpy(constants.data(), &flag, sizeof flag);
            e.setPushConstants(constants);
        }
        else if (!draw.pushConstants.empty())
        {
            e.setPushConstants(draw.pushConstants);
        }
        e.setVertexBuffer(0, draw.vertexBuffer);
        if (!draw.instanceBuffer.isNull())
        {
            e.setVertexBuffer(1, draw.instanceBuffer);
        }
        e.setIndexBuffer(draw.indexBuffer, rhi::IndexType::kUint32);
        e.drawIndexed(draw.count, draw.instanceCount);
    }
    return {};
}

core::Result<> SceneRenderer::updateFrameData()
{
    FrameData f;
    f.view = cameraView_;
    f.proj = cameraProjection_;
    f.viewProj = cameraProjection_ * cameraView_;
    f.invView = glm::inverse(f.view);
    f.invProj = glm::inverse(f.proj);
    f.invViewProj = glm::inverse(f.viewProj);
    f.prevViewProj = hasPreviousFrame_ ? previousViewProj_ : f.viewProj;
    const glm::vec3 shaderOrigin = cameraRelative_ ? glm::vec3(0.0f) : glm::vec3(cameraWorld_);
    f.cameraRelativeOrigin = glm::vec4(shaderOrigin, 0.0f);
    const SplitDouble x = splitDouble(cameraWorld_.x);
    const SplitDouble y = splitDouble(cameraWorld_.y);
    const SplitDouble z = splitDouble(cameraWorld_.z);
    f.cameraWorldHigh = {x.high, y.high, z.high, 0.0f};
    f.cameraWorldLow = {x.low, y.low, z.low, 0.0f};
    f.jitter = {currentJitter_.x, currentJitter_.y, hasPreviousFrame_ ? previousJitter_.x : currentJitter_.x,
                hasPreviousFrame_ ? previousJitter_.y : currentJitter_.y};
    const auto w = static_cast<float>(frameWidth_);
    const auto h = static_cast<float>(frameHeight_);
    f.viewport = {w, h, 1.0f / w, 1.0f / h};
    timeSeconds_ += static_cast<double>(deltaTime_);
    f.time = {static_cast<float>(timeSeconds_), deltaTime_, static_cast<float>(frameIndex_), frameNoiseSeed(frameIndex_)};
    // Lumière principale : la première lumière directionnelle des lumières actives.
    for (const Light &light : activeLights().lights())
    {
        if (light.type == LightType::kDirectional)
        {
            const float length = glm::length(light.direction);
            f.sunDirection = glm::vec4(length > 0.0f ? light.direction / length : light.direction, 1.0f);
            f.sunColor = glm::vec4(light.color * light.strength, 0.0f);
            break;
        }
    }
    f.planet = environment_.planet;
    f.scatteringRayleigh = environment_.scatteringRayleigh;
    f.scatteringMie = environment_.scatteringMie;
    f.exposure = {environment_.exposure, 0.0f, 0.0f, 0.0f};
    frameData_ = f;
    previousViewProj_ = f.viewProj;
    previousJitter_ = currentJitter_;
    hasPreviousFrame_ = true;
    ++frameIndex_;
    return frameBuffer_.update(0, std::as_bytes(std::span(&frameData_, 1)));
}

core::Result<> SceneRenderer::ensureHistory(RenderTarget &target, bool &fresh)
{
    const std::uint32_t w = static_cast<std::uint32_t>(frameWidth_);
    const std::uint32_t h = static_cast<std::uint32_t>(frameHeight_);
    fresh = false;
    if (!target.valid())
    {
        RenderTargetInfo info;
        info.size = {w, h};
        info.colorFormats = {rhi::Format::kRGBA8Unorm};
        info.depth = false;
        info.debugName = "scene.history";
        auto created = RenderTarget::create(device_, info);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        target = std::move(*created);
        fresh = true;
    }
    else if (target.width() != w || target.height() != h)
    {
        if (auto r = target.resize(w, h); !r)
        {
            return r;
        }
        fresh = true;
    }
    return {};
}

core::Result<> SceneRenderer::buildGraph(const UpscaleTarget &output)
{
    graph_.reset();
    const rhi::Extent2D renderSize{static_cast<std::uint32_t>(frameWidth_), static_cast<std::uint32_t>(frameHeight_)};
    RenderGraphBuilder::Resources known;
    known.renderSize = renderSize;
    known.outputSize = output.size;
    known.sceneColor = graph_.importTexture("scene.color", sceneTarget_.color(0), {.format = sceneTarget_.info().colorFormats[0], .size = renderSize});
    if (motionVectorsEnabled_ && sceneTarget_.colorCount() > 1)
    {
        known.motionVectors = graph_.importTexture("scene.motion", sceneTarget_.color(1), {.format = rhi::Format::kRG16Float, .size = renderSize});
    }
    if (!sceneTarget_.depth().isNull())
    {
        known.sceneDepth = graph_.importTexture("scene.depth", sceneTarget_.depth(), {.format = rhi::Format::kD32Float, .size = renderSize});
    }
    if (!output.texture.isNull())
    {
        known.output = graph_.importTexture("output", output.texture, {.format = output.format, .size = output.size});
    }

    featureFrameBindings_.clear(); // redéclarées par les plugins à chaque image

    // ── Pré-passe de profondeur (REN-04) : mêmes attachements que la scène, profondeur seule ─────────────────────────
    const bool prepass = prepassThisFrame_ && known.sceneDepth.valid();
    if (prepass)
    {
        auto depth = graph_.addPass("depth-prepass", PassType::kRaster, [this](RenderPassContext &ctx) { return playDraws(ctx.encoder(), true); });
        depth.writeColor(known.sceneColor, rhi::LoadOp::kClear, frameOptions_.clearColor);
        if (known.motionVectors.valid())
        {
            depth.writeColor(known.motionVectors, rhi::LoadOp::kClear, frameOptions_.clearColor);
        }
        depth.writeDepth(known.sceneDepth, frameOptions_.clearDepth ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, 0.0f);
    }

    // ── Passe de scène : joue les dessins enregistrés ────────────────────────────────────────────────────────────────
    {
        auto scene = graph_.addPass("scene", PassType::kRaster, [this](RenderPassContext &ctx) { return playDraws(ctx.encoder()); });
        scene.writeColor(known.sceneColor, rhi::LoadOp::kClear, frameOptions_.clearColor);
        if (known.motionVectors.valid())
        {
            scene.writeColor(known.motionVectors, rhi::LoadOp::kClear, frameOptions_.clearColor);
        }
        if (known.sceneDepth.valid())
        {
            const bool clear = frameOptions_.clearDepth && !prepass; // après la pré-passe : sa profondeur est gardée
            scene.writeDepth(known.sceneDepth, clear ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, 0.0f); // reverse-Z : 0 au loin
        }
    }

    // ── Post-traitements : modifient la cible de la scène, dans l'ordre d'ajout ───────────────────────────────────────
    for (std::size_t i = 0; i < postProcessPasses_.size(); ++i)
    {
        IPostProcessPass *post = postProcessPasses_[i].get();
        std::string name = "post." + post->name();
        if (graph_.pass(name))
        {
            name += "#" + std::to_string(i);
        }
        graph_
            .addPass(std::move(name), PassType::kGeneric,
                     [this, post](RenderPassContext &ctx)
                     {
                         FrameContext context{device_, ctx.encoder(), *blit_};
                         return post->process(context, sceneTarget_);
                     })
            .read(known.sceneColor)
            .write(known.sceneColor);
    }

    // ── Historique en double tampon : lecture de l'image précédente, copie de l'image courante dans l'autre tampon ──────
    // L'historique n'est « l'image précédente » que s'il a été écrit avec succès à l'image précédente, à la même taille. Il est
    // marqué invalide tout de suite : seule une copie réussie à la fin de CETTE image le revalide (un retour anticipé le laisse
    // invalide, de même qu'une image qui n'en a pas besoin).
    const bool history = needsHistory();
    bool previousValid = false;
    RgTexture previousHistory;
    RgTexture nextHistory;
    const std::uint32_t next = historyValid_ ? (lastHistory_ ^ 1u) : lastHistory_;
    if (history)
    {
        bool fresh = false;
        if (historyValid_)
        {
            RenderTarget &previous = history_[lastHistory_];
            previousValid = previous.valid() && previous.width() == renderSize.width && previous.height() == renderSize.height;
        }
        if (auto r = ensureHistory(history_[next], fresh); !r)
        {
            return r;
        }
        if (previousValid)
        {
            previousHistory = graph_.importTexture("history.previous", history_[lastHistory_].color(0), {.size = renderSize});
        }
        nextHistory = graph_.importTexture("history.next", history_[next].color(0), {.size = renderSize});
    }
    historyValid_ = false;
    const bool resetHistory = resetHistoryNextFrame_ || !previousValid;
    const rhi::TextureHandle previous = previousValid ? history_[lastHistory_].color(0) : rhi::TextureHandle{};

    // ── Mise à l'échelle (ou copie 1:1) vers la sortie ────────────────────────────────────────────────────────────────
    if (modeRuns() || !scaled())
    {
        if (!known.output.valid())
        {
            return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::endFrame: the output texture is null");
        }
        auto upscale = graph_.addPass(
            "upscale", PassType::kGeneric,
            [this, output, previous, resetHistory](RenderPassContext &ctx) -> core::Result<>
            {
                FrameContext context{device_, ctx.encoder(), *blit_};
                if (!modeRuns())
                {
                    // Sans mise à l'échelle (ou sans mode), l'ancien rendu allait droit à l'écran : on garde une copie 1:1.
                    return blit_->copy(ctx.encoder(), sceneTarget_.color(0), blit_->nearestSampler(), output.texture, output.format, "present");
                }
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
                    return advanced->execute(context, input, out);
                }
                return activeUpscaleMode_->upscale(context, sceneTarget_, output);
            });
        // readFinal : les post-traitements des plugins, déclarés plus tard, s'exécutent avant la mise à l'échelle.
        upscale.readFinal(known.sceneColor).write(known.output);
        if (known.sceneDepth.valid())
        {
            upscale.readFinal(known.sceneDepth);
        }
        if (known.motionVectors.valid())
        {
            upscale.readFinal(known.motionVectors);
        }
        if (previousHistory.valid())
        {
            upscale.read(previousHistory);
        }
    }

    // ── Génération d'images ───────────────────────────────────────────────────────────────────────────────────────────
    if (frameGenMode_ != nullptr)
    {
        auto generation = graph_.addPass("frame-generation", PassType::kGeneric,
                                         [this, previous, resetHistory](RenderPassContext &ctx) -> core::Result<>
                                         {
                                             FrameContext context{device_, ctx.encoder(), *blit_};
                                             FrameGenerationInput input;
                                             input.currentColor = sceneTarget_.color(0);
                                             input.previousColor = previous;
                                             input.depth = sceneTarget_.depth();
                                             input.motionVectors =
                                                 (motionVectorsEnabled_ && sceneTarget_.colorCount() > 1) ? sceneTarget_.color(1) : rhi::TextureHandle{};
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
                                             return core::Result<>{};
                                         });
        generation.readFinal(known.sceneColor).sideEffect(); // son image appartient au mode : effet hors du graphe
        if (previousHistory.valid())
        {
            generation.read(previousHistory);
        }
        if (known.sceneDepth.valid())
        {
            generation.readFinal(known.sceneDepth);
        }
        if (known.motionVectors.valid())
        {
            generation.readFinal(known.motionVectors);
        }
    }

    // ── Copie de l'image courante dans l'historique, après ses lecteurs ──────────────────────────────────────────────
    if (history)
    {
        graph_
            .addPass("history", PassType::kGeneric,
                     [this, next](RenderPassContext &ctx) -> core::Result<>
                     {
                         if (auto r = blit_->copy(ctx.encoder(), sceneTarget_.color(0), blit_->nearestSampler(), history_[next].color(0),
                                                  rhi::Format::kRGBA8Unorm, "history");
                             !r)
                         {
                             return r;
                         }
                         historyValid_ = true;
                         lastHistory_ = next;
                         resetHistoryNextFrame_ = false; // la demande de réinitialisation a été transmise avec un historique neuf
                         return {};
                     })
            .readFinal(known.sceneColor)
            .write(nextHistory);
    }

    // ── Plugins ───────────────────────────────────────────────────────────────────────────────────────────────────────
    RenderGraphBuilder builder(graph_, known, device_.capabilities(), "scene", &featureFrameBindings_);
    for (const auto &feature : features_)
    {
        if (auto r = feature->prepare(frameData_); !r)
        {
            return makeError(r.error().code, "feature '" + feature->name() + "': " + r.error().message, r.error().where);
        }
        if (auto r = feature->declarePasses(builder); !r)
        {
            return makeError(r.error().code, "feature '" + feature->name() + "': " + r.error().message, r.error().where);
        }
    }
    return {};
}

core::Result<> SceneRenderer::endFrame(const UpscaleTarget &output)
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::endFrame: no frame is open");
    }
    frameOpen_ = false;
    generatedFrame_ = {}; // pas de handle d'une image précédente si cette fin d'image échoue avant la génération
    rhi::CommandEncoder &encoder = *encoder_;
    encoder_ = nullptr;
    // Les dessins enregistrés ne survivent pas à l'image, qu'elle réussisse ou non.
    struct ClearDraws
    {
        SceneRenderer &self;
        ~ClearDraws()
        {
            self.draws_.clear();
            self.debugVertices_.clear();
        }
    } clearDraws{*this};

    if (auto r = rebuildFrameGroup(activeLights()); !r)
    {
        return r;
    }
    if (auto r = uploadDebugLines(); !r)
    {
        return r;
    }
    if (auto r = updateFrameData(); !r)
    {
        return r;
    }
    if (auto r = buildGraph(output); !r)
    {
        return r;
    }
    // Les plugins ont pu ajouter des liaisons au groupe 0 (addFrameBinding) : le groupe les suit avant l'exécution.
    if (auto r = rebuildFrameGroup(activeLights()); !r)
    {
        return r;
    }
    return graph_.execute(encoder);
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
