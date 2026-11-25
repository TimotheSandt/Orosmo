// Cache de textures par chemin et par usage (ASSET-02) : une image chargée deux fois avec le même usage donne la même texture ;
// la couleur est créée en sRGB, les données en linéaire ; les mips sont générés sur le GPU (filtrage dans l'espace linéaire).
#pragma once

#include "orosmo/assets/image_loader.hpp"
#include "orosmo/assets/ktx2.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/renderer/texture.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>

namespace orosmo::assets
{

struct TextureRequest
{
    TextureRole role = TextureRole::kColor;
    renderer::FilterMode filter = renderer::FilterMode::kLinear; // kLinear : mips générés
    rhi::AddressMode address = rhi::AddressMode::kRepeat;
};

// Crée une texture à partir d'une image déjà décodée (format choisi par `request.role`).
core::Result<renderer::Texture> createTexture(rhi::Device &device, const DecodedImage &image, const TextureRequest &request,
                                              std::string debugName);
// Crée une texture à partir de niveaux déjà prêts (KTX2 : compressés ou RGBA8) ; le format vient du fichier, pas de l'usage.
core::Result<renderer::Texture> createTexture(rhi::Device &device, const TextureLevels &levels, const TextureRequest &request,
                                              std::string debugName);

class TextureCache
{
  public:
    explicit TextureCache(rhi::Device &device) : device_(&device) {}

    // Fichier image (PNG, JPG, TGA, BMP, ou KTX2 reconnu à son en-tête, ASSET-03). La clé est le chemin rendu absolu et normalisé
    // (`weakly_canonical`), plus l'usage, le filtre et l'adressage.
    core::Result<std::shared_ptr<renderer::Texture>> load(const std::filesystem::path &path, const TextureRequest &request = {});
    // Image encodée en mémoire (par exemple incluse dans un .glb) ; `key` l'identifie dans le cache.
    core::Result<std::shared_ptr<renderer::Texture>> loadFromMemory(const std::string &key, std::span<const std::byte> encoded,
                                                                    const TextureRequest &request = {});

    // ASSET-05 : relit les fichiers modifiés sur le disque depuis le dernier appel (date et taille) et remplace leur contenu dans la
    // texture partagée. Même taille et même nombre de niveaux : envoi en place (handle conservé, les matériaux voient le nouveau
    // contenu sans être recréés) ; sinon la texture partagée est recréée et ses matériaux doivent reconstruire leur groupe. Un fichier
    // illisible garde l'ancienne texture et l'erreur est journalisée. Renvoie le nombre de textures rechargées.
    std::size_t reloadChanged();

    std::size_t size() const noexcept { return entries_.size(); }
    // Oublie les textures que plus personne ne tient (seul le cache les garde). Renvoie le nombre d'entrées retirées.
    std::size_t purgeUnused();
    void clear() noexcept { entries_.clear(); }

  private:
    using Key = std::tuple<std::string, TextureRole, renderer::FilterMode, rhi::AddressMode>;
    struct Entry
    {
        std::shared_ptr<renderer::Texture> texture;
        std::filesystem::path file; // vide pour une image en mémoire
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
    };
    core::Result<renderer::Texture> build(std::span<const std::byte> encoded, const TextureRequest &request, const std::string &name);

    rhi::Device *device_;
    std::map<Key, Entry> entries_;
};

} // namespace orosmo::assets
