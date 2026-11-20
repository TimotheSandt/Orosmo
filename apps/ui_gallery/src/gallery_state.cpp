#include "gallery_state.hpp"

#include <engine/ui/ui.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
std::string idOf(const engine::ui::Event &e) { return e.source != nullptr && !e.source->id().empty() ? e.source->id() : "?"; }
} // namespace

void GalleryState::record(std::string line)
{
    log.push_front(std::move(line));
    while (log.size() > 12)
        log.pop_back();
}

void GalleryState::clicked(const engine::ui::Event &e)
{
    ++clicks;
    record("click   " + idOf(e));
}

void GalleryState::changed(const engine::ui::Event &e) { record("change  " + idOf(e)); }
void GalleryState::hovered(const engine::ui::Event &e) { record(std::string(e.active ? "enter   " : "leave   ") + idOf(e)); }
void GalleryState::focused(const engine::ui::Event &e) { record(std::string(e.active ? "focus   " : "blur    ") + idOf(e)); }

std::string GalleryState::summary() const
{
    // Le nom est copié tel quel (pas de tampon fixe qui le couperait au milieu d'un caractère UTF-8 et perdrait la suite).
    char rest[160];
    std::snprintf(rest, sizeof rest, " volume=%.2f count=%d fruit=%s seed=%u clicks=%d", static_cast<double>(volume), count,
                  fruits()[static_cast<std::size_t>(fruit) % 4].c_str(), seed, clicks);
    return "name=" + name + rest;
}

std::string GalleryState::controlsSummary() const
{
    static const char *const kDetail[] = {"low", "medium", "high"};
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "detail=%s heights=[%.0f, %.0f] octaves=%d", kDetail[std::clamp(detail, 0, 2)], heightLow, heightHigh,
                  octaves);
    return buffer;
}

void GalleryState::notifyInfo()
{
    if (ui != nullptr)
        ui->notify("Notification " + std::to_string(++notified) + ": information", engine::ui::NotificationKind::kInfo);
}
void GalleryState::notifySuccess()
{
    if (ui != nullptr)
        ui->notify("Notification " + std::to_string(++notified) + ": terrain saved", engine::ui::NotificationKind::kSuccess);
}
void GalleryState::notifyWarning()
{
    if (ui != nullptr)
        ui->notify("Notification " + std::to_string(++notified) + ": low memory", engine::ui::NotificationKind::kWarning);
}
void GalleryState::notifyError()
{
    if (ui != nullptr)
        ui->notify("Notification " + std::to_string(++notified) + ": export failed", engine::ui::NotificationKind::kError, 6.0f);
}

namespace
{
// Hauteur de démonstration (déterministe, sans bruit du moteur) dans [0, 1].
float sampleHeight(float x, float y, int octaves)
{
    float h = 0.0f;
    float amplitude = 0.5f;
    float frequency = 3.0f;
    float total = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        h += amplitude * (std::sin(x * frequency + static_cast<float>(o) * 1.7f) * std::cos(y * frequency * 1.3f - static_cast<float>(o) * 0.9f));
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.1f;
    }
    return std::clamp(0.5f + 0.5f * h / total, 0.0f, 1.0f);
}
} // namespace

void GalleryState::drawMinimap()
{
    const std::uint32_t w = minimap.width();
    const std::uint32_t h = minimap.height();
    const float lo = static_cast<float>(heightLow) / 100.0f;
    const float hi = std::max(static_cast<float>(heightHigh) / 100.0f, lo + 0.01f);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(w);
            const float v = static_cast<float>(y) / static_cast<float>(h);
            const float raw = sampleHeight(u, v, std::clamp(octaves, 1, 8));
            const float shaped = heightCurve.evaluate(std::clamp((raw - lo) / (hi - lo), 0.0f, 1.0f));
            minimap.setPixel(x, y, terrainColors.evaluate(shaped));
        }
    if (showPlayer)
    {
        const float a = time * 0.4f;
        const int px = static_cast<int>((0.5f + 0.3f * std::cos(a)) * static_cast<float>(w));
        const int py = static_cast<int>((0.5f + 0.3f * std::sin(a)) * static_cast<float>(h));
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                if (dx * dx + dy * dy <= 4 && px + dx >= 0 && py + dy >= 0 && px + dx < static_cast<int>(w) && py + dy < static_cast<int>(h))
                    minimap.setPixel(static_cast<std::uint32_t>(px + dx), static_cast<std::uint32_t>(py + dy), accent);
    }
    minimap.commit();
}

void GalleryState::drawCompass()
{
    const float size = static_cast<float>(compass.width());
    const glm::vec2 c(size * 0.5f);
    const float heading = time * 0.4f + 1.5707963f; // le joueur tourne sur le cercle de la mini-carte
    const glm::vec2 dir(std::cos(heading), std::sin(heading));
    compass.fill({0, 0, 0, 0});
    for (std::uint32_t y = 0; y < compass.height(); ++y)
        for (std::uint32_t x = 0; x < compass.width(); ++x)
        {
            const glm::vec2 p = glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f - c;
            const float d = glm::length(p);
            if (d > size * 0.5f - 1.0f)
                continue;
            engine::ui::Color color{0.08f, 0.09f, 0.12f, 0.85f};
            if (d > size * 0.5f - 4.0f)
                color = {0.75f, 0.75f, 0.80f, 1.0f};
            const float along = glm::dot(p, dir);
            const float across = std::abs(p.x * dir.y - p.y * dir.x);
            const float half = size * 0.5f - 7.0f;
            if (std::abs(along) < half && across < 3.0f * (1.0f - std::abs(along) / half) + 0.5f)
                color = along > 0.0f ? engine::ui::Color{0.90f, 0.25f, 0.20f, 1.0f} : engine::ui::Color{0.92f, 0.92f, 0.95f, 1.0f};
            compass.setPixel(x, y, color);
        }
    compass.commit();
}

void GalleryState::tick(float deltaTime)
{
    time += deltaTime;
    samples.resize(48);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const float x = static_cast<float>(i) * 0.25f + time * 2.0f;
        samples[i] = 1.0f + std::sin(x) * 0.6f + std::sin(x * 2.7f) * 0.25f * volume;
    }
    drawMinimap();
    drawCompass();
}
