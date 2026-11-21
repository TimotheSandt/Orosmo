#include "orosmo/renderer/lod.hpp"

#include <algorithm>
#include <cmath>

namespace orosmo::renderer
{

LodSelection selectLod(const LodSettings &settings, std::uint8_t levelCount, float distance) noexcept
{
    LodSelection out;
    if (!(distance >= 0.0f)) // NaN ou négatif : distance nulle
    {
        distance = 0.0f;
    }
    levelCount = std::max<std::uint8_t>(levelCount, 1);
    const float d = distance * (settings.bias > 0.0f ? settings.bias : 1.0f);
    const float width = std::max(settings.transitionWidth, 0.0f);
    const float half = width * 0.5f;

    // Niveau (ou fondu croisé) choisi par les bascules.
    const std::size_t switches = std::min<std::size_t>(settings.switchDistances.size(), static_cast<std::size_t>(levelCount - 1));
    std::uint8_t level = static_cast<std::uint8_t>(switches);
    float crossT = 0.0f;
    bool crossing = false;
    for (std::size_t j = 0; j < switches; ++j)
    {
        const float s = settings.switchDistances[j];
        if (d < s - half)
        {
            level = static_cast<std::uint8_t>(j);
            break;
        }
        if (width > 0.0f && d < s + half)
        {
            level = static_cast<std::uint8_t>(j);
            crossT = (d - (s - half)) / width;
            crossing = crossT > 0.0f; // t = 0 : le niveau j seul (un seuil nul voudrait dire « toujours dessiné »)
            break;
        }
    }

    if (settings.cullDistance > 0.0f)
    {
        if (d >= settings.cullDistance + half)
        {
            return out; // rejeté
        }
        if (width > 0.0f && d > settings.cullDistance - half)
        {
            const float t = (d - (settings.cullDistance - half)) / width;
            out.count = 1;
            out.draws[0] = {static_cast<std::uint8_t>(crossing ? level + 1 : level), t};
            return out;
        }
    }

    if (crossing)
    {
        out.count = 2;
        out.draws[0] = {level, crossT};                                         // s'efface
        out.draws[1] = {static_cast<std::uint8_t>(level + 1), -crossT};        // apparaît
        return out;
    }
    out.count = 1;
    out.draws[0] = {level, 0.0f};
    return out;
}

bool ditherKeeps(float dither, float n) noexcept
{
    if (dither > 0.0f)
    {
        return n >= dither;
    }
    if (dither < 0.0f)
    {
        return n < -dither;
    }
    return true;
}

float ditherCoverage(float dither) noexcept
{
    if (dither > 0.0f)
    {
        return 1.0f - std::min(dither, 1.0f);
    }
    if (dither < 0.0f)
    {
        return std::min(-dither, 1.0f);
    }
    return 1.0f;
}

} // namespace orosmo::renderer
