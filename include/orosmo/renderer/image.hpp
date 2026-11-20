// Images CPU (RGBA8, origine en haut à gauche) : lecture d'une texture RHI, PNG, comparaison (captures et images de référence).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace orosmo::renderer
{

struct Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, première ligne = haut de l'image

    bool empty() const noexcept { return width == 0 || height == 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4; }
};

// Lit toute la texture (RGBA8Unorm, RGBA8Srgb ou BGRA8Unorm). Repère du RHI (A26) : la première ligne est le haut, comme dans
// l'image produite (aucun retournement).
core::Result<Image> readImage(rhi::Device &device, rhi::TextureHandle texture);

core::Result<> savePng(const Image &image, const std::filesystem::path &path);
core::Result<> savePng(rhi::Device &device, rhi::TextureHandle texture, const std::filesystem::path &path);
core::Result<Image> loadPng(const std::filesystem::path &path);

struct ImageDiff
{
    std::uint64_t pixels = 0;
    std::uint64_t outOfTolerance = 0; // pixels dont un canal dépasse `tolerance`
    std::uint8_t maxDelta = 0;
    double meanDelta = 0.0; // sur tous les canaux
};

// Les deux images doivent avoir la même taille (sinon kInvalidArgument).
core::Result<ImageDiff> compareImages(const Image &a, const Image &b, std::uint8_t tolerance);

} // namespace orosmo::renderer
