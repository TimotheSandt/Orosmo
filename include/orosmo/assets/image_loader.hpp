// Décodage d'images (PNG, JPG, TGA, BMP par stb) et choix de l'espace colorimétrique d'une texture selon son usage (ASSET-02).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace orosmo::assets
{

// Usage d'une texture : il décide de l'espace colorimétrique. Une couleur (albédo, émission, interface) est stockée en sRGB et
// lue linéaire par l'échantillonneur ; une donnée (normale, métal/rugosité, occlusion, masque) est déjà linéaire.
enum class TextureRole : std::uint8_t
{
    kColor,
    kData,
    kNormal,
};

enum class ColorSpace : std::uint8_t
{
    kSrgb,
    kLinear,
};

ColorSpace colorSpaceOf(TextureRole role) noexcept;
// Format RGBA8 du RHI pour cet usage : kRGBA8Srgb pour une couleur, kRGBA8Unorm sinon.
rhi::Format textureFormatFor(TextureRole role) noexcept;

// Image décodée en RGBA8, lignes serrées, première ligne = haut de l'image (repère du RHI, A26).
struct DecodedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sourceChannels = 0; // canaux présents dans le fichier (1 à 4), avant l'expansion en RGBA
    std::vector<std::byte> rgba8;

    bool empty() const noexcept { return width == 0 || height == 0; }
};

// `name` sert seulement aux messages d'erreur. kInvalidArgument pour des octets vides, kParse pour un contenu illisible.
core::Result<DecodedImage> decodeImage(std::span<const std::byte> encoded, std::string_view name);
// kIo si le fichier ne s'ouvre pas, puis les erreurs de `decodeImage`.
core::Result<DecodedImage> loadImage(const std::filesystem::path &path);

} // namespace orosmo::assets
