// Texture 2D à durée de vie liée à un objet C++ (RAII) : stockage, échantillonneur, envoi des texels, mips, relecture.
// Plus de « slot » ni de nom d'uniforme : la liaison passe par un groupe de liaison du RHI (docs/03 §4).
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/rhi/device.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace orosmo::renderer
{

enum class FilterMode : std::uint8_t
{
    kLinear,          // linéaire + mips (trilinéaire)
    kNearest,         // plus proche voisin, sans mips
    kLinearNoMipmaps, // linéaire, sans mips
};

struct TextureInfo
{
    rhi::Format format = rhi::Format::kRGBA8Unorm;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    FilterMode filter = FilterMode::kLinear;
    rhi::AddressMode address = rhi::AddressMode::kRepeat;
    bool renderTarget = false; // peut servir d'attachement de passe (couleur ou profondeur selon `format`)
    bool storage = false;      // image de stockage (compute)
    std::string debugName;
};

class Texture
{
  public:
    Texture() = default;
    ~Texture() { reset(); }
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&other) noexcept { swap(other); }
    Texture &operator=(Texture &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            swap(other);
        }
        return *this;
    }

    // `pixels` : lignes serrées, première ligne = bas (repère du RHI) ; vide = contenu indéfini. Les mips sont générés quand
    // le filtre en demande et que des texels ont été fournis.
    static core::Result<Texture> create(rhi::Device &device, const TextureInfo &info, std::span<const std::byte> pixels = {});
    // Fichier image décodé par stb en RGBA8, retourné verticalement pour suivre le repère du RHI (comme l'ancien chargeur).
    static core::Result<Texture> fromFile(rhi::Device &device, const std::filesystem::path &file, FilterMode filter = FilterMode::kLinear,
                                          rhi::AddressMode address = rhi::AddressMode::kRepeat);

    core::Result<> upload(std::span<const std::byte> pixels); // tout le niveau 0 ; régénère les mips
    core::Result<> resize(std::uint32_t width, std::uint32_t height); // nouveau stockage, contenu indéfini ; les handles changent
    core::Result<std::vector<std::byte>> readback() const;

    void reset() noexcept;
    bool valid() const noexcept { return device_ != nullptr && !texture_.isNull(); }
    rhi::TextureHandle handle() const noexcept { return texture_; }
    rhi::SamplerHandle sampler() const noexcept { return sampler_; }
    const TextureInfo &info() const noexcept { return info_; }
    std::uint32_t width() const noexcept { return info_.width; }
    std::uint32_t height() const noexcept { return info_.height; }
    rhi::Format format() const noexcept { return info_.format; }
    std::uint32_t mipLevels() const noexcept { return mips_; }
    std::size_t byteSize() const noexcept;

  private:
    core::Result<> allocate();
    core::Result<> buildMips();
    void swap(Texture &other) noexcept
    {
        std::swap(device_, other.device_);
        std::swap(texture_, other.texture_);
        std::swap(sampler_, other.sampler_);
        std::swap(info_, other.info_);
        std::swap(mips_, other.mips_);
    }

    rhi::Device *device_ = nullptr;
    rhi::TextureHandle texture_;
    rhi::SamplerHandle sampler_;
    TextureInfo info_;
    std::uint32_t mips_ = 1;
};

} // namespace orosmo::renderer
