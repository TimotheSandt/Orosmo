#include "viewer.hpp"

#include "TerrainPanel.gen.h"

#include <engine/platform/input_manager.hpp>
#include <engine/renderer/image.hpp>
#include <engine/ui/window_input.hpp>
#include <engine/world/export.hpp>

#include "Logger.h"

#include <algorithm>
#include <cmath>

namespace rd = engine::renderer;
namespace rhi = engine::rhi;
namespace ui = engine::ui;
namespace w = engine::world;
using engine::core::ErrorCode;
using engine::core::makeError;

namespace
{
constexpr float kFixedStep = 1.0f / 60.0f;
constexpr std::size_t kFrameHistory = 120;
} // namespace

Viewer::Viewer(ViewerOptions options) : options_(std::move(options)), window_("Terrain viewer", options_.width, options_.height) {}

Viewer::~Viewer()
{
    if (device_)
    {
        device_->waitIdle();
    }
    streamer_.reset();
    ui_.reset();
    font_.reset();
    composed_.reset();
    terrain_.reset();
    canvas_.reset();
    scene_.reset();
    pipelines_.reset();
    shaders_.reset();
    lights_ = {};
    device_.reset();
    window_.Close();
}

engine::core::Result<std::unique_ptr<Viewer>> Viewer::create(const ViewerOptions &options)
{
    std::unique_ptr<Viewer> viewer(new Viewer(options));
    if (auto r = viewer->init(); !r)
    {
        return std::unexpected(r.error());
    }
    return viewer;
}

engine::core::Result<> Viewer::init()
{
    const auto &res = options_.resources;
    const auto shaderDir = res / "shader" / "rhi";

    rhi::DeviceDesc desc;
    desc.backend = rhi::Backend::kOpenGL;
    desc.validation = true;
    if (options_.headless)
    {
        desc.surface.size = {static_cast<std::uint32_t>(options_.width), static_cast<std::uint32_t>(options_.height)};
    }
    else
    {
        if (window_.Init([&] { (void)rhi::prepareWindowHints(desc); }) != 0)
        {
            return makeError(ErrorCode::kUnsupported, "cannot open a window (no display?); try --headless");
        }
        desc.surface.nativeWindow = window_.GetWindow();
    }
    auto device = rhi::createDevice(desc);
    if (!device)
    {
        return makeError(device.error().code, "graphics device: " + device.error().message);
    }
    device_ = std::move(*device);
    LOG_INFO("Viewer: ", device_->capabilities().deviceName, " (", device_->capabilities().driverVersion, ")");

    shaders_ = std::make_unique<rd::ShaderLibrary>(*device_);
    pipelines_ = std::make_unique<rd::PipelineCache>(*device_);
    auto scene = rd::SceneRenderer::create(*device_, *shaders_, *pipelines_, {.shaderDirectory = shaderDir});
    if (!scene)
        return makeError(scene.error().code, "scene renderer: " + scene.error().message);
    scene_ = std::move(*scene);
    auto canvas = rd::CanvasRenderer::create(*device_, *shaders_, *pipelines_, shaderDir);
    if (!canvas)
        return makeError(canvas.error().code, "2D renderer: " + canvas.error().message);
    canvas_ = std::move(*canvas);
    auto terrain = rd::TerrainRenderer::create(*device_, *shaders_, shaderDir);
    if (!terrain)
        return makeError(terrain.error().code, "terrain renderer: " + terrain.error().message);
    terrain_ = std::move(*terrain);
    auto lights = rd::LightManager::create(*device_);
    if (!lights)
        return makeError(lights.error().code, "lights: " + lights.error().message);
    lights_ = std::move(*lights);
    lights_.setAmbientLight({0.62f, 0.68f, 0.78f}, 0.45f);
    // Direction VERS le soleil (shaders : dot(normale, direction)).
    lights_.addLight(rd::directionalLight(glm::normalize(glm::vec3(0.45f, 1.0f, 0.3f)), {1.0f, 0.95f, 0.85f}, 0.9f));
    if (auto r = lights_.upload(); !r)
        return r;
    scene_->setLights(&lights_);

    // Interface : police FreeType + MSDF existante, thème JSON, vue compilée depuis TerrainPanel.cui.
    if (!text_.init(static_cast<unsigned>(options_.width), static_cast<unsigned>(options_.height)) ||
        !text_.loadFont((res / "fonts" / "Roboto-Regular.ttf").string(), "default", 48))
    {
        return makeError(ErrorCode::kNotFound, "font: " + (res / "fonts" / "Roboto-Regular.ttf").generic_string());
    }
    text_.setActiveFont("default");
    font_ = std::make_unique<ui::TextRendererFont>(text_);
    ui_ = std::make_unique<ui::Ui>(*font_);
    if (!options_.headless)
        ui::useSystemClipboard(*ui_, window_); // window_ est déclarée avant ui_ : elle vit plus longtemps

    state_.seed = options_.seed;
    state_.loadPresets(res / "presets");
    if (!options_.preset.empty())
    {
        if (!state_.selectPreset(options_.preset))
            return makeError(ErrorCode::kNotFound, "unknown preset '" + options_.preset + "'");
        state_.params = state_.presets[static_cast<std::size_t>(state_.preset)];
    }
    const auto themes = state_.themeNames();
    const auto theme = std::find(themes.begin(), themes.end(), options_.theme);
    if (theme == themes.end())
        return makeError(ErrorCode::kInvalidArgument, "unknown theme '" + options_.theme + "' (dark, light, framed)");
    state_.theme = static_cast<int>(theme - themes.begin());
    loadTheme();
    ui_->themes().watch(watcher_);
    ui_->setRoot(cui::buildTerrainPanel(*ui_, state_, stats_));
    for (const std::string &missing : ui_->actions().missing())
        LOG_WARNING("Viewer: the panel names an unregistered action: ", missing);

    restartStreaming();
    const float ground = w::heightAt(state_.params, state_.seed, 0.0f, 0.0f);
    camera_.setPosition({0.0f, ground + 0.4f * state_.params.heightScale + 40.0f, 0.0f});
    camera_.setOrientation(glm::normalize(glm::vec3(0.0f, -0.35f, -1.0f)));
    camera_.setFovDegrees(70.0f);
    camera_.setNearPlane(0.5f);
    return {};
}

void Viewer::loadTheme()
{
    const std::string name = state_.themeNames()[static_cast<std::size_t>(std::clamp(state_.theme, 0, static_cast<int>(state_.themeNames().size()) - 1))];
    const auto path = options_.resources / "themes" / (name + ".json");
    if (auto r = ui_->themes().load(path); !r)
    {
        state_.status = r.error().message;
        LOG_ERROR(1, "Viewer: theme: ", r.error().message);
        return;
    }
    ui_->themes().watch(watcher_);
    state_.status = "Theme " + name;
}

void Viewer::restartStreaming()
{
    // Nouveaux paramètres ou nouvelle graine : on repart de zéro (la tâche en cours se termine dans le destructeur).
    streamer_.reset();
    terrain_->retainOnly({});
    shown_.clear();
    w::LodConfig lod;
    w::StreamingConfig config;
    config.memoryBudgetBytes = 192u * 1024u * 1024u;
    streamer_ = std::make_unique<w::TileStreamer>(state_.params, state_.seed, lod, config, tasks_);
}

// Le jeu de tuiles affiché change d'un coup, quand toute la nouvelle sélection est prête : jamais de trou ni de recouvrement
// entre deux niveaux de détail pendant la génération.
void Viewer::syncTiles()
{
    const auto &selection = streamer_->selection();
    std::vector<const w::ResidentTile *> ready;
    ready.reserve(selection.size());
    for (const w::TileKey &key : selection)
    {
        const w::ResidentTile *tile = streamer_->find(key);
        if (tile == nullptr)
        {
            return;
        }
        ready.push_back(tile);
    }
    std::vector<std::uint64_t> ids;
    ids.reserve(ready.size());
    for (std::size_t i = 0; i < ready.size(); ++i)
    {
        const w::TileKey &key = selection[i];
        const auto id = static_cast<std::uint64_t>(w::TileKeyHash{}(key));
        ids.push_back(id);
        auto it = shown_.find(id);
        if (it != shown_.end() && it->second == ready[i])
        {
            continue;
        }
        const w::TileMesh &mesh = ready[i]->mesh;
        const glm::vec3 origin{w::sampleX(state_.params, key, 0), 0.0f, w::sampleZ(state_.params, key, 0)};
        std::vector<float> relative = mesh.vertices;
        for (std::size_t v = 0; v < relative.size(); v += w::TileMesh::kFloatsPerVertex)
        {
            relative[v] -= origin.x;
            relative[v + 2] -= origin.z;
        }
        if (auto r = terrain_->updateTile({.id = id, .origin = origin, .vertices = relative, .indices = mesh.indices}); !r)
        {
            LOG_ERROR(1, "Viewer: tile upload: ", r.error().message);
            continue;
        }
        shown_[id] = ready[i];
    }
    terrain_->retainOnly(ids);
    std::erase_if(shown_, [&](const auto &entry) { return std::find(ids.begin(), ids.end(), entry.first) == ids.end(); });
}

ui::InputFrame Viewer::readInput(float deltaTime)
{
    if (options_.headless)
    {
        ui::InputFrame in;
        in.deltaTime = deltaTime;
        return in;
    }
    return ui::readWindowInput(window_, deltaTime); // touches répétées, raccourcis (Ctrl+A / C / X / V / Z dans les champs)
}

// Caméra libre (mêmes touches que la démo) : clic droit pour tourner (hors de l'interface) ; ZQSD en AZERTY, WASD sinon, et les
// flèches pour avancer ; Espace / Maj pour monter et descendre ; A (Q en QWERTY) plus vite, E plus lentement ; la vitesse suit
// l'altitude au-dessus du sol.
void Viewer::moveCamera(float deltaTime)
{
    if (options_.headless)
    {
        return;
    }
    InputManager &input = InputManager::GetInstance(window_.GetWindow());
    const glm::dvec2 cursor = window_.GetCursorPosition();
    if (input.IsMouseButtonJustPressed(MouseButton::RIGHT) && !ui_->wantsMouse())
    {
        rotating_ = true;
    }
    if (!input.IsMouseButtonPressed(MouseButton::RIGHT) && !input.IsMouseButtonJustPressed(MouseButton::RIGHT))
    {
        rotating_ = false;
    }
    if (rotating_)
    {
        const glm::dvec2 d = cursor - lastCursor_;
        camera_.rotateYaw(static_cast<float>(-d.x) * 0.15f);
        camera_.rotatePitch(static_cast<float>(-d.y) * 0.15f);
    }
    lastCursor_ = cursor;
    if (ui_->wantsKeyboard())
    {
        return;
    }
    // Touches selon la disposition détectée (une seule à la fois) : les mêmes positions physiques que ZQSD en AZERTY.
    const bool azerty = InputManager::GetKeyLayout() == KeyLayout::AZERTY;
    const KeyButton forward = azerty ? KeyButton::Z : KeyButton::W;
    const KeyButton left = azerty ? KeyButton::Q : KeyButton::A;
    const KeyButton faster = azerty ? KeyButton::A : KeyButton::Q; // à gauche de l'avance
    const KeyButton slower = KeyButton::E;                         // à droite de l'avance
    const glm::vec3 p = camera_.position();
    const float ground = w::heightAt(state_.params, state_.seed, p.x, p.z);
    float speed = std::clamp(std::abs(p.y - ground), 10.0f, 2000.0f) * 0.8f;
    if (input.IsKeyPressed(faster))
        speed *= 4.0f;
    else if (input.IsKeyPressed(slower))
        speed *= 0.25f;
    const float step = speed * deltaTime;
    auto held = [&](KeyButton a, KeyButton b) { return input.IsKeyPressed(a) || input.IsKeyPressed(b); };
    if (held(forward, KeyButton::UP))
        camera_.moveForward(step);
    if (held(KeyButton::S, KeyButton::DOWN))
        camera_.moveForward(-step);
    if (held(left, KeyButton::LEFT))
        camera_.moveRight(-step);
    if (held(KeyButton::D, KeyButton::RIGHT))
        camera_.moveRight(step);
    if (input.IsKeyPressed(KeyButton::SPACE))
        camera_.moveUp(step);
    if (held(KeyButton::LEFT_SHIFT, KeyButton::RIGHT_SHIFT))
        camera_.moveUp(-step);
}

void Viewer::handleRequests()
{
    if (state_.themeRequested)
    {
        state_.themeRequested = false;
        loadTheme();
    }
    // Pendant un glissement, la régénération attend 0,2 s sans changement (pas une régénération par image).
    if (state_.paramsChangedAt >= 0.0 && time_ - state_.paramsChangedAt > 0.2)
    {
        state_.paramsChangedAt = -1.0;
        state_.regenerateRequested = true;
    }
    if (state_.regenerateRequested)
    {
        state_.regenerateRequested = false;
        restartStreaming();
    }
    if (state_.exportRequested)
    {
        state_.exportRequested = false;
        exportHeightmap();
    }
}

void Viewer::exportHeightmap()
{
    constexpr std::uint32_t kSize = 1024;
    const glm::vec3 p = camera_.position();
    const float spacing = state_.params.sampleSpacing;
    const auto firstX = static_cast<std::int64_t>(std::floor(p.x / spacing)) - kSize / 2;
    const auto firstZ = static_cast<std::int64_t>(std::floor(p.z / spacing)) - kSize / 2;
    const w::HeightField field = w::generateRegion(state_.params, state_.seed, firstX, firstZ, kSize, kSize, &tasks_);
    std::error_code ec;
    std::filesystem::create_directories("exports", ec);
    const std::filesystem::path path = "exports/heightmap_seed" + std::to_string(state_.seed) + ".png";
    if (auto r = w::writeHeightPng16(path, field, field.minHeight(), field.maxHeight()); !r)
    {
        state_.status = "Export failed: " + r.error().message;
        LOG_ERROR(1, state_.status);
        return;
    }
    state_.status = "Exported " + path.generic_string();
    LOG_INFO("Viewer: ", state_.status);
}

void Viewer::frame(float deltaTime)
{
    time_ += deltaTime;
    state_.now = time_;
    watchClock_ += deltaTime;
    if (watchClock_ > 0.5)
    {
        watchClock_ = 0.0;
        watcher_.poll(); // thème modifié sur le disque : rechargé par Ui::update
    }
    if (options_.headless)
    {
        view_ = pixels_ = {options_.width, options_.height};
    }
    else
    {
        view_ = {window_.GetWidth(), window_.GetHeight()};
        window_.GetFramebufferSize(pixels_.x, pixels_.y);
    }

    ui_->update(readInput(deltaTime), glm::vec2(view_));
    handleRequests();
    moveCamera(deltaTime);

    const glm::vec3 p = camera_.position();
    streamer_->update(p.x, p.z);
    if (!options_.captureDirectory.empty())
    {
        // Capture : toutes les tuiles de la sélection générées avant l'image (complète et reproductible).
        for (int round = 0; round < 64 && streamer_->stats().pendingTiles > 0; ++round)
        {
            streamer_->finishPending();
            streamer_->update(p.x, p.z);
        }
    }
    syncTiles();

    const w::StreamingStats s = streamer_->stats();
    stats_.tiles = static_cast<int>(s.residentTiles);
    stats_.pending = static_cast<int>(s.pendingTiles);
    stats_.memoryMb = static_cast<double>(s.memoryBytes) / (1024.0 * 1024.0);
    stats_.fps = options_.headless ? 0.0 : window_.GetAverageFPS();
    stats_.frameTimes.push_back(deltaTime * 1000.0f);
    if (stats_.frameTimes.size() > kFrameHistory)
    {
        stats_.frameTimes.erase(stats_.frameTimes.begin());
    }
}

bool Viewer::render()
{
    auto backbuffer = device_->beginFrame();
    if (!backbuffer || pixels_.x <= 0 || pixels_.y <= 0)
    {
        return false;
    }
    const auto pw = static_cast<std::uint32_t>(pixels_.x);
    const auto ph = static_cast<std::uint32_t>(pixels_.y);
    if (!composed_.valid())
    {
        auto target = rd::RenderTarget::create(*device_, {.size = {pw, ph}, .depth = false, .debugName = "composed"});
        if (!target)
        {
            LOG_ERROR(1, "Viewer: ", target.error().message);
            return false;
        }
        composed_ = std::move(*target);
    }
    else if (auto r = composed_.resize(pw, ph); !r)
    {
        LOG_ERROR(1, "Viewer: ", r.error().message);
        return false;
    }

    auto encoder = device_->createCommandEncoder();
    camera_.setAspect(pw, ph);
    scene_->setOutputResolution(pixels_.x, pixels_.y);
    engine::core::Result<> status = scene_->beginFrame(*encoder, {.clearColor = {0.55f, 0.68f, 0.86f, 1.0f}});
    if (status)
        status = scene_->setCamera(camera_);
    if (status)
        status = terrain_->draw(*scene_, camera_);
    if (status)
        status = scene_->endFrame({composed_.color(0), rhi::Format::kRGBA8Unorm, {pw, ph}});
    rd::DrawList list;
    ui_->draw(list);
    if (status)
        status = canvas_->render(*encoder, list, {composed_.color(0), rhi::Format::kRGBA8Unorm, {pw, ph}}, true);
    if (status)
        status = scene_->blit().copy(*encoder, composed_.color(0), scene_->blit().linearSampler(), *backbuffer, rhi::Format::kRGBA8Unorm, "present");
    auto submitted = device_->submit(std::move(encoder));
    if (!status || !submitted)
    {
        LOG_ERROR(1, "Viewer: frame: ", (!status ? status.error().message : submitted.error().message));
        (void)device_->present();
        return false;
    }
    ++renderedFrames_;
    if (!options_.captureDirectory.empty() && renderedFrames_ >= options_.captureFrames)
    {
        std::error_code ec;
        std::filesystem::create_directories(options_.captureDirectory, ec);
        const auto path = options_.captureDirectory / "viewer.png";
        if (auto saved = rd::savePng(*device_, composed_.color(0), path); !saved)
        {
            LOG_ERROR(1, "Viewer: capture: ", saved.error().message);
            captureFailed_ = true;
        }
        else
        {
            LOG_INFO("Viewer: capture written to ", path.generic_string());
        }
    }
    (void)device_->present();
    return true;
}

int Viewer::run()
{
    if (options_.headless || !options_.captureDirectory.empty())
    {
        // Pas de temps fixe : mêmes images à chaque lancement.
        const std::uint32_t frames = std::max<std::uint32_t>(options_.captureFrames, 1);
        std::uint32_t attempts = 0;
        while (renderedFrames_ < frames && attempts++ < frames * 4)
        {
            if (!options_.headless && !window_.NewFrame())
            {
                continue;
            }
            frame(kFixedStep);
            if (!render())
            {
                captureFailed_ = true;
                break;
            }
        }
        return captureFailed_ || renderedFrames_ < frames ? 2 : 0;
    }
    while (!window_.ShouldClose())
    {
        if (!window_.NewFrame())
        {
            continue;
        }
        InputManager &input = InputManager::GetInstance(window_.GetWindow());
        if (input.IsKeyJustPressed(KeyButton::ESCAPE) && !ui_->wantsKeyboard() && ui_->popup() == nullptr)
        {
            window_.RequestClose();
        }
        const double fps = window_.GetFPS();
        frame(fps > 1.0 ? static_cast<float>(1.0 / fps) : kFixedStep);
        (void)render();
    }
    return 0;
}
