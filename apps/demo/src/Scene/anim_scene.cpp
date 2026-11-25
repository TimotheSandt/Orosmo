#include "Scene/anim_scene.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/renderer/image.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <system_error>
#include <utility>

namespace demo
{

namespace an = orosmo::anim;
namespace as = orosmo::assets;
namespace rd = orosmo::renderer;
using orosmo::core::ErrorCode;
using orosmo::core::makeError;
using orosmo::core::Result;

namespace
{

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kClipSeconds = 2.0f;
constexpr std::uint32_t kClipKeys = 17;
constexpr float kCapHeight = 0.28f;

// Hachage entier déterministe (aucun rand, règle 9) ramené dans [0, 1).
float hash01(std::uint32_t v) noexcept
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFFFFU) / static_cast<float>(0x1000000U);
}

float radiusAt(const AnimFigureDesc &d, float y) noexcept { return d.radius * (1.0f - 0.55f * y / d.height); }

// Os et poids d'un point à la hauteur `y` : mélange linéaire des deux os qui encadrent le segment.
void influence(const AnimFigureDesc &d, float y, glm::u16vec4 &joints, glm::vec4 &weights)
{
    const float segment = d.height / static_cast<float>(d.joints);
    const float t = std::clamp(y / segment, 0.0f, static_cast<float>(d.joints));
    const auto s = std::min(static_cast<std::uint32_t>(t), d.joints - 1);
    const float f = t - static_cast<float>(s);
    const auto next = std::min(s + 1, d.joints - 1);
    joints = glm::u16vec4(static_cast<std::uint16_t>(s), static_cast<std::uint16_t>(next), 0, 0);
    weights = next == s ? glm::vec4(1.0f, 0.0f, 0.0f, 0.0f) : glm::vec4(1.0f - f, f, 0.0f, 0.0f);
}

as::AnimationData swingClip(const char *name, std::uint32_t joints, const glm::vec3 &axis, float amplitude, float phaseStep)
{
    as::AnimationData anim;
    anim.name = name;
    anim.duration = kClipSeconds;
    for (std::uint32_t k = 0; k < joints; ++k)
    {
        as::AnimationChannel ch;
        ch.node = static_cast<int>(k + 1);
        ch.path = as::AnimationPath::kRotation;
        ch.interpolation = as::Interpolation::kLinear;
        ch.componentCount = 4;
        const float a = amplitude * (0.6f + 0.4f * static_cast<float>(k) / static_cast<float>(std::max(joints - 1, 1U)));
        for (std::uint32_t i = 0; i < kClipKeys; ++i)
        {
            const float time = kClipSeconds * static_cast<float>(i) / static_cast<float>(kClipKeys - 1);
            const float angle = a * std::sin(kTwoPi * time / kClipSeconds + phaseStep * static_cast<float>(k));
            const glm::quat q = glm::angleAxis(angle, axis);
            ch.times.push_back(time);
            ch.values.insert(ch.values.end(), {q.x, q.y, q.z, q.w}); // ordre glTF : x, y, z, w
        }
        anim.channels.push_back(std::move(ch));
    }
    return anim;
}

} // namespace

std::size_t animatedFigureVertexCount(const AnimFigureDesc &desc) noexcept
{
    return static_cast<std::size_t>(desc.rings + 1) * (desc.sides + 1) + static_cast<std::size_t>(desc.sides) * 3;
}

as::Model makeAnimatedFigure(const AnimFigureDesc &d, const std::filesystem::path &texture)
{
    as::Model m;
    // Tronc : anneaux de `sides + 1` sommets (couture dupliquée pour les UV), normales radiales.
    as::MeshPrimitive body;
    for (std::uint32_t r = 0; r <= d.rings; ++r)
    {
        const float v = static_cast<float>(r) / static_cast<float>(d.rings);
        const float y = v * d.height;
        const float radius = radiusAt(d, y);
        glm::u16vec4 joints;
        glm::vec4 weights;
        influence(d, y, joints, weights);
        for (std::uint32_t s = 0; s <= d.sides; ++s)
        {
            const float u = static_cast<float>(s) / static_cast<float>(d.sides);
            const float a = kTwoPi * u;
            const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));
            body.positions.emplace_back(n.x * radius, y, n.z * radius);
            body.normals.push_back(n);
            body.uv0.emplace_back(u * 2.0f, v);
            body.joints.push_back(joints);
            body.weights.push_back(weights);
        }
    }
    const std::uint32_t stride = d.sides + 1;
    for (std::uint32_t r = 0; r < d.rings; ++r)
    {
        for (std::uint32_t s = 0; s < d.sides; ++s)
        {
            const std::uint32_t a = r * stride + s;
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + stride;
            const std::uint32_t e = c + 1;
            body.indices.insert(body.indices.end(), {a, c, b, b, c, e}); // sens trigonométrique vu de l'extérieur
        }
    }
    body.material = 0;

    // Coiffe : cône posé sur le dernier anneau, entièrement portée par le dernier os ; un triangle (3 sommets) par côté.
    as::MeshPrimitive cap;
    const float baseRadius = radiusAt(d, d.height);
    const glm::u16vec4 lastJoint(static_cast<std::uint16_t>(d.joints - 1), 0, 0, 0);
    for (std::uint32_t s = 0; s < d.sides; ++s)
    {
        const float a0 = kTwoPi * static_cast<float>(s) / static_cast<float>(d.sides);
        const float a1 = kTwoPi * static_cast<float>(s + 1) / static_cast<float>(d.sides);
        const float am = 0.5f * (a0 + a1);
        const glm::vec3 slope = glm::normalize(glm::vec3(std::cos(am) * kCapHeight, baseRadius, std::sin(am) * kCapHeight));
        const glm::vec3 p0(std::cos(a0) * baseRadius, d.height, std::sin(a0) * baseRadius);
        const glm::vec3 p1(std::cos(a1) * baseRadius, d.height, std::sin(a1) * baseRadius);
        const glm::vec3 apex(0.0f, d.height + kCapHeight, 0.0f);
        const auto first = static_cast<std::uint32_t>(cap.positions.size());
        for (const glm::vec3 &p : {p0, apex, p1})
        {
            cap.positions.push_back(p);
            cap.normals.push_back(slope);
            cap.uv0.emplace_back(0.5f, 0.5f);
            cap.joints.push_back(lastJoint);
            cap.weights.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }
        cap.indices.insert(cap.indices.end(), {first, first + 1, first + 2});
    }
    cap.material = 1;
    m.meshes.push_back({"figure", {std::move(body), std::move(cap)}});

    m.images.push_back({.name = "figure_albedo", .path = texture, .encoded = {}, .mimeType = "image/png"});
    m.textures.push_back({.image = 0, .sampler = -1});
    as::MaterialData skin;
    skin.name = "figure.body";
    skin.baseColor.texture = 0;
    skin.metallicFactor = 0.0f;
    skin.roughnessFactor = 0.6f;
    m.materials.push_back(skin);
    as::MaterialData hat;
    hat.name = "figure.cap";
    hat.baseColorFactor = glm::vec4(0.85f, 0.18f, 0.12f, 1.0f);
    hat.metallicFactor = 0.0f;
    hat.roughnessFactor = 0.4f;
    m.materials.push_back(hat);

    // Nœud 0 : le maillage (transformation ignorée, règle glTF) ; nœuds 1..joints : la chaîne d'os le long de Y.
    const float segment = d.height / static_cast<float>(d.joints);
    m.nodes.resize(d.joints + 1);
    m.nodes[0].name = "figure";
    m.nodes[0].mesh = 0;
    m.nodes[0].skin = 0;
    as::SkinData skinData;
    skinData.name = "spine";
    for (std::uint32_t k = 0; k < d.joints; ++k)
    {
        as::NodeData &node = m.nodes[k + 1];
        node.name = "spine" + std::to_string(k);
        if (k > 0)
        {
            node.parent = static_cast<int>(k);
            node.translation = glm::vec3(0.0f, segment, 0.0f);
            m.nodes[k].children.push_back(static_cast<int>(k + 1));
        }
        skinData.joints.push_back(static_cast<int>(k + 1));
        skinData.inverseBindMatrices.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -segment * static_cast<float>(k), 0.0f)));
    }
    skinData.skeleton = 1;
    m.skins.push_back(std::move(skinData));
    m.animations.push_back(swingClip("sway", d.joints, glm::vec3(0.0f, 0.0f, 1.0f), 0.32f, 0.7f));
    m.animations.push_back(swingClip("wave", d.joints, glm::vec3(1.0f, 0.0f, 0.0f), 0.5f, 1.1f));
    m.scenes.push_back({"figure", {0, 1}});
    m.defaultScene = 0;
    return m;
}

Result<> writeFigureTexture(const std::filesystem::path &path, std::uint32_t variant)
{
    constexpr std::uint32_t kSize = 64;
    // Bandes horizontales (le long du tronc) de deux couleurs, une fine ligne claire entre elles.
    const std::array<std::array<glm::vec3, 2>, 2> palettes{{
        {glm::vec3(0.10f, 0.55f, 0.60f), glm::vec3(0.95f, 0.65f, 0.20f)},
        {glm::vec3(0.55f, 0.20f, 0.70f), glm::vec3(0.30f, 0.85f, 0.35f)},
    }};
    const auto &colors = palettes[variant % palettes.size()];
    rd::Image image;
    image.width = kSize;
    image.height = kSize;
    image.rgba.resize(static_cast<std::size_t>(kSize) * kSize * 4);
    for (std::uint32_t y = 0; y < kSize; ++y)
    {
        const std::uint32_t band = y / 8;
        const glm::vec3 c = (y % 8 == 0) ? glm::vec3(0.92f) : colors[band % 2];
        for (std::uint32_t x = 0; x < kSize; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * kSize + x) * 4;
            image.rgba[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            image.rgba[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            image.rgba[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
            image.rgba[i + 3] = 255;
        }
    }
    return rd::savePng(image, path);
}

AnimScene::~AnimScene() { destroy(); }

Result<> AnimScene::init(orosmo::rhi::Device &device, rd::ShaderLibrary &library, rd::PipelineCache &pipelines, const std::filesystem::path &shaderDirectory,
                         AnimSceneOptions options)
{
    if (options.figure.joints == 0 || options.figure.sides < 3 || options.figure.rings == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "AnimScene: invalid figure description");
    }
    device_ = &device;
    library_ = &library;
    pipelines_ = &pipelines;
    shaderDirectory_ = shaderDirectory;
    options_ = std::move(options);

    std::error_code ec;
    std::filesystem::create_directories(options_.workDirectory, ec);
    if (ec)
    {
        return makeError(ErrorCode::kIo, "AnimScene: cannot create " + options_.workDirectory.string() + ": " + ec.message());
    }
    modelPath_ = options_.workDirectory / "figure.gltf";
    texturePath_ = options_.workDirectory / "figure_albedo.png";
    if (auto written = writeFigureTexture(texturePath_, 0); !written)
    {
        return written;
    }
    // Aller-retour glTF : le modèle dessiné est celui relu par l'importeur, pas celui construit en mémoire.
    if (auto saved = as::saveGltf(makeAnimatedFigure(options_.figure, texturePath_), modelPath_); !saved)
    {
        return makeError(saved.error().code, "AnimScene: saving " + modelPath_.string() + ": " + saved.error().message);
    }
    auto loaded = as::loadGltf(modelPath_);
    if (!loaded)
    {
        return makeError(loaded.error().code, "AnimScene: loading " + modelPath_.string() + ": " + loaded.error().message);
    }
    model_ = std::move(*loaded);
    if (model_.skins.empty() || model_.animations.size() < 2)
    {
        return makeError(ErrorCode::kParse, "AnimScene: the reloaded glTF lost its skin or its animations");
    }

    clips_.clear();
    for (std::size_t i = 0; i < 2; ++i)
    {
        auto clip = an::Clip::fromModel(model_, i);
        if (!clip)
        {
            return std::unexpected(clip.error());
        }
        clips_.push_back(std::move(*clip));
    }
    machine_.emplace(an::restPose(model_));
    for (auto r : {machine_->addState({"sway", &clips_[0], an::LoopMode::kLoop, 1.0f}), machine_->addState({"wave", &clips_[1], an::LoopMode::kLoop, 1.4f}),
                   machine_->addTransition({.from = "sway", .to = "wave", .duration = 0.5f, .condition = "toWave"}),
                   machine_->addTransition({.from = "wave", .to = "sway", .duration = 0.5f, .condition = "toSway"}), machine_->start("sway")})
    {
        if (!r)
        {
            return r;
        }
    }

    textures_ = std::make_unique<as::TextureCache>(device);
    pbrShaders_ = std::make_unique<rd::PbrShaderCache>(library, shaderDirectory);
    auto defaults = rd::PbrDefaults::create(device);
    if (!defaults)
    {
        return std::unexpected(defaults.error());
    }
    defaults_ = *defaults;

    // Agents : grille de colonnes × rangées, phase, vitesse et orientation tirées d'un hachage de l'indice (déterministe).
    const std::uint32_t n = options_.agents;
    const auto columns = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(n) * options_.crowdExtent.x /
                                                                                                    std::max(options_.crowdExtent.y, 0.01f)))));
    const std::uint32_t rows = (n + columns - 1) / columns;
    agentWorlds_.resize(n);
    agentPhase_.resize(n);
    agentSpeed_.resize(n);
    agentPoses_.assign(n, an::restPose(model_));
    for (std::uint32_t i = 0; i < n; ++i)
    {
        const float fx = columns > 1 ? static_cast<float>(i % columns) / static_cast<float>(columns - 1) - 0.5f : 0.0f;
        const float fz = rows > 1 ? static_cast<float>(i / columns) / static_cast<float>(rows - 1) - 0.5f : 0.0f;
        const glm::vec3 jitter((hash01(i * 3U + 1U) - 0.5f) * 0.08f, 0.0f, (hash01(i * 3U + 2U) - 0.5f) * 0.08f);
        const glm::vec3 p = options_.crowdCenter + glm::vec3(fx * options_.crowdExtent.x, 0.0f, fz * options_.crowdExtent.y) + jitter;
        const float yaw = kTwoPi * hash01(i * 7U + 5U);
        agentWorlds_[i] = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), p), yaw, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(options_.agentScale));
        agentPhase_[i] = kClipSeconds * hash01(i * 11U + 3U);
        agentSpeed_[i] = 0.75f + 0.5f * hash01(i * 13U + 9U);
    }
    time_ = 0.0f;
    nextSwitch_ = 3.0f;
    waving_ = false;

    if (auto built = buildModels(); !built)
    {
        return built;
    }
    if (auto posed = update(0.0f); !posed)
    {
        return posed;
    }
    LOG_INFO("Anim scene: ", modelPath_.string(), " (", model_.stats().vertices, " vertices, ", model_.skins[0].joints.size(), " joints, ",
             model_.animations.size(), " clips) ; hero ", skinned_ ? "skinned on the GPU (compute)" : "in rest pose", " ; crowd: ", agentCount(),
             " agents, ", crowdBatchCount(), " draw calls ; edit ", texturePath_.string(), " to reload it live");
    return {};
}

Result<> AnimScene::buildModels()
{
    skinned_.reset();
    crowd_.reset();
    hero_.reset();
    crowdModel_.reset();

    as::RenderModelOptions heroOptions;
    heroOptions.sourceKey = "anim.hero";
    const bool canSkin = an::SkinnedModel::supportedBy(device_->capabilities());
    heroOptions.skinnable = canSkin; // tampon de sommets inscriptible par le compute seulement s'il existe
    heroOptions.extraDefines = options_.modelDefines;
    auto hero = as::RenderModel::create(*device_, model_, *textures_, *pbrShaders_, defaults_, heroOptions);
    if (!hero)
    {
        return makeError(hero.error().code, "AnimScene: hero model: " + hero.error().message);
    }
    hero_ = std::make_unique<as::RenderModel>(std::move(*hero));
    hero_->setRootTransform(glm::scale(glm::translate(glm::mat4(1.0f), options_.heroPosition), glm::vec3(options_.heroScale)));
    if (canSkin)
    {
        auto skinned = an::SkinnedModel::create(*device_, *library_, *pipelines_, shaderDirectory_, model_, *hero_);
        if (!skinned)
        {
            return makeError(skinned.error().code, "AnimScene: skinning: " + skinned.error().message);
        }
        skinned_.emplace(std::move(*skinned));
    }
    else
    {
        LOG_WARNING("Anim scene: no compute skinning on this device (compute and storage buffers required): the hero stays in its rest pose");
    }

    if (options_.agents > 0 && an::CrowdRenderer::supportedBy(device_->capabilities()))
    {
        as::RenderModelOptions crowdOptions;
        crowdOptions.sourceKey = "anim.crowd";
        crowdOptions.extraDefines = options_.crowdDefines;
        auto render = as::RenderModel::create(*device_, model_, *textures_, *pbrShaders_, defaults_, crowdOptions);
        if (!render)
        {
            return makeError(render.error().code, "AnimScene: crowd model: " + render.error().message);
        }
        crowdModel_ = std::make_unique<as::RenderModel>(std::move(*render));
        an::CrowdOptions co;
        co.extraDefines = options_.crowdDefines;
        auto crowd = an::CrowdRenderer::create(*device_, *library_, *pipelines_, shaderDirectory_, model_, *crowdModel_, co);
        if (!crowd)
        {
            return makeError(crowd.error().code, "AnimScene: crowd: " + crowd.error().message);
        }
        crowd_ = std::move(*crowd);
    }
    else if (options_.agents > 0)
    {
        LOG_WARNING("Anim scene: no crowd on this device (storage buffers read by the vertex shader required)");
    }

    const auto &maps = hero_->materials().front()->desc().maps;
    const auto &albedo = maps[static_cast<std::size_t>(rd::PbrMap::kBaseColor)];
    albedoHandle_ = albedo ? albedo->handle() : orosmo::rhi::TextureHandle{};
    return {};
}

Result<> AnimScene::update(float dt)
{
    if (!machine_)
    {
        return makeError(ErrorCode::kInvalidArgument, "AnimScene::update: scene is not initialized");
    }
    time_ += dt;
    // Le personnage alterne entre ses deux clips toutes les 3 s (fondu de 0,5 s), au temps simulé : même suite, même pose.
    if (time_ >= nextSwitch_)
    {
        nextSwitch_ += 3.0f;
        waving_ = !waving_;
        machine_->setTrigger(waving_ ? "toWave" : "toSway");
    }
    if (auto r = machine_->update(dt); !r)
    {
        return r;
    }
    (void)machine_->takeEvents();
    if (skinned_)
    {
        if (auto r = skinned_->setPose(machine_->pose()); !r)
        {
            return r;
        }
    }
    return updateCrowd();
}

Result<> AnimScene::updateCrowd()
{
    if (!crowd_)
    {
        return {};
    }
    for (std::size_t i = 0; i < agentPoses_.size(); ++i)
    {
        // Un agent sur trois agite les bras (« wave »), les autres se balancent (« sway ») ; chacun à sa phase et sa vitesse.
        const an::Clip &clip = clips_[i % 3 == 0 ? 1 : 0];
        const float t = std::fmod(time_ * agentSpeed_[i] + agentPhase_[i], clip.duration());
        if (auto r = clip.sample(t, agentPoses_[i]); !r)
        {
            return r;
        }
    }
    return crowd_->setAgents(agentWorlds_, agentPoses_);
}

Result<> AnimScene::dispatch(orosmo::rhi::CommandEncoder &encoder)
{
    return skinned_ ? skinned_->dispatch(encoder) : Result<>{};
}

Result<> AnimScene::render(rd::SceneRenderer &renderer, const rd::Camera &camera)
{
    if (!hero_)
    {
        return makeError(ErrorCode::kInvalidArgument, "AnimScene::render: scene is not initialized");
    }
    if (auto r = hero_->draw(renderer, camera); !r)
    {
        return r;
    }
    crowdStats_ = {};
    if (crowd_)
    {
        auto drawn = crowd_->draw(renderer, camera.wireframe());
        if (!drawn)
        {
            return std::unexpected(drawn.error());
        }
        crowdStats_ = *drawn;
        if (!statsLogged_)
        {
            statsLogged_ = true;
            LOG_INFO("Anim scene: crowd drawn: ", crowdStats_.instances, " instances (", agentCount(), " agents x ", crowdBatchCount(), " skinned primitives) in ",
                     crowdStats_.drawCalls, " draw calls, ", crowdStats_.paletteBytes, " palette bytes");
        }
    }
    return {};
}

std::size_t AnimScene::reloadChanged()
{
    if (!textures_)
    {
        return 0;
    }
    const std::size_t reloaded = textures_->reloadChanged();
    // Un échec précédent a laissé la scène sans modèles : on retente à chaque modification, et sinon toutes les kRetryFrames
    // images (échec transitoire, fichier inchangé).
    constexpr int kRetryFrames = 120;
    bool retry = false;
    if (rebuildPending_ && reloaded == 0)
    {
        retry = ++retryCountdown_ >= kRetryFrames;
    }
    if (reloaded == 0 && !retry)
    {
        return reloaded;
    }
    retryCountdown_ = 0;
    bool rebuild = rebuildPending_;
    if (!rebuild && hero_)
    {
        const auto &albedo = hero_->materials().front()->desc().maps[static_cast<std::size_t>(rd::PbrMap::kBaseColor)];
        rebuild = albedo && !(albedo->handle() == albedoHandle_);
    }
    if (rebuild)
    {
        // Taille changée : la texture partagée a été recréée ; les groupes des matériaux et des lots de la foule la lient encore.
        device_->waitIdle();
        if (auto rebuilt = buildModels(); !rebuilt)
        {
            rebuildPending_ = true;
            LOG_ERROR(1, "Anim scene: rebuilding the models after the texture reload: ", rebuilt.error().message, " (retried automatically)");
        }
        else
        {
            rebuildPending_ = false;
            if (auto posed = update(0.0f); !posed)
            {
                LOG_ERROR(1, "Anim scene: ", posed.error().message);
            }
        }
    }
    if (reloaded > 0)
    {
        LOG_INFO("Anim scene: reloaded ", texturePath_.string());
    }
    return reloaded;
}

void AnimScene::destroy()
{
    skinned_.reset();
    crowd_.reset();
    hero_.reset();
    crowdModel_.reset();
    textures_.reset();
    defaults_.reset();
    pbrShaders_.reset();
    machine_.reset();
    clips_.clear();
}

} // namespace demo
