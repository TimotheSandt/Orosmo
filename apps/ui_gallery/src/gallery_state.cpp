#include "gallery_state.hpp"

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
    char buffer[200];
    std::snprintf(buffer, sizeof buffer, "name=%s volume=%.2f count=%d fruit=%s seed=%u clicks=%d", name.c_str(), static_cast<double>(volume), count,
                  fruits()[static_cast<std::size_t>(fruit) % 4].c_str(), seed, clicks);
    return buffer;
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
}
