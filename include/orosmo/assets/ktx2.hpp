// KTX2 (ASSET-03) : lecture de textures compressées et écriture de textures cuites. Une texture Basis Universal (UASTC ou ETC1S) est
// transcodée au chargement vers le meilleur format que le périphérique échantillonne : BC7 (PC), sinon ASTC 4x4 (Mac, ARM), sinon
// RGBA8 non compressé. Un KTX2 déjà en BC7, ASTC 4x4 ou RGBA8 est envoyé tel quel s'il est pris en charge. Bibliothèque : libktx.
#pragma once

#include "orosmo/assets/image_loader.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/rhi/capabilities.hpp"
#include "orosmo/rhi/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace orosmo::assets
{

// Texture prête à envoyer : un tableau d'octets par niveau de mip (lignes serrées, ou blocs 4x4 pour un format compressé, première
// ligne = haut, A26).
struct TextureLevels
{
    rhi::Format format = rhi::Format::kUndefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::vector<std::byte>> levels;

    bool empty() const noexcept { return levels.empty() || width == 0 || height == 0; }
};

// Octets qui commencent par l'identifiant d'un fichier KTX2 (« «KTX 20» »).
bool isKtx2(std::span<const std::byte> bytes) noexcept;

// Format de sortie quand le fichier est en Basis Universal : BC7 si `caps.bc7Textures`, sinon ASTC 4x4 si `caps.astcTextures`,
// sinon RGBA8 ; variante sRGB quand `srgb`.
rhi::Format transcodeTargetFor(const rhi::Capabilities &caps, bool srgb) noexcept;

// Décode un KTX2 2D (pas de tableau, de cube ni de 3D) pour ce périphérique. kParse si le contenu est illisible, kUnsupported si
// le format n'est ni Basis, ni BC7, ni ASTC 4x4, ni RGBA8, ou si le périphérique ne l'échantillonne pas. `name` sert aux messages.
core::Result<TextureLevels> decodeKtx2(std::span<const std::byte> bytes, const rhi::Capabilities &caps, std::string_view name);
core::Result<TextureLevels> loadKtx2(const std::filesystem::path &path, const rhi::Capabilities &caps);

struct Ktx2EncodeOptions
{
    TextureRole role = TextureRole::kColor; // couleur : sRGB ; données et normales : linéaire
    bool mips = true;                        // chaîne complète, filtrée sur le CPU (boîte 2x2, dans l'espace linéaire)
    bool basis = true;                       // UASTC (transcodable partout) ; false : RGBA8 non compressé
    std::uint32_t uastcLevel = 1;            // 0 (le plus rapide) à 4 (le plus lent)
    bool zstd = true;                        // supercompression Zstandard (niveau 18) du résultat
};

// Chaîne de mips RGBA8 d'une image (niveau 0 compris) : boîte 2x2 déterministe, moyennée dans l'espace linéaire pour une couleur
// sRGB, directement pour une donnée.
std::vector<DecodedImage> buildMipChain(const DecodedImage &image, ColorSpace space);

// Encode une image RGBA8 en KTX2 (outil de cuisson, ASSET-04). Résultat déterministe pour une même entrée et les mêmes options.
core::Result<std::vector<std::byte>> encodeKtx2(const DecodedImage &image, const Ktx2EncodeOptions &options = {});

} // namespace orosmo::assets
