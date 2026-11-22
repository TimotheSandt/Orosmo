// REN-17, REN-18 : références CPU des post-traitements (voir tonemap.hpp). Toute modification ici se reporte dans
// shaders/rhi/post/post_common.glsl (et inversement) : tests/renderer/post_effects_tests.cpp compare les deux.
#include "orosmo/renderer/tonemap.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

glm::vec3 saturate(const glm::vec3 &v) noexcept { return glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f)); }

// ACES : ajustement de S. Hill (matrices sRGB -> AP1 + RRT/ODT approchés). Matrices écrites colonne par colonne (comme GLSL).
glm::vec3 aces(const glm::vec3 &color) noexcept
{
    const glm::mat3 input(0.59719f, 0.07600f, 0.02840f, 0.35458f, 0.90834f, 0.13383f, 0.04823f, 0.01566f, 0.83777f);
    const glm::mat3 output(1.60475f, -0.10208f, -0.00327f, -0.53108f, 1.10813f, -0.07276f, -0.07367f, -0.00605f, 1.07602f);
    const glm::vec3 v = input * color;
    const glm::vec3 a = v * (v + 0.0245786f) - 0.000090537f;
    const glm::vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return saturate(output * (a / b));
}

// AgX (version « minimale » de B. Wrensch) : matrice d'entrée, encodage log2 dans [-12.47393, 4.026069], courbe de contraste
// polynomiale (degré 6), matrice de sortie et retour en linéaire (puissance 2,2).
glm::vec3 agx(const glm::vec3 &color) noexcept
{
    const glm::mat3 inset(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f, 0.0784335999999992f, 0.878468636469772f, 0.0784336f,
                          0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    const glm::mat3 outset(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f, -0.0980208811401368f, 1.15190312990417f,
                           -0.0980434501171241f, -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
    constexpr float kMinEv = -12.47393f;
    constexpr float kMaxEv = 4.026069f;
    glm::vec3 v = inset * glm::max(color, glm::vec3(1e-10f));
    v = glm::clamp(glm::log2(v), glm::vec3(kMinEv), glm::vec3(kMaxEv));
    v = (v - kMinEv) / (kMaxEv - kMinEv);
    const glm::vec3 x2 = v * v;
    const glm::vec3 x4 = x2 * x2;
    v = 15.5f * x4 * x2 - 40.14f * x4 * v + 31.96f * x4 - 6.868f * x2 * v + 0.4298f * x2 + 0.1191f * v - 0.00232f;
    v = outset * v;
    return saturate(glm::pow(glm::max(v, glm::vec3(0.0f)), glm::vec3(2.2f)));
}

glm::vec3 reinhardExtended(const glm::vec3 &color, float whitePoint) noexcept
{
    const float l = luminance(color);
    if (l <= 1e-6f)
    {
        return glm::vec3(0.0f);
    }
    const float w = std::max(whitePoint, 1e-3f);
    const float mapped = l * (1.0f + l / (w * w)) / (1.0f + l);
    return saturate(color * (mapped / l));
}

float srgbEncode(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c < 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float srgbDecode(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return c < 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float smoothstep(float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    {
        s.remove_suffix(1);
    }
    return s;
}

// Lit exactement `count` flottants finis séparés par des blancs ; faux si un jeton n'est pas entièrement un nombre fini
// (« 0.1,0.2 », « 1x », « inf », « +-1 »), s'il en manque ou s'il en reste. Un « + » initial unique est accepté (« +0.5 »).
bool readFloats(std::string_view s, float *out, int count) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        s = trim(s);
        if (s.empty())
        {
            return false;
        }
        const char *begin = s.data();
        if (*begin == '+')
        {
            ++begin;
            if (begin == s.data() + s.size() || *begin == '+' || *begin == '-')
            {
                return false;
            }
        }
        const auto [end, ec] = std::from_chars(begin, s.data() + s.size(), out[i]);
        if (ec != std::errc{} || !std::isfinite(out[i]))
        {
            return false;
        }
        s.remove_prefix(static_cast<std::size_t>(end - s.data()));
        if (!s.empty() && s.front() != ' ' && s.front() != '\t' && s.front() != '\r')
        {
            return false; // le jeton n'est pas consommé en entier
        }
    }
    return trim(s).empty();
}

} // namespace

const char *toString(Tonemapper tonemapper) noexcept
{
    switch (tonemapper)
    {
    case Tonemapper::kClamp:
        return "clamp";
    case Tonemapper::kAces:
        return "aces";
    case Tonemapper::kAgx:
        return "agx";
    case Tonemapper::kReinhardExtended:
        return "reinhard";
    }
    return "unknown";
}

std::optional<Tonemapper> parseTonemapper(std::string_view name) noexcept
{
    for (const Tonemapper t : {Tonemapper::kClamp, Tonemapper::kAces, Tonemapper::kAgx, Tonemapper::kReinhardExtended})
    {
        if (name == toString(t))
        {
            return t;
        }
    }
    return std::nullopt;
}

float luminance(const glm::vec3 &linearRgb) noexcept { return glm::dot(linearRgb, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }

glm::vec3 tonemap(Tonemapper tonemapper, const glm::vec3 &color, float whitePoint) noexcept
{
    const glm::vec3 c = glm::max(color, glm::vec3(0.0f));
    switch (tonemapper)
    {
    case Tonemapper::kAces:
        return aces(c);
    case Tonemapper::kAgx:
        return agx(c);
    case Tonemapper::kReinhardExtended:
        return reinhardExtended(c, whitePoint);
    case Tonemapper::kClamp:
        break;
    }
    return saturate(c);
}

glm::vec3 linearToSrgb(const glm::vec3 &linear) noexcept { return {srgbEncode(linear.r), srgbEncode(linear.g), srgbEncode(linear.b)}; }

glm::vec3 srgbToLinear(const glm::vec3 &encoded) noexcept { return {srgbDecode(encoded.r), srgbDecode(encoded.g), srgbDecode(encoded.b)}; }

std::uint32_t histogramBin(float lum, float minLog2, float maxLog2) noexcept
{
    if (!(lum >= std::exp2(minLog2))) // NaN compris
    {
        return 0;
    }
    const float t = std::clamp((std::log2(lum) - minLog2) / (maxLog2 - minLog2), 0.0f, 1.0f);
    return static_cast<std::uint32_t>(t * 254.0f + 1.0f);
}

float histogramAverageLuminance(std::span<const std::uint32_t> bins, float minLog2, float maxLog2) noexcept
{
    double weighted = 0.0;
    double count = 0.0;
    for (std::size_t i = 1; i < bins.size() && i < kHistogramBins; ++i)
    {
        weighted += static_cast<double>(bins[i]) * static_cast<double>(i);
        count += static_cast<double>(bins[i]);
    }
    if (count < 0.5)
    {
        return std::exp2(minLog2);
    }
    const float averageBin = static_cast<float>(weighted / count);
    // Case k (1 à 255) : t dans [(k - 1) / 254, k / 254[ ; son centre est (k - 0,5) / 254.
    const float logLum = (averageBin - 0.5f) / 254.0f * (maxLog2 - minLog2) + minLog2;
    return std::exp2(logLum);
}

float targetExposure(float averageLuminance, float key, float minEv, float maxEv) noexcept
{
    const float exposure = key / std::max(averageLuminance, 1e-6f);
    return std::clamp(exposure, std::exp2(minEv), std::exp2(maxEv));
}

float adaptationFactor(float deltaSeconds, float speed) noexcept
{
    if (speed <= 0.0f || deltaSeconds <= 0.0f)
    {
        return 1.0f;
    }
    return 1.0f - std::exp(-deltaSeconds * speed);
}

float vignetteFactor(const glm::vec2 &uv, float aspect, const VignetteSettings &settings) noexcept
{
    const float ax = 1.0f + (aspect - 1.0f) * std::clamp(settings.roundness, 0.0f, 1.0f);
    glm::vec2 v = (uv - 0.5f) * 2.0f;
    v.x *= ax;
    const float d = glm::length(v) / glm::length(glm::vec2(ax, 1.0f));
    const float t = smoothstep(settings.radius, settings.radius + std::max(settings.softness, 1e-4f), d);
    return 1.0f - std::clamp(settings.intensity, 0.0f, 1.0f) * t;
}

ColorLut identityLut(std::uint32_t size)
{
    ColorLut lut;
    lut.size = std::max<std::uint32_t>(size, 2);
    const float scale = 1.0f / static_cast<float>(lut.size - 1);
    lut.values.reserve(static_cast<std::size_t>(lut.size) * lut.size * lut.size);
    for (std::uint32_t b = 0; b < lut.size; ++b)
    {
        for (std::uint32_t g = 0; g < lut.size; ++g)
        {
            for (std::uint32_t r = 0; r < lut.size; ++r)
            {
                lut.values.emplace_back(static_cast<float>(r) * scale, static_cast<float>(g) * scale, static_cast<float>(b) * scale);
            }
        }
    }
    return lut;
}

glm::vec3 sampleLut(const ColorLut &lut, const glm::vec3 &color) noexcept
{
    if (!lut.valid())
    {
        return color;
    }
    const auto n = static_cast<std::size_t>(lut.size);
    const auto last = static_cast<int>(lut.size) - 1;
    const glm::vec3 p = saturate(color) * static_cast<float>(last);
    const glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(p)), glm::ivec3(last - 1));
    const glm::vec3 f = p - glm::vec3(i0);
    auto at = [&](int r, int g, int b)
    { return lut.values[static_cast<std::size_t>(r) + n * (static_cast<std::size_t>(g) + n * static_cast<std::size_t>(b))]; };
    glm::vec3 result(0.0f);
    for (int dz = 0; dz < 2; ++dz)
    {
        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                const float w = (dx ? f.x : 1.0f - f.x) * (dy ? f.y : 1.0f - f.y) * (dz ? f.z : 1.0f - f.z);
                result += w * at(i0.x + dx, i0.y + dy, i0.z + dz);
            }
        }
    }
    return result;
}

core::Result<ColorLut> parseCubeLut(std::string_view text)
{
    ColorLut lut;
    std::size_t lineNumber = 0;
    while (!text.empty())
    {
        const std::size_t eol = text.find('\n');
        const std::string_view raw = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        ++lineNumber;
        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const auto where = " (line " + std::to_string(lineNumber) + ")";
        if (line.starts_with("TITLE"))
        {
            continue;
        }
        if (line.starts_with("LUT_1D_SIZE"))
        {
            return makeError(ErrorCode::kUnsupported, "parseCubeLut: 1D LUTs are not supported" + where);
        }
        if (line.starts_with("LUT_3D_SIZE"))
        {
            float size = 0.0f;
            if (!readFloats(line.substr(11), &size, 1) || size < 2.0f || size > 256.0f || size != std::floor(size))
            {
                return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: LUT_3D_SIZE must be an integer in [2, 256]" + where);
            }
            lut.size = static_cast<std::uint32_t>(size);
            continue; // pas de réservation d'après la taille annoncée : le tableau croît avec les lignes réellement lues
        }
        if (line.starts_with("DOMAIN_MIN") || line.starts_with("DOMAIN_MAX"))
        {
            float d[3] = {};
            const float expected = line.starts_with("DOMAIN_MIN") ? 0.0f : 1.0f;
            if (!readFloats(line.substr(10), d, 3) || d[0] != expected || d[1] != expected || d[2] != expected)
            {
                return makeError(ErrorCode::kUnsupported, "parseCubeLut: only the [0, 1] domain is supported" + where);
            }
            continue;
        }
        float rgb[3] = {};
        if (!readFloats(line, rgb, 3))
        {
            return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: unreadable line" + where);
        }
        if (lut.size == 0)
        {
            return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: a color comes before LUT_3D_SIZE" + where);
        }
        const std::size_t expected = static_cast<std::size_t>(lut.size) * lut.size * lut.size;
        if (lut.values.size() >= expected)
        {
            return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: more than " + std::to_string(expected) + " colors" + where);
        }
        lut.values.emplace_back(rgb[0], rgb[1], rgb[2]);
    }
    if (lut.size == 0)
    {
        return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: LUT_3D_SIZE is missing");
    }
    if (!lut.valid())
    {
        return makeError(ErrorCode::kInvalidArgument, "parseCubeLut: expected " + std::to_string(static_cast<std::size_t>(lut.size) * lut.size * lut.size) +
                                                          " colors, got " + std::to_string(lut.values.size()));
    }
    return lut;
}

} // namespace orosmo::renderer
