#include "orosmo/assets/ktx2.hpp"

// vcpkg lie libktx en statique sur toutes les cibles (x64-mingw-static compris).
// Sans KHRONOS_STATIC, ktx.h déclare l'API en dllimport sous Windows et l'édition
// de liens cherche des symboles __imp_ktx* qui n'existent pas.
#ifndef KHRONOS_STATIC
#define KHRONOS_STATIC
#endif
#include <ktx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

namespace
{

// Identifiants de format Vulkan inscrits dans l'en-tête KTX2 (spécification KTX 2.0, champ vkFormat) : le fichier en porte la
// valeur numérique ; le moteur ne les passe jamais à Vulkan (règle 4 : aucune API graphique hors de src/rhi/).
constexpr ktx_uint32_t kKtxRgba8Unorm = 37;
constexpr ktx_uint32_t kKtxRgba8Srgb = 43;

constexpr std::array<std::uint8_t, 12> kKtx2Magic{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

struct KtxDeleter
{
    void operator()(ktxTexture2 *t) const noexcept { ktxTexture2_Destroy(t); }
};
using KtxPtr = std::unique_ptr<ktxTexture2, KtxDeleter>;

std::string ktxMessage(std::string_view what, std::string_view name, KTX_error_code code)
{
    return std::string(what) + " (" + std::string(name) + "): " + ktxErrorString(code);
}

bool astc(rhi::Format f) noexcept { return f == rhi::Format::kASTC4x4Unorm || f == rhi::Format::kASTC4x4Srgb; }
bool bc7(rhi::Format f) noexcept { return f == rhi::Format::kBC7Unorm || f == rhi::Format::kBC7Srgb; }

bool sampleable(const rhi::Capabilities &caps, rhi::Format f) noexcept
{
    return bc7(f) ? caps.bc7Textures : astc(f) ? caps.astcTextures : true;
}

// Format d'un KTX2 déjà compressé ou non compressé, lu dans son descripteur de format (DFD), pas dans son champ vkFormat.
rhi::Format storedFormat(ktxTexture2 *t, bool srgb)
{
    const khr_df_model_e model = ktxTexture2_GetColorModel_e(t);
    if (model == KHR_DF_MODEL_BC7)
    {
        return srgb ? rhi::Format::kBC7Srgb : rhi::Format::kBC7Unorm;
    }
    if (model == KHR_DF_MODEL_ASTC)
    {
        const ktx_uint32_t *bdb = t->pDfd + 1;
        if (KHR_DFDVAL(bdb, TEXELBLOCKDIMENSION0) == 3 && KHR_DFDVAL(bdb, TEXELBLOCKDIMENSION1) == 3)
        {
            return srgb ? rhi::Format::kASTC4x4Srgb : rhi::Format::kASTC4x4Unorm;
        }
        return rhi::Format::kUndefined;
    }
    if (model == KHR_DF_MODEL_RGBSDA && !t->isCompressed && ktxTexture_GetElementSize(ktxTexture(t)) == 4 &&
        ktxTexture2_GetNumComponents(t) == 4)
    {
        // Les octets sont envoyés tels quels : seul l'ordre R, G, B, A (un octet chacun) du DFD est lu comme RGBA8.
        const ktx_uint32_t *bdb = t->pDfd + 1;
        if (KHR_DFDSAMPLECOUNT(bdb) != 4)
        {
            return rhi::Format::kUndefined;
        }
        constexpr std::array<std::uint32_t, 4> kOrder{KHR_DF_CHANNEL_RGBSDA_RED, KHR_DF_CHANNEL_RGBSDA_GREEN, KHR_DF_CHANNEL_RGBSDA_BLUE,
                                                      KHR_DF_CHANNEL_RGBSDA_ALPHA};
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            if (KHR_DFDSVAL(bdb, i, CHANNELID) != kOrder[i] || KHR_DFDSVAL(bdb, i, BITOFFSET) != 8 * i || KHR_DFDSVAL(bdb, i, BITLENGTH) != 7)
            {
                return rhi::Format::kUndefined;
            }
        }
        return srgb ? rhi::Format::kRGBA8Srgb : rhi::Format::kRGBA8Unorm;
    }
    return rhi::Format::kUndefined;
}

float toLinear(std::uint8_t v) noexcept
{
    const float c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t fromLinear(float l) noexcept
{
    l = std::clamp(l, 0.0f, 1.0f);
    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

} // namespace

bool isKtx2(std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= kKtx2Magic.size() && std::memcmp(bytes.data(), kKtx2Magic.data(), kKtx2Magic.size()) == 0;
}

rhi::Format transcodeTargetFor(const rhi::Capabilities &caps, bool srgb) noexcept
{
    if (caps.bc7Textures)
    {
        return srgb ? rhi::Format::kBC7Srgb : rhi::Format::kBC7Unorm;
    }
    if (caps.astcTextures)
    {
        return srgb ? rhi::Format::kASTC4x4Srgb : rhi::Format::kASTC4x4Unorm;
    }
    return srgb ? rhi::Format::kRGBA8Srgb : rhi::Format::kRGBA8Unorm;
}

core::Result<TextureLevels> decodeKtx2(std::span<const std::byte> bytes, const rhi::Capabilities &caps, std::string_view name)
{
    if (!isKtx2(bytes))
    {
        return makeError(ErrorCode::kParse, "decodeKtx2: not a KTX2 file (" + std::string(name) + ")");
    }
    ktxTexture2 *raw = nullptr;
    if (const KTX_error_code r = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t *>(bytes.data()), bytes.size(),
                                                              KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        r != KTX_SUCCESS)
    {
        return makeError(ErrorCode::kParse, ktxMessage("decodeKtx2: cannot read", name, r));
    }
    KtxPtr texture(raw);
    if (texture->numDimensions != 2 || texture->isArray || texture->isCubemap || texture->numLayers > 1 || texture->numFaces > 1)
    {
        return makeError(ErrorCode::kUnsupported, "decodeKtx2: only 2D textures are supported (" + std::string(name) + ")");
    }
    // Le contrat est « première ligne en haut » : un fichier dont l'origine est en bas (ou à gauche) est refusé, sans réordonner les lignes.
    if (texture->orientation.x == KTX_ORIENT_X_LEFT || texture->orientation.y == KTX_ORIENT_Y_UP)
    {
        return makeError(ErrorCode::kUnsupported, "decodeKtx2: only top-left origin (KTXorientation=rd) is supported (" + std::string(name) + ")");
    }
    const bool srgb = ktxTexture2_GetTransferFunction_e(texture.get()) == KHR_DF_TRANSFER_SRGB;
    rhi::Format format = rhi::Format::kUndefined;
    if (ktxTexture2_NeedsTranscoding(texture.get()))
    {
        format = transcodeTargetFor(caps, srgb);
        const ktx_transcode_fmt_e target = bc7(format) ? KTX_TTF_BC7_RGBA : astc(format) ? KTX_TTF_ASTC_4x4_RGBA : KTX_TTF_RGBA32;
        if (const KTX_error_code r = ktxTexture2_TranscodeBasis(texture.get(), target, 0); r != KTX_SUCCESS)
        {
            return makeError(ErrorCode::kParse, ktxMessage("decodeKtx2: cannot transcode", name, r));
        }
    }
    else
    {
        format = storedFormat(texture.get(), srgb);
        if (format == rhi::Format::kUndefined)
        {
            return makeError(ErrorCode::kUnsupported,
                             "decodeKtx2: format is neither Basis Universal, BC7, ASTC 4x4 nor RGBA8 (" + std::string(name) + ")");
        }
        if (!sampleable(caps, format))
        {
            return makeError(ErrorCode::kUnsupported, std::string("decodeKtx2: this device cannot sample ") + rhi::toString(format) + " (" +
                                                          std::string(name) + ")");
        }
    }

    TextureLevels out;
    out.format = format;
    out.width = texture->baseWidth;
    out.height = texture->baseHeight;
    out.levels.reserve(texture->numLevels);
    for (ktx_uint32_t level = 0; level < texture->numLevels; ++level)
    {
        ktx_size_t offset = 0;
        if (const KTX_error_code r = ktxTexture2_GetImageOffset(texture.get(), level, 0, 0, &offset); r != KTX_SUCCESS)
        {
            return makeError(ErrorCode::kParse, ktxMessage("decodeKtx2: missing level", name, r));
        }
        const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(texture.get()), level);
        const std::uint64_t expected =
            rhi::imageBytes(format, std::max(1u, out.width >> level), std::max(1u, out.height >> level));
        if (size != expected || offset + size > texture->dataSize)
        {
            return makeError(ErrorCode::kParse, "decodeKtx2: level " + std::to_string(level) + " has an unexpected size (" + std::string(name) + ")");
        }
        const auto *p = reinterpret_cast<const std::byte *>(texture->pData + offset);
        out.levels.emplace_back(p, p + size);
    }
    return out;
}

core::Result<TextureLevels> loadKtx2(const std::filesystem::path &path, const rhi::Capabilities &caps)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "loadKtx2: cannot open " + path.generic_string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        return makeError(ErrorCode::kIo, "loadKtx2: cannot read " + path.generic_string());
    }
    return decodeKtx2(std::as_bytes(std::span(bytes)), caps, path.generic_string());
}

std::vector<DecodedImage> buildMipChain(const DecodedImage &image, ColorSpace space)
{
    std::vector<DecodedImage> chain;
    // Image incohérente (tampon plus court que largeur x hauteur x 4) : chaîne vide, rien n'est lu hors du tampon.
    if (image.empty() || image.rgba8.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return chain;
    }
    chain.push_back(image);
    const bool srgb = space == ColorSpace::kSrgb;
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        const DecodedImage &src = chain.back();
        DecodedImage dst;
        dst.width = std::max(1u, src.width / 2);
        dst.height = std::max(1u, src.height / 2);
        dst.sourceChannels = src.sourceChannels;
        dst.rgba8.resize(static_cast<std::size_t>(dst.width) * dst.height * 4);
        const auto at = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c)
        {
            x = std::min(x, src.width - 1);
            y = std::min(y, src.height - 1);
            return static_cast<std::uint8_t>(src.rgba8[(static_cast<std::size_t>(y) * src.width + x) * 4 + c]);
        };
        for (std::uint32_t y = 0; y < dst.height; ++y)
        {
            for (std::uint32_t x = 0; x < dst.width; ++x)
            {
                for (std::uint32_t c = 0; c < 4; ++c)
                {
                    const std::array<std::uint8_t, 4> v{at(2 * x, 2 * y, c), at(2 * x + 1, 2 * y, c), at(2 * x, 2 * y + 1, c),
                                                        at(2 * x + 1, 2 * y + 1, c)};
                    std::uint8_t result = 0;
                    if (srgb && c < 3) // l'alpha reste linéaire
                    {
                        result = fromLinear((toLinear(v[0]) + toLinear(v[1]) + toLinear(v[2]) + toLinear(v[3])) * 0.25f);
                    }
                    else
                    {
                        result = static_cast<std::uint8_t>((v[0] + v[1] + v[2] + v[3] + 2) / 4);
                    }
                    dst.rgba8[(static_cast<std::size_t>(y) * dst.width + x) * 4 + c] = static_cast<std::byte>(result);
                }
            }
        }
        chain.push_back(std::move(dst));
    }
    return chain;
}

core::Result<std::vector<std::byte>> encodeKtx2(const DecodedImage &image, const Ktx2EncodeOptions &options)
{
    if (image.empty() || image.rgba8.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return makeError(ErrorCode::kInvalidArgument, "encodeKtx2: empty or inconsistent image");
    }
    const ColorSpace space = colorSpaceOf(options.role);
    std::vector<DecodedImage> chain = options.mips ? buildMipChain(image, space) : std::vector<DecodedImage>{image};

    // Champs dans l'ordre de ktxTextureCreateInfo : glInternalformat, vkFormat, pDfd, largeur, hauteur, profondeur, dimensions,
    // niveaux, couches, faces, tableau, mips générés au chargement.
    ktxTextureCreateInfo info{0,       space == ColorSpace::kSrgb ? kKtxRgba8Srgb : kKtxRgba8Unorm,
                              nullptr, image.width,
                              image.height, 1,
                              2,       static_cast<ktx_uint32_t>(chain.size()),
                              1,       1,
                              KTX_FALSE, KTX_FALSE};
    ktxTexture2 *raw = nullptr;
    if (const KTX_error_code r = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw); r != KTX_SUCCESS)
    {
        return makeError(ErrorCode::kInvalidArgument, ktxMessage("encodeKtx2: cannot create", "image", r));
    }
    KtxPtr texture(raw);
    // Origine explicite en haut à gauche (« rd ») : c'est l'ordre des lignes de nos niveaux, et la seule orientation que decodeKtx2 accepte.
    ktxHashList_AddKVPair(&texture->kvDataHead, KTX_ORIENTATION_KEY, 3, "rd");
    for (std::size_t level = 0; level < chain.size(); ++level)
    {
        const auto &px = chain[level].rgba8;
        if (const KTX_error_code r = ktxTexture_SetImageFromMemory(ktxTexture(texture.get()), static_cast<ktx_uint32_t>(level), 0, 0,
                                                                   reinterpret_cast<const ktx_uint8_t *>(px.data()), px.size());
            r != KTX_SUCCESS)
        {
            return makeError(ErrorCode::kInvalidArgument, ktxMessage("encodeKtx2: cannot set level", "image", r));
        }
    }
    if (options.basis)
    {
        ktxBasisParams params{};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 1; // résultat identique d'une exécution à l'autre
        params.uastcFlags = std::min<ktx_uint32_t>(options.uastcLevel, KTX_PACK_UASTC_MAX_LEVEL);
        if (const KTX_error_code r = ktxTexture2_CompressBasisEx(texture.get(), &params); r != KTX_SUCCESS)
        {
            return makeError(ErrorCode::kUnknown, ktxMessage("encodeKtx2: Basis Universal compression failed", "image", r));
        }
    }
    if (options.zstd)
    {
        if (const KTX_error_code r = ktxTexture2_DeflateZstd(texture.get(), 18); r != KTX_SUCCESS)
        {
            return makeError(ErrorCode::kUnknown, ktxMessage("encodeKtx2: Zstandard supercompression failed", "image", r));
        }
    }
    ktx_uint8_t *written = nullptr;
    ktx_size_t size = 0;
    if (const KTX_error_code r = ktxTexture_WriteToMemory(ktxTexture(texture.get()), &written, &size); r != KTX_SUCCESS)
    {
        return makeError(ErrorCode::kUnknown, ktxMessage("encodeKtx2: cannot write", "image", r));
    }
    std::unique_ptr<ktx_uint8_t, decltype(&std::free)> owned(written, &std::free);
    const auto *p = reinterpret_cast<const std::byte *>(written);
    return std::vector<std::byte>(p, p + size);
}

} // namespace orosmo::assets
