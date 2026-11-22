#include "UI/demo_ui_state.hpp"

#include <orosmo/core/serialization.hpp>
#include <orosmo/core/tasks.hpp>
#include <orosmo/ui/ui.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace
{
constexpr const char *kConfigPath = "demo_config.json";
constexpr std::size_t kFrameHistorySize = 48;
constexpr std::size_t kMaxLogLines = 12;

std::string idOf(const orosmo::ui::Event &e) { return e.source != nullptr && !e.source->id().empty() ? e.source->id() : "?"; }
} // namespace

std::vector<std::string> DemoUiState::themes() const { return {"dark", "light", "framed"}; }

std::vector<std::string> DemoUiState::tabNames() { return {"scene", "widgets", "controls", "color", "windows", "layout", "themes"}; }

bool DemoUiState::selectThemeByName(std::string_view name)
{
    const auto available = themes();
    const auto it = std::find(available.begin(), available.end(), name);
    if (it == available.end())
    {
        return false;
    }
    theme = static_cast<int>(it - available.begin());
    themeRequested = true;
    return true;
}

bool DemoUiState::selectTabByName(std::string_view name)
{
    const auto available = tabNames();
    const auto it = std::find(available.begin(), available.end(), name);
    if (it == available.end())
    {
        return false;
    }
    tab = static_cast<int>(it - available.begin());
    return true;
}

void DemoUiState::requestQuit()
{
    quitRequested = true;
    record("action  quit");
}

void DemoUiState::requestCloseUi()
{
    closeUiRequested = true;
    record("action  resume 3D");
}

void DemoUiState::requestResetCamera()
{
    resetCameraRequested = true;
    record("action  reset camera");
    if (ui != nullptr)
    {
        ui->notify("Camera reset to default view", orosmo::ui::NotificationKind::kInfo);
    }
}

void DemoUiState::requestCapture()
{
    captureRequested = true;
    record("action  capture PNG");
    if (ui != nullptr)
    {
        ui->notify("Capture requested", orosmo::ui::NotificationKind::kInfo);
    }
}

void DemoUiState::runTaskBenchmark()
{
    constexpr std::size_t kTaskCount = 10000;
    constexpr std::size_t kGrain = 128;

    orosmo::core::TaskSystem tasks;
    std::atomic<std::uint64_t> checksum{0};

    const auto start = std::chrono::steady_clock::now();
    tasks.parallelFor(kTaskCount, kGrain,
                      [&checksum](std::size_t i)
                      {
                          const std::uint64_t mixed = static_cast<std::uint64_t>(i) * 2654435761ULL;
                          checksum.fetch_add(mixed ^ (mixed >> 16), std::memory_order_relaxed);
                      });
    const auto end = std::chrono::steady_clock::now();

    const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    (void)checksum.load(std::memory_order_relaxed);

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%zu tasks on %u threads in %.2f ms", kTaskCount, tasks.threadCount(), elapsedMs);
    taskBenchmarkResult = buffer;
    record("tasks   " + taskBenchmarkResult);
    if (ui != nullptr)
    {
        ui->notify(taskBenchmarkResult, orosmo::ui::NotificationKind::kSuccess);
    }
}

void DemoUiState::saveConfigJson()
{
    const std::string json = orosmo::core::dumpJson(orosmo::core::saveDocument(scene));
    std::ofstream out(kConfigPath, std::ios::binary | std::ios::trunc);
    if (!out || !(out << json) || !out.flush())
    {
        configStatus = std::string("Save failed: cannot write ") + kConfigPath;
        record("config  save failed");
        if (ui != nullptr)
        {
            ui->notify(configStatus, orosmo::ui::NotificationKind::kError);
        }
        return;
    }

    configStatus = std::string("Saved ") + kConfigPath;
    record("config  saved " + std::string(kConfigPath));
    if (ui != nullptr)
    {
        ui->notify(configStatus, orosmo::ui::NotificationKind::kSuccess);
    }
}

void DemoUiState::loadConfigJson()
{
    std::ifstream in(kConfigPath, std::ios::binary);
    if (!in)
    {
        configStatus = std::string("Load failed: ") + kConfigPath + " not found";
        record("config  load failed");
        if (ui != nullptr)
        {
            ui->notify(configStatus, orosmo::ui::NotificationKind::kWarning);
        }
        return;
    }

    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = orosmo::core::parseJson(content);
    if (!parsed)
    {
        configStatus = "Load failed: " + parsed.error().message;
        record("config  parse error");
        if (ui != nullptr)
        {
            ui->notify(configStatus, orosmo::ui::NotificationKind::kError);
        }
        return;
    }

    demo::DemoSceneParams loaded;
    if (auto status = orosmo::core::loadDocument(loaded, std::move(*parsed)); !status)
    {
        configStatus = "Load failed: " + status.error().message;
        record("config  load error");
        if (ui != nullptr)
        {
            ui->notify(configStatus, orosmo::ui::NotificationKind::kError);
        }
        return;
    }

    scene = loaded;
    configStatus = std::string("Loaded ") + kConfigPath;
    record("config  loaded " + std::string(kConfigPath));
    if (ui != nullptr)
    {
        ui->notify(configStatus, orosmo::ui::NotificationKind::kSuccess);
    }
}

void DemoUiState::record(std::string line)
{
    log.push_front(std::move(line));
    while (log.size() > kMaxLogLines)
    {
        log.pop_back();
    }
}

void DemoUiState::clicked(const orosmo::ui::Event &e)
{
    ++clicks;
    record("click   " + idOf(e));
}

void DemoUiState::changed(const orosmo::ui::Event &e) { record("change  " + idOf(e)); }

void DemoUiState::hovered(const orosmo::ui::Event &e) { record(std::string(e.active ? "enter   " : "leave   ") + idOf(e)); }

void DemoUiState::focused(const orosmo::ui::Event &e) { record(std::string(e.active ? "focus   " : "blur    ") + idOf(e)); }

std::string DemoUiState::logLine(int i) const
{
    if (i < 0 || static_cast<std::size_t>(i) >= log.size())
    {
        return {};
    }
    return log[static_cast<std::size_t>(i)];
}

std::string DemoUiState::summary() const
{
    const auto list = fruits();
    const std::size_t idx = static_cast<std::size_t>(std::clamp(fruit, 0, static_cast<int>(list.size()) - 1));
    char rest[160];
    std::snprintf(rest, sizeof(rest), " volume=%.2f count=%d fruit=%s id=%u clicks=%d", static_cast<double>(volume), count,
                  list[idx].c_str(), static_cast<unsigned>(sampleId), clicks);
    return "name=" + name + rest;
}

std::string DemoUiState::controlsSummary() const
{
    static constexpr const char *kDetail[] = {"low", "medium", "high"};
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "detail=%s range=[%.0f, %.0f] steps=%d",
                  kDetail[static_cast<std::size_t>(std::clamp(detail, 0, 2))], rangeLow, rangeHigh, steps);
    return buffer;
}

void DemoUiState::notifyInfo()
{
    if (ui != nullptr)
    {
        ui->notify("Notification " + std::to_string(++notified) + ": information", orosmo::ui::NotificationKind::kInfo);
    }
}

void DemoUiState::notifySuccess()
{
    if (ui != nullptr)
    {
        ui->notify("Notification " + std::to_string(++notified) + ": configuration saved", orosmo::ui::NotificationKind::kSuccess);
    }
}

void DemoUiState::notifyWarning()
{
    if (ui != nullptr)
    {
        ui->notify("Notification " + std::to_string(++notified) + ": low memory", orosmo::ui::NotificationKind::kWarning);
    }
}

void DemoUiState::notifyError()
{
    if (ui != nullptr)
    {
        ui->notify("Notification " + std::to_string(++notified) + ": export failed", orosmo::ui::NotificationKind::kError, 6.0f);
    }
}

void DemoUiState::drawPreview()
{
    const std::uint32_t w = previewImage.width();
    const std::uint32_t h = previewImage.height();
    const float lo = static_cast<float>(rangeLow) / 100.0f;
    const float hi = std::max(static_cast<float>(rangeHigh) / 100.0f, lo + 0.01f);
    const int cells = std::clamp(steps, 1, 8);
    const float cellScale = static_cast<float>(cells * 2);

    for (std::uint32_t y = 0; y < h; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        const int cy = static_cast<int>(v * cellScale);
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            const int cx = static_cast<int>(u * cellScale);
            const float dx = u - 0.5f;
            const float dy = v - 0.5f;
            const float radial = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy) * 1.41421356f, 0.0f, 1.0f);
            const float checker = ((cx + cy) & 1) == 0 ? 0.12f : -0.12f;
            const float raw = std::clamp(radial + checker, 0.0f, 1.0f);
            const float shaped = responseCurve.evaluate(std::clamp((raw - lo) / (hi - lo), 0.0f, 1.0f));
            previewImage.setPixel(x, y, paletteGradient.evaluate(shaped));
        }
    }

    if (showMarker)
    {
        const int px = static_cast<int>((0.5f + 0.3f * std::cos(cameraYawRadians)) * static_cast<float>(w));
        const int py = static_cast<int>((0.5f + 0.3f * std::sin(cameraYawRadians)) * static_cast<float>(h));
        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                if (dx * dx + dy * dy <= 4 && px + dx >= 0 && py + dy >= 0 && px + dx < static_cast<int>(w) &&
                    py + dy < static_cast<int>(h))
                {
                    previewImage.setPixel(static_cast<std::uint32_t>(px + dx), static_cast<std::uint32_t>(py + dy), accent);
                }
            }
        }
    }

    previewImage.commit();
}

void DemoUiState::drawCompass()
{
    const float size = static_cast<float>(compass.width());
    const glm::vec2 center(size * 0.5f);
    const float heading = cameraYawRadians - 1.5707963f;
    const glm::vec2 dir(std::cos(heading), std::sin(heading));

    compass.fill({0.0f, 0.0f, 0.0f, 0.0f});
    for (std::uint32_t y = 0; y < compass.height(); ++y)
    {
        for (std::uint32_t x = 0; x < compass.width(); ++x)
        {
            const glm::vec2 p = glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f - center;
            const float d = glm::length(p);
            if (d > size * 0.5f - 1.0f)
            {
                continue;
            }
            orosmo::ui::Color color{0.08f, 0.09f, 0.12f, 0.85f};
            if (d > size * 0.5f - 4.0f)
            {
                color = {0.75f, 0.75f, 0.80f, 1.0f};
            }
            const float along = glm::dot(p, dir);
            const float across = std::abs(p.x * dir.y - p.y * dir.x);
            const float half = size * 0.5f - 7.0f;
            if (std::abs(along) < half && across < 3.0f * (1.0f - std::abs(along) / half) + 0.5f)
            {
                color = along > 0.0f ? orosmo::ui::Color{0.90f, 0.25f, 0.20f, 1.0f} : orosmo::ui::Color{0.92f, 0.92f, 0.95f, 1.0f};
            }
            compass.setPixel(x, y, color);
        }
    }
    compass.commit();
}

void DemoUiState::tick(float deltaTime, float frameTimeMs)
{
    time += deltaTime;
    const float sample = frameTimeMs > 0.0f ? frameTimeMs : 16.67f;
    if (frameSamples.size() != kFrameHistorySize)
    {
        frameSamples.assign(kFrameHistorySize, sample);
    }
    else
    {
        std::rotate(frameSamples.begin(), frameSamples.begin() + 1, frameSamples.end());
        frameSamples.back() = sample;
    }
}

bool DemoUiState::liveImagesVisible(bool fullUiShown) const
{
    static const int kWindowsTab = [] {
        const auto names = tabNames();
        return static_cast<int>(std::find(names.begin(), names.end(), "windows") - names.begin());
    }();
    return fullUiShown && tab == kWindowsTab;
}

bool DemoUiState::previewInputsChanged() const
{
    if (!drawnPreview_)
    {
        return true;
    }
    const PreviewInputs &d = *drawnPreview_;
    return d.rangeLow != rangeLow || d.rangeHigh != rangeHigh || d.steps != steps || d.showMarker != showMarker ||
           (showMarker && (d.cameraYawRadians != cameraYawRadians || d.accent != accent)) || d.gradient != paletteGradient ||
           d.curve != responseCurve;
}

void DemoUiState::refreshLiveImages(bool fullUiShown)
{
    if (!liveImagesVisible(fullUiShown))
    {
        return;
    }
    if (previewInputsChanged())
    {
        drawPreview();
        drawnPreview_ = PreviewInputs{rangeLow, rangeHigh, steps, showMarker, cameraYawRadians, accent, paletteGradient, responseCurve};
        ++previewDraws;
    }
    if (drawnCompassYaw_ != cameraYawRadians)
    {
        drawCompass();
        drawnCompassYaw_ = cameraYawRadians;
        ++compassDraws;
    }
}
