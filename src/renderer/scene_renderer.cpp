#include "orosmo/renderer/scene_renderer.hpp"

#include "orosmo/renderer/blend_mode.hpp"
#include "orosmo/renderer/image.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <cstring>
#include <limits>
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
    renderer->pbrShaders_ = std::make_unique<PbrShaderCache>(library, config.shaderDirectory);
    // Anneau des données par image : FrameData à 0 (FrameResources::buffer la lit là), la caméra et les tranches du modèle après,
    // chacune alignée pour un tampon uniforme (décalages dynamiques du groupe 2).
    const std::uint64_t alignment = std::max<std::uint64_t>(device.capabilities().minUniformAlignment, 1);
    renderer->ring_ = FrameRing(rhi::BufferUsage::kUniform | rhi::BufferUsage::kVertex, "scene.frame-ring", std::max(config.framesInFlight, 1u));
    renderer->cameraOffset_ = alignUp(sizeof(FrameData), alignment);
    renderer->modelBase_ = alignUp(renderer->cameraOffset_ + sizeof(CameraData), alignment);
    renderer->modelSliceStride_ = alignUp(Mesh::kModelBlockSize, alignment);
    renderer->slotGroups_.resize(renderer->ring_.framesInFlight());
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
    for (SlotGroups &groups : slotGroups_)
    {
        device_.destroy(groups.frame);
        device_.destroy(groups.model);
    }
}

core::Result<std::unique_ptr<SceneRenderer>> SceneRenderer::create(const RenderContext &context, SceneRendererConfig config)
{
    if (config.shaderDirectory.empty())
    {
        config.shaderDirectory = context.shaderDirectory();
    }
    return create(context.device(), context.library(), context.pipelines(), config);
}

std::vector<Define> SceneRenderer::shaderDefines() const
{
    std::vector<Define> defines;
    for (const std::unique_ptr<RenderFeature> &feature : features_)
    {
        const std::vector<Define> wanted = feature->shaderDefines();
        defines.insert(defines.end(), wanted.begin(), wanted.end());
    }
    const std::vector<Define> aov = aovShaderDefines();
    defines.insert(defines.end(), aov.begin(), aov.end());
    return defines;
}

std::uint64_t SceneRenderer::revision() const
{
    std::vector<Define> defines = shaderDefines();
    if (revision_ == 0 || defines != lastDefines_)
    {
        lastDefines_ = std::move(defines);
        ++revision_;
    }
    return revision_;
}

core::Result<std::shared_ptr<ShaderProgram>> SceneRenderer::resolveProgram(const Mesh &mesh)
{
    if (mesh.program() != nullptr)
    {
        return mesh.program(); // programme imposé (ancien chemin)
    }
    // Révision lue une fois par image (beginFrame) : tous les dessins de l'image ont les mêmes définitions.
    if (mesh.material() != nullptr)
    {
        const std::uint32_t features = mesh.material()->features();
        if (auto found = pbrPrograms_.find(features); found != pbrPrograms_.end())
        {
            return found->second;
        }
        auto program = pbrShaders_->program(features, resolvedDefines_);
        if (!program)
        {
            return std::unexpected(program.error());
        }
        pbrPrograms_.emplace(features, *program);
        return *program;
    }
    // Disposition kColored (MeshBuilder) : position, normale, couleur (trois vec3), sans instance : mesh.vert / mesh.frag.
    const std::vector<rhi::VertexAttribute> &attributes = mesh.vertexAttributes();
    const bool colored = !mesh.instanced() && attributes.size() == 3 &&
                         std::ranges::all_of(attributes, [](const rhi::VertexAttribute &a)
                                             { return a.binding == 0 && a.location < 3 && a.format == rhi::VertexFormat::kFloat3; });
    if (!colored)
    {
        return makeError(ErrorCode::kInvalidArgument,
                         "SceneRenderer::drawMesh: the mesh has no program, no material and not the default (colored) vertex layout");
    }
    if (defaultProgram_ == nullptr)
    {
        if (resolvedDefines_.empty())
        {
            defaultProgram_ = meshProgram_;
        }
        else if (auto found = defaultPrograms_.find(resolvedDefines_); found != defaultPrograms_.end())
        {
            defaultProgram_ = found->second;
        }
        else
        {
            auto program = library_.loadGraphics(shaderDirectory_ / "mesh.vert", shaderDirectory_ / "mesh.frag", resolvedDefines_);
            if (!program)
            {
                return std::unexpected(program.error());
            }
            defaultPrograms_.emplace(resolvedDefines_, *program); // gardé : les pipelines des images en vol le référencent
            defaultProgram_ = *program;
        }
    }
    return defaultProgram_;
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
    if (const std::uint32_t needed = (activeAovs() | feature->requiredAovs()).count() + 1; needed > device_.capabilities().maxColorAttachments)
    {
        return makeError(ErrorCode::kUnsupported, "SceneRenderer::addFeature: '" + feature->name() + "' needs " + std::to_string(needed) +
                                                      " color attachments, the device has " + std::to_string(device_.capabilities().maxColorAttachments));
    }
    features_.push_back(std::move(feature));
    return features_.back().get();
}

void SceneRenderer::removeFeature(const std::string &name)
{
    if (frameOpen_ || executing_)
    {
        // Image en cours : les définitions des programmes (figées à beginFrame) et les liaisons du groupe 0 (déclarées à endFrame)
        // doivent rester celles des mêmes plugins ; le retrait est appliqué à la fin de l'image (EndOfFrame).
        pendingRemovals_.push_back(name);
        return;
    }
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

bool SceneRenderer::needsJitter() const noexcept
{
    return (modeRuns() && activeUpscaleMode_->requirements().needsJitter) ||
           std::any_of(features_.begin(), features_.end(), [](const auto &feature) { return feature->wantsJitter(); }); // REN-19 : TAA
}

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

core::Result<> SceneRenderer::rebuildFrameGroup(LightManager &lights, bool featuresDeclared)
{
    // Un groupe 0 par zone de l'anneau (son tampon change d'une zone à l'autre), refait seulement si une de ses liaisons change.
    // Avant la déclaration des plugins (beginFrame), leurs liaisons ne sont pas encore connues : le groupe de la zone est gardé tel
    // quel (endFrame le refait si elles ont changé avant tout dessin).
    SlotGroups &slot = slotGroups_[ring_.slot()];
    const rhi::BufferHandle buffer = ring_.buffer();
    if (!slot.frame.isNull() && slot.buffer == buffer && slot.lights == lights.buffer().handle() &&
        (!featuresDeclared || sameEntries(slot.features, featureFrameBindings_)))
    {
        frameGroup_ = slot.frame;
        frameResources_.group = frameGroup_;
        return {};
    }
    rhi::BindGroupDesc desc;
    desc.entries.push_back(
        {.binding = 0, .type = rhi::BindingType::kUniformBuffer, .buffer = buffer, .offset = cameraOffset_, .size = sizeof(CameraData)});
    desc.entries.push_back({.binding = 1, .type = rhi::BindingType::kStorageBuffer, .buffer = lights.buffer().handle()});
    desc.entries.push_back({.binding = kFrameDataBinding, .type = rhi::BindingType::kUniformBuffer, .buffer = buffer, .offset = 0, .size = sizeof(FrameData)});
    desc.entries.insert(desc.entries.end(), featureFrameBindings_.begin(), featureFrameBindings_.end());
    desc.debugName = "scene.frame[" + std::to_string(ring_.slot()) + "]";
    auto group = device_.createBindGroup(desc);
    if (!group)
    {
        return std::unexpected(group.error());
    }
    device_.destroy(slot.frame); // destruction différée par le RHI : une image en vol a pu le lier
    slot.frame = *group;
    slot.buffer = buffer;
    slot.lights = lights.buffer().handle();
    slot.features = featureFrameBindings_;
    frameGroup_ = slot.frame;
    frameResources_.group = frameGroup_;
    return {};
}

core::Result<> SceneRenderer::beginFrame(rhi::CommandEncoder &encoder, const FrameOptions &options)
{
    if (frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::beginFrame: the previous frame was not ended");
    }
    if (executing_)
    {
        // Appelé depuis un dessin ou une passe de l'image en cours d'exécution (endFrame) : vider les dessins invaliderait celui
        // qui s'exécute.
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::beginFrame: the previous frame is still executing (called from endFrame)");
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
    aovActive_ = activeAovs();
    if (aovActive_.count() + 1 > device_.capabilities().maxColorAttachments)
    {
        return makeError(ErrorCode::kUnsupported, "SceneRenderer::beginFrame: " + std::to_string(aovActive_.count()) +
                                                      " auxiliary outputs need more color attachments than the device has");
    }
    aovLayout_ = AovLayout(aovActive_);
    info.colorFormats = aovLayout_.colorFormats(sceneColorFormat_); // sans AOV : la couleur seule, comme avant
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
    // Zone de l'anneau de CETTE image (frameSerial_ après incrément) : son tampon existe avant les dessins (groupe 0 complet).
    ring_.beginFrame(frameSerial_ + 1);
    if (auto r = ring_.reserve(device_, modelBase_); !r)
    {
        return std::unexpected(r.error());
    }
    if (auto r = rebuildFrameGroup(activeLights(), false); !r)
    {
        return r;
    }
    frameOptions_ = options;
    prepassThisFrame_ = depthPrepass_;
    draws_.clear();
    debugVertices_.clear();
    ringBytes_.assign(modelBase_, std::byte{0}); // en-tête (FrameData, caméra), rempli à endFrame
    encoder_ = &encoder;
    frameOpen_ = true;
    ++frameSerial_;
    // A75.3 : définitions des programmes résolus au dessin, une fois par image ; une nouvelle révision les fait recompiler.
    if (const std::uint64_t current = revision(); current != resolvedRevision_)
    {
        resolvedRevision_ = current;
        resolvedDefines_ = lastDefines_;
        pbrPrograms_.clear(); // les programmes restent dans pbrShaders_ (images en vol)
        defaultProgram_.reset();
    }
    return {};
}

core::Result<> SceneRenderer::setCamera(Camera &camera)
{
    if (blit_ == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::setCamera: not created");
    }
    camera.setJitter(currentJitter_);
    cameraWireframe_ = camera.wireframe();
    cameraOrigin_ = cameraRelative_ ? camera.position() : glm::vec3(0.0f);
    // Données par image (REN-03) : mêmes matrices que le tampon de la caméra.
    cameraView_ = cameraRelative_ ? camera.relativeViewMatrix() : camera.viewMatrix();
    cameraProjection_ = camera.projectionMatrix();
    cameraWorld_ = glm::dvec3(camera.position());
    cameraData_ = camera.data(cameraRelative_); // envoyée avec les données de l'image (endFrame), dans sa zone de l'anneau
    return {};
}

core::Result<> SceneRenderer::record(Mesh &mesh, const ShaderProgram &drawProgram, bool lines, std::uint32_t modelOffset)
{
    GraphicsState state;
    state.vertexBuffers = mesh.vertexBuffers();
    state.vertexAttributes = mesh.vertexAttributes();
    state.raster.cull = lines ? rhi::CullMode::kNone : mesh.cullMode();
    state.raster.frontFace = mesh.frontFace(); // miroir : sens horaire (dernier prepareTransform, celui de CE dessin)
    state.raster.fill = lines ? rhi::FillMode::kLine : rhi::FillMode::kFill;
    // Reverse-Z (REN-02) : le plus proche a la plus GRANDE profondeur. Le passage fil de fer redessine la même géométrie : à
    // profondeur égale, ses arêtes doivent passer.
    state.depth = {true, true, lines ? rhi::CompareOp::kGreaterEqual : rhi::CompareOp::kGreater};
    // Pré-passe (REN-04) : la scène retrouve la profondeur de la pré-passe (égale) sans la réécrire. Seuls les matériaux qui
    // écrivent la profondeur y sont (opaques, masqués : REN-41) ; un transparent n'y écrirait pas sa profondeur, il cacherait ce
    // qui est derrière lui. Il teste quand même la profondeur de la pré-passe, sans l'écrire.
    const bool prepass = prepassThisFrame_ && !lines && blendModeState(mesh.aovMaterial().blend).depthWrite;
    if (prepass)
    {
        state.depth = {true, false, rhi::CompareOp::kGreaterEqual};
    }
    state.depthFormat = rhi::Format::kD32Float;
    // Mode de debug (REN-35) : un maillage texturé (uv à l'emplacement 2, groupe 1 de textures) prend la variante texturée.
    const bool textured = !mesh.textureGroup().isNull() &&
                          std::ranges::any_of(mesh.vertexAttributes(), [](const rhi::VertexAttribute &a)
                                              { return a.location == 2 && a.binding == 0 && a.format == rhi::VertexFormat::kFloat2; });
    const ShaderProgram &program = !replacesMeshProgram() || !debugViewProgram_ ? drawProgram
                                   : textured && debugViewTexturedProgram_     ? *debugViewTexturedProgram_
                                                                               : *debugViewProgram_;
    // REN-21 / REN-41 : un AOV n'est écrit que s'il est actif, si le matériau le veut (un transparent n'en écrit aucun par défaut) et
    // si le programme a la sortie ; le passage filaire n'en écrit jamais. Sans AOV, c'est la couleur seule, comme avant.
    aovColorTargets(state.colorTargets, aovLayout_, frameColorFormat(), lines ? AovSet{} : aovWrites(mesh.aovMaterial(), aovActive_),
                    program.reflection().colorOutputs);
    configureMaterialState(state, mesh.aovMaterial().blend, lines);
    state.debugName = program.name() + (lines ? ".lines" : "");
    auto pipeline = pipelines_.graphics(program, state);
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
        auto prepassPipeline = pipelines_.graphics(drawProgram, depthOnly);
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
    draw.modelOffset = modelOffset;
    draw.vertexBuffer = mesh.vertexBuffer();
    draw.instanceBuffer = mesh.instanced() ? mesh.instanceBuffer() : rhi::BufferHandle{};
    draw.indexBuffer = mesh.indexBuffer();
    draw.count = mesh.indexCount();
    draw.instanceCount = mesh.instanceCount();
    draws_.push_back(std::move(draw));
    return {};
}

// Ancienne signature : comportement d'avant M8 (programme imposé obligatoire), fil de fer de la caméra donnée.
core::Result<> SceneRenderer::drawMesh(Mesh &mesh, const Camera &camera) { return drawMeshWith(mesh, camera.wireframe() || wireframe_, false); }

core::Result<> SceneRenderer::drawMesh(Mesh &mesh) { return drawMeshWith(mesh, cameraWireframe_ || wireframe_, true); }

core::Result<> SceneRenderer::drawMeshWith(Mesh &mesh, bool wireframe, bool resolve)
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawMesh: no frame is open");
    }
    if (!mesh.valid() || (!resolve && mesh.program() == nullptr))
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawMesh: the mesh is empty or has no graphics program");
    }
    auto program = resolveProgram(mesh);
    if (!program)
    {
        return std::unexpected(program.error());
    }
    if (*program == nullptr || (*program)->isCompute())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::drawMesh: the mesh is empty or has no graphics program");
    }
    // Vecteurs de mouvement : l'historique du modèle n'est suivi que si l'AOV existe (sinon l'envoi est celui d'avant).
    const TransformHistory history = aovActive_.has(Aov::kMotion)
                                               ? TransformHistory{frameIndex_ + 1, hasPreviousFrame_ ? previousCameraOrigin_ : cameraOrigin_}
                                               : TransformHistory{};
    // Bloc calculé côté CPU seulement : la tranche de l'image (anneau) le porte au GPU ; le tampon propre du maillage n'est plus
    // réécrit à chaque image (Mesh::uploadTransform le met à jour pour qui le lit lui-même).
    if (auto r = mesh.prepareTransform(cameraOrigin_, history); !r)
    {
        return r;
    }
    // Transformation de CE dessin, copiée maintenant : le maillage peut être redessiné ailleurs dans l'image (REN-01).
    auto slice = recordModelSlice(mesh);
    if (!slice)
    {
        return std::unexpected(slice.error());
    }
    if (auto r = record(mesh, **program, false, *slice); !r)
    {
        return r;
    }
    return wireframe || debugView_ == DebugView::kWireframe ? record(mesh, **program, true, *slice) : core::Result<>{};
}

core::Result<std::uint32_t> SceneRenderer::recordModelSlice(const Mesh &mesh)
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::recordModelSlice: no frame is open");
    }
    // Décalage dans la zone de l'anneau de l'image (après l'en-tête FrameData + caméra) : c'est directement le décalage dynamique
    // du groupe 2.
    const std::size_t offset = ringBytes_.size();
    if (offset + modelSliceStride_ > std::numeric_limits<std::uint32_t>::max())
    {
        return makeError(ErrorCode::kOutOfRange, "SceneRenderer::recordModelSlice: too many draws in one frame");
    }
    ringBytes_.resize(offset + modelSliceStride_, std::byte{0});
    const std::span<const std::byte> block = mesh.modelBlock();
    std::memcpy(ringBytes_.data() + offset, block.data(), block.size());
    return static_cast<std::uint32_t>(offset);
}

core::Result<> SceneRenderer::uploadFrameRing()
{
    // En-tête : FrameData (calculé par updateFrameData) et la dernière caméra donnée.
    std::memcpy(ringBytes_.data(), &frameData_, sizeof(FrameData));
    std::memcpy(ringBytes_.data() + cameraOffset_, &cameraData_, sizeof(CameraData));
    // Lignes de debug de l'image, à la suite (sommets : même tampon, usage kVertex).
    debugBase_ = alignUp(ringBytes_.size(), 256);
    if (!debugVertices_.empty())
    {
        const std::span<const std::byte> lines = std::as_bytes(std::span(debugVertices_));
        ringBytes_.resize(debugBase_ + lines.size(), std::byte{0});
        std::memcpy(ringBytes_.data() + debugBase_, lines.data(), lines.size());
    }
    if (auto r = ring_.reserve(device_, ringBytes_.size()); !r)
    {
        return std::unexpected(r.error());
    }
    // Groupe 2 (tranches du modèle) de la zone : refait seulement quand son tampon change (création, croissance).
    SlotGroups &slot = slotGroups_[ring_.slot()];
    if (slot.model.isNull() || slot.modelBuffer != ring_.buffer())
    {
        rhi::BindGroupDesc group;
        group.entries.push_back({.binding = 0,
                                 .type = rhi::BindingType::kUniformBuffer,
                                 .buffer = ring_.buffer(),
                                 .size = Mesh::kModelBlockSize,
                                 .dynamicOffset = true});
        group.debugName = "scene.model-slices[" + std::to_string(ring_.slot()) + "]";
        auto created = device_.createBindGroup(group);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        device_.destroy(slot.model); // destruction différée par le RHI : une image en vol a pu le lier
        slot.model = *created;
        slot.modelBuffer = ring_.buffer();
    }
    modelSliceGroup_ = slot.model;
    frameResources_.modelGroup = modelSliceGroup_;
    frameResources_.buffer = ring_.buffer();
    // Une seule écriture pour toute l'image, dans une zone que plus aucune image en vol ne lit.
    return ring_.write(0, std::span<const std::byte>(ringBytes_));
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
    aovColorTargets(state.colorTargets, aovLayout_, frameColorFormat(), AovSet{}, 1); // les lignes ne touchent que la couleur
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

core::Result<> SceneRenderer::playDraws(rhi::CommandEncoder &e, bool prepass)
{
    for (const SceneDraw &draw : draws_)
    {
        if (prepass && draw.prepassPipeline.isNull())
        {
            continue; // dessin externe, lignes, fil de fer : seulement dans la passe de scène
        }
        // Les lumières ont pu changer pendant l'image (setLights après beginFrame) : le groupe suit avant chaque dessin.
        if (auto r = rebuildFrameGroup(*draw.lights, true); !r)
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
            e.setVertexBuffer(0, ring_.buffer(), debugBase_ + draw.vertexOffset);
            e.draw(draw.count);
            continue;
        }
        if (!draw.textureGroup.isNull())
        {
            e.setBindGroup(1, draw.textureGroup);
        }
        const std::uint32_t modelOffset = draw.modelOffset;
        e.setBindGroup(2, modelSliceGroup_, std::span(&modelOffset, 1)); // la tranche de CE dessin
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
    previousCameraOrigin_ = cameraOrigin_;
    previousJitter_ = currentJitter_;
    hasPreviousFrame_ = true;
    ++frameIndex_;
    return {}; // envoyé avec le reste de l'image par uploadFrameRing
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

bool SceneRenderer::outputSampled(rhi::TextureHandle texture) const
{
    // A63 : un plugin peut échantillonner la couleur de scène ; quand c'est la sortie, elle doit l'accepter (revue CodeRabbit PR #12).
    const auto desc = device_.textureDesc(texture);
    return desc && rhi::hasFlag(desc->usage, rhi::TextureUsage::kSampled);
}

core::Result<> SceneRenderer::buildGraph(const UpscaleTarget &output, bool hadHistory)
{
    graph_.reset();
    const rhi::Extent2D renderSize{static_cast<std::uint32_t>(frameWidth_), static_cast<std::uint32_t>(frameHeight_)};
    RenderGraphBuilder::Resources known;
    known.renderSize = renderSize;
    known.outputSize = output.size;
    // A63 : la scène va droit dans la sortie quand la copie 1:1 serait la seule chose entre les deux (voir setRenderIntoOutput).
    // La couleur de scène et la sortie sont alors UNE ressource du graphe : l'ordre des passes qui écrivent la sortie est gardé.
    const bool intoOutput = renderIntoOutput_ && !output.texture.isNull() && !scaled() && !modeRuns() && !needsHistory() && frameGenMode_ == nullptr &&
                            postProcessPasses_.empty() && aovLayout_.set().list().empty() && output.format == sceneTarget_.info().colorFormats[0] &&
                            output.size.width == renderSize.width && output.size.height == renderSize.height &&
                            std::ranges::none_of(features_, [](const auto &f) { return f->postProcessesScene(); }) && outputSampled(output.texture);
    renderedIntoOutput_ = intoOutput;
    if (intoOutput)
    {
        known.output = graph_.importTexture("output", output.texture, {.format = output.format, .size = output.size});
        known.sceneColor = known.output;
    }
    else
    {
        known.sceneColor = graph_.importTexture("scene.color", sceneTarget_.color(0), {.format = sceneTarget_.info().colorFormats[0], .size = renderSize});
    }
    for (const Aov aov : aovLayout_.set().list())
    {
        const AovInfo &info = aovInfo(aov);
        const RgTexture texture = graph_.importTexture(aov == Aov::kMotion ? std::string("scene.motion") : "scene.aov." + std::string(info.name),
                                                       sceneTarget_.color(*aovLayout_.index(aov)), {.format = info.format, .size = renderSize});
        known.aovs[static_cast<std::size_t>(aov)] = texture;
        if (aov == Aov::kMotion)
        {
            known.motionVectors = texture;
        }
    }
    if (!sceneTarget_.depth().isNull())
    {
        known.sceneDepth = graph_.importTexture("scene.depth", sceneTarget_.depth(), {.format = rhi::Format::kD32Float, .size = renderSize});
    }
    if (!output.texture.isNull() && !intoOutput)
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
        for (const Aov aov : aovLayout_.set().list()) // mêmes attachements que la scène ; la pré-passe n'écrit AUCUN AOV (masque nul)
        {
            depth.writeColor(known.aovs[static_cast<std::size_t>(aov)], rhi::LoadOp::kClear, aovInfo(aov).clear);
        }
        depth.writeDepth(known.sceneDepth, frameOptions_.clearDepth ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, 0.0f);
    }

    // ── Passe de scène : joue les dessins enregistrés ────────────────────────────────────────────────────────────────
    {
        auto scene = graph_.addPass("scene", PassType::kRaster, [this](RenderPassContext &ctx) { return playDraws(ctx.encoder()); });
        // Surdessin : le comptage s'additionne sur un fond noir, quelle que soit la couleur de fond demandée.
        scene.writeColor(known.sceneColor, rhi::LoadOp::kClear,
                         debugView_ == DebugView::kOverdraw ? std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f} : frameOptions_.clearColor);
        for (const Aov aov : aovLayout_.set().list()) // les AOV sont effacés à leur valeur « rien » (aov.hpp), pas à la couleur de fond
        {
            scene.writeColor(known.aovs[static_cast<std::size_t>(aov)], rhi::LoadOp::kClear, aovInfo(aov).clear);
        }
        if (known.sceneDepth.valid())
        {
            const bool clear = frameOptions_.clearDepth && !prepass; // après la pré-passe : sa profondeur est gardée
            scene.writeDepth(known.sceneDepth, clear ? rhi::LoadOp::kClear : rhi::LoadOp::kLoad, 0.0f); // reverse-Z : 0 au loin
        }
    }

    // ── Historique en double tampon : lecture de l'image précédente, copie de l'image courante dans l'autre tampon ──────
    // L'historique n'est « l'image précédente » que s'il a été écrit avec succès à l'image précédente, à la même taille.
    // `hadHistory` est l'état lu par endFrame AVANT toute opération qui peut échouer ; historyValid_ est déjà faux : seule une copie
    // réussie suivie d'une exécution réussie du graphe le revalide (un échec le laisse invalide, de même qu'une image qui n'en a
    // pas besoin).
    const bool history = needsHistory();
    bool previousValid = false;
    RgTexture previousHistory;
    RgTexture nextHistory;
    const std::uint32_t next = hadHistory ? (lastHistory_ ^ 1u) : lastHistory_;
    if (history)
    {
        bool fresh = false;
        if (hadHistory)
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
    const bool resetHistory = resetHistoryNextFrame_ || !previousValid;
    const rhi::TextureHandle previous = previousValid ? history_[lastHistory_].color(0) : rhi::TextureHandle{};

    // ── Mise à l'échelle (ou copie 1:1) vers la sortie ────────────────────────────────────────────────────────────────
    if (!intoOutput && (modeRuns() || !scaled()))
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
                    input.motionVectors = aovLayout_.set().has(Aov::kMotion) ? sceneTarget_.color(1) : rhi::TextureHandle{};
                    input.historyColor = previous;
                    input.renderResolution = {static_cast<float>(frameWidth_), static_cast<float>(frameHeight_)};
                    // Taille de la texture de sortie (celle que le mode écrit), comme l'adaptateur `upscale()` : elle peut différer
                    // de `setOutputResolution` (`endFrame(rhi::TextureHandle)` étire la scène sur la texture).
                    input.outputResolution = {static_cast<float>(output.size.width), static_cast<float>(output.size.height)};
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
                                         [this, output, previous, resetHistory](RenderPassContext &ctx) -> core::Result<>
                                         {
                                             FrameContext context{device_, ctx.encoder(), *blit_};
                                             FrameGenerationInput input;
                                             input.currentColor = sceneTarget_.color(0);
                                             input.previousColor = previous;
                                             input.depth = sceneTarget_.depth();
                                             input.motionVectors =
                                                 aovLayout_.set().has(Aov::kMotion) ? sceneTarget_.color(1) : rhi::TextureHandle{};
                                             input.renderResolution = {static_cast<float>(frameWidth_), static_cast<float>(frameHeight_)};
                                             input.outputResolution = {static_cast<float>(output.size.width), static_cast<float>(output.size.height)};
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
                         historyWritten_ = true; // publié par endFrame seulement si tout le graphe réussit
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

    // ── Post-traitements (IPostProcessPass) : modifient la cible de la scène, dans l'ordre d'ajout ────────────────────
    // Déclarés APRÈS les plugins : les écrivains de la scène s'exécutent dans leur ordre de déclaration, donc les post-traitements
    // voient la passe principale complète (3D opaque, transparents, 2D du monde : docs/03 §11) et les effets des plugins. La mise à
    // l'échelle, la génération d'images et l'historique, déclarés avant, lisent la scène avec readFinal : ils passent après eux.
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
    return {};
}

core::Result<> SceneRenderer::endFrame(rhi::TextureHandle output)
{
    auto desc = device_.textureDesc(output);
    if (!desc)
    {
        // L'image ouverte est fermée comme par un endFrame qui échoue : les dessins ne passent pas à l'image suivante.
        return endFrame(UpscaleTarget{output, rhi::Format::kRGBA8Unorm, {}});
    }
    return endFrame(UpscaleTarget{output, desc->format, {desc->size.width, desc->size.height}});
}

core::Result<> SceneRenderer::endFrame(const UpscaleTarget &output)
{
    if (!frameOpen_)
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::endFrame: no frame is open");
    }
    // Cycle de vie : enregistrement (frameOpen_) -> exécution (executing_ : beginFrame refusé) -> disponible, après le nettoyage
    // ci-dessous, que l'image réussisse ou non.
    frameOpen_ = false;
    executing_ = true;
    generatedFrame_ = {}; // pas de handle d'une image précédente si cette fin d'image échoue avant la génération
    rhi::CommandEncoder &encoder = *encoder_;
    encoder_ = nullptr;
    // Historique (REN-19) : l'état de l'image précédente est lu avant toute opération qui peut échouer, puis invalidé ; il n'est
    // republié qu'après la réussite de la copie ET du graphe entier.
    const bool hadHistory = historyValid_;
    historyValid_ = false;
    historyWritten_ = false;
    // Les dessins enregistrés et les dépôts des plugins ne survivent pas à l'image, qu'elle réussisse ou non.
    struct EndOfFrame
    {
        SceneRenderer &self;
        ~EndOfFrame()
        {
            self.draws_.clear();
            self.debugVertices_.clear();
            self.ringBytes_.clear();
            for (const auto &feature : self.features_)
            {
                feature->frameEnded();
            }
            // Un plugin a pu détruire dans frameEnded un tampon qu'il liait : ses liaisons ne servent pas à l'image suivante, qui
            // les redéclare (revue CodeRabbit PR #12).
            self.featureFrameBindings_.clear();
            self.executing_ = false;
            for (const std::string &name : std::exchange(self.pendingRemovals_, {}))
            {
                self.removeFeature(name); // retraits demandés pendant l'image
            }
        }
    } endOfFrame{*this};

    // Sortie nulle (ou texture inconnue de `endFrame(rhi::TextureHandle)`) : refusée avant le graphe, quel que soit le mode (à
    // échelle réduite sans mode, aucune passe ne la lirait et l'image passerait pour réussie). endOfFrame ferme l'image.
    if (output.texture.isNull())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::endFrame: the output texture is null or unknown");
    }
    if (auto r = updateFrameData(); !r)
    {
        return r;
    }
    if (auto r = uploadFrameRing(); !r)
    {
        return r;
    }
    if (auto r = rebuildFrameGroup(activeLights(), false); !r) // le tampon de la zone a pu grandir
    {
        return r;
    }
    if (auto r = buildGraph(output, hadHistory); !r)
    {
        return r;
    }
    // Les plugins ont pu ajouter des liaisons au groupe 0 (addFrameBinding) : le groupe les suit avant l'exécution.
    if (auto r = rebuildFrameGroup(activeLights(), true); !r)
    {
        return r;
    }
    // Les liaisons des plugins (clusters du forward+, REN-04) sont calculées pour les lumières actives à endFrame : un dessin
    // enregistré avec un autre LightManager lirait des listes d'indices d'un autre tampon. Refusé plutôt que rendu faux.
    if (!featureFrameBindings_.empty())
    {
        LightManager *frameLights = &activeLights();
        for (const SceneDraw &draw : draws_)
        {
            if (draw.lights != frameLights)
            {
                return makeError(ErrorCode::kUnsupported, "SceneRenderer::endFrame: plugin frame bindings (forward+ clusters) are built for the lights "
                                                          "active at endFrame; a draw was recorded with another LightManager");
            }
        }
    }
    if (auto r = graph_.execute(encoder); !r)
    {
        return r;
    }
    historyValid_ = historyWritten_;
    return {};
}

core::Result<> SceneRenderer::saveColorPng(const std::filesystem::path &path) const
{
    if (!sceneTarget_.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "SceneRenderer::saveColorPng: nothing was rendered yet");
    }
    if (renderedIntoOutput_)
    {
        return makeError(ErrorCode::kUnsupported, "SceneRenderer::saveColorPng: the last frame was drawn into the output (setRenderIntoOutput), "
                                                  "the scene target was not written");
    }
    return savePng(device_, sceneTarget_.color(0), path);
}

} // namespace orosmo::renderer
