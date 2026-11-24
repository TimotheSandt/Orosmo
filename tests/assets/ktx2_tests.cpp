// ASSET-03 (textures KTX2 : BC7, ASTC, repli RGBA8) et ASSET-05 (rechargement à chaud des textures du cache).
#include <doctest/doctest.h>

#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"
#include "support/test_support.hpp"

#include <orosmo/assets/ktx2.hpp>
#include <orosmo/assets/texture_cache.hpp>
#include <orosmo/renderer/image.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

namespace
{

as::DecodedImage solid(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
    as::DecodedImage image;
    image.width = w;
    image.height = h;
    image.sourceChannels = 4;
    for (std::uint32_t i = 0; i < w * h; ++i)
    {
        for (const std::uint8_t c : {r, g, b, a})
        {
            image.rgba8.push_back(static_cast<std::byte>(c));
        }
    }
    return image;
}

// Dégradé non uniforme, pour vérifier un aller-retour sans perte.
as::DecodedImage gradient(std::uint32_t w, std::uint32_t h)
{
    as::DecodedImage image = solid(w, h, 0, 0, 0);
    for (std::uint32_t y = 0; y < h; ++y)
    {
        for (std::uint32_t x = 0; x < w; ++x)
        {
            auto *p = &image.rgba8[(y * w + x) * 4];
            p[0] = static_cast<std::byte>((x * 37 + 11) & 255);
            p[1] = static_cast<std::byte>((y * 53 + 7) & 255);
            p[2] = static_cast<std::byte>((x * 5 + y * 3) & 255);
            p[3] = static_cast<std::byte>(255 - ((x + y) & 63));
        }
    }
    return image;
}

std::vector<std::byte> fileBytes(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto *p = reinterpret_cast<const std::byte *>(raw.data());
    return {p, p + raw.size()};
}

void writeBytes(const std::filesystem::path &path, std::span<const std::byte> data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

rd::Image solidPng(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    rd::Image image;
    image.width = w;
    image.height = h;
    for (std::uint32_t i = 0; i < w * h; ++i)
    {
        for (const std::uint8_t c : {r, g, b, std::uint8_t{255}})
        {
            image.rgba.push_back(c);
        }
    }
    return image;
}

// Force une date de modification différente de l'ancienne (le système de fichiers peut avoir une résolution grossière).
void bumpTime(const std::filesystem::path &file, int seconds)
{
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(seconds));
}

orosmo::rhi::Capabilities capsWith(bool bc7, bool astc)
{
    orosmo::rhi::Capabilities caps;
    caps.bc7Textures = bc7;
    caps.astcTextures = astc;
    return caps;
}

// Échantillonne `texture` plein écran dans une cible RGBA8 8x8 et renvoie le pixel central.
std::array<std::uint8_t, 4> sampleCenter(Gpu &gpu, const rd::Texture &texture)
{
    const char *fs = R"(#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(tex, gl_FragCoord.xy / 8.0); })";
    const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, fs));
    BindGroupDesc bd;
    bd.entries = {{.binding = 0, .type = BindingType::kSampledTexture, .texture = texture.handle(), .sampler = texture.sampler()}};
    auto group = gpu->createBindGroup(bd);
    REQUIRE(group.has_value());
    const TextureHandle out = gpu.target(Format::kRGBA8Unorm, {8, 8}, "ktx.out");
    gpu.run(
        [&](CommandEncoder &e)
        {
            e.beginRenderPass(passOn(out));
            e.setPipeline(pipe);
            e.setBindGroup(0, *group);
            e.draw(3);
            e.endRenderPass();
        });
    return rgba(gpu.pixel(out, 4, 4));
}

bool closeTo(const std::array<std::uint8_t, 4> &px, int r, int g, int b, int tol)
{
    return near(px[0], r, tol) && near(px[1], g, tol) && near(px[2], b, tol);
}

} // namespace

TEST_SUITE("assets/ktx2")
{
    TEST_CASE("compressed formats are described by blocks")
    {
        for (const Format f : {Format::kBC7Unorm, Format::kBC7Srgb, Format::kASTC4x4Unorm, Format::kASTC4x4Srgb})
        {
            const FormatInfo info = formatInfo(f);
            CHECK(info.compressed());
            CHECK(info.blockExtent == 4);
            CHECK(info.bytesPerBlock == 16);
            CHECK(info.bytesPerTexel == 0);
            CHECK(imageBytes(f, 5, 3) == 32); // 2x1 blocs
            CHECK(imageBytes(f, 4, 4) == 16);
            CHECK(imageBytes(f, 1, 1) == 16);
            CHECK(imageBytes(f, 8, 8) == 64);
        }
        CHECK_FALSE(formatInfo(Format::kRGBA8Unorm).compressed());
        CHECK(formatInfo(Format::kRGBA8Unorm).blockExtent == 1);
        CHECK(imageBytes(Format::kRGBA8Unorm, 5, 3) == 60);
    }

    TEST_CASE("blockAligned accepts block multiples and edges of the level only")
    {
        const Format f = Format::kBC7Unorm;
        CHECK(blockAligned(f, 0, 0, 8, 8, 8, 8));
        CHECK(blockAligned(f, 4, 4, 4, 4, 8, 8));
        CHECK_FALSE(blockAligned(f, 2, 0, 4, 4, 8, 8));  // décalage hors bloc
        CHECK_FALSE(blockAligned(f, 0, 0, 3, 4, 8, 8));  // largeur hors bloc, pas au bord
        CHECK_FALSE(blockAligned(f, 0, 2, 4, 4, 8, 8));
        CHECK(blockAligned(f, 4, 0, 3, 4, 7, 8));        // jusqu'au bord du niveau
        CHECK(blockAligned(f, 0, 0, 2, 2, 2, 2));        // niveau plus petit qu'un bloc
        CHECK(blockAligned(Format::kRGBA8Unorm, 1, 3, 5, 7, 100, 100));
    }

    TEST_CASE("isKtx2 recognises the header, not PNG")
    {
        auto encoded = as::encodeKtx2(solid(4, 4, 1, 2, 3), {.role = as::TextureRole::kColor, .mips = false, .basis = false, .uastcLevel = 1, .zstd = true});
        REQUIRE(encoded.has_value());
        CHECK(as::isKtx2(*encoded));
        CHECK_FALSE(as::isKtx2({}));
        tests::TempDir dir("engine_ktx");
        REQUIRE(rd::savePng(solidPng(2, 2, 1, 2, 3), dir.path / "a.png").has_value());
        CHECK_FALSE(as::isKtx2(fileBytes(dir.path / "a.png")));
        CHECK_FALSE(as::isKtx2(std::vector<std::byte>(40, std::byte{0})));
    }

    TEST_CASE("transcodeTargetFor prefers BC7, then ASTC, then RGBA8")
    {
        CHECK((as::transcodeTargetFor(capsWith(true, true), true) == Format::kBC7Srgb));
        CHECK((as::transcodeTargetFor(capsWith(true, false), false) == Format::kBC7Unorm));
        CHECK((as::transcodeTargetFor(capsWith(false, true), true) == Format::kASTC4x4Srgb));
        CHECK((as::transcodeTargetFor(capsWith(false, true), false) == Format::kASTC4x4Unorm));
        CHECK((as::transcodeTargetFor(capsWith(false, false), true) == Format::kRGBA8Srgb));
        CHECK((as::transcodeTargetFor(capsWith(false, false), false) == Format::kRGBA8Unorm));
    }

    TEST_CASE("uncompressed KTX2 round trip keeps pixels and the mip chain")
    {
        const as::DecodedImage src = gradient(8, 4);
        auto encoded = as::encodeKtx2(src, {.role = as::TextureRole::kData, .mips = true, .basis = false, .uastcLevel = 1, .zstd = false});
        REQUIRE(encoded.has_value());
        auto decoded = as::decodeKtx2(*encoded, capsWith(false, false), "grad.ktx2");
        REQUIRE(decoded.has_value());
        CHECK((decoded->format == Format::kRGBA8Unorm));
        CHECK(decoded->width == 8);
        CHECK(decoded->height == 4);
        REQUIRE(decoded->levels.size() == 4); // 8x4, 4x2, 2x1, 1x1
        CHECK(decoded->levels[0] == src.rgba8);
        CHECK(decoded->levels[1].size() == 4 * 2 * 4);
        CHECK(decoded->levels[2].size() == 2 * 1 * 4);
        CHECK(decoded->levels[3].size() == 4);

        // Rôle couleur : sRGB ; sans mips : un seul niveau ; avec zstd : même contenu.
        auto color = as::encodeKtx2(src, {.role = as::TextureRole::kColor, .mips = false, .basis = false, .uastcLevel = 1, .zstd = true});
        REQUIRE(color.has_value());
        auto colorDecoded = as::decodeKtx2(*color, capsWith(true, true), "c.ktx2");
        REQUIRE(colorDecoded.has_value());
        CHECK((colorDecoded->format == Format::kRGBA8Srgb));
        REQUIRE(colorDecoded->levels.size() == 1);
        CHECK(colorDecoded->levels[0] == src.rgba8);
    }

    TEST_CASE("encodeKtx2 is deterministic")
    {
        const as::DecodedImage src = gradient(8, 8);
        auto a = as::encodeKtx2(src);
        auto b = as::encodeKtx2(src);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(*a == *b);
    }

    TEST_CASE("buildMipChain filters in linear space for colour, directly for data")
    {
        as::DecodedImage checker = solid(2, 2, 0, 0, 0);
        for (const std::size_t i : {4u, 8u}) // pixels 1 et 2 blancs
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                checker.rgba8[i + c] = std::byte{255};
            }
        }
        const auto srgb = as::buildMipChain(checker, as::ColorSpace::kSrgb);
        const auto linear = as::buildMipChain(checker, as::ColorSpace::kLinear);
        REQUIRE(srgb.size() == 2);
        REQUIRE(linear.size() == 2);
        CHECK(srgb[0].rgba8 == checker.rgba8);
        REQUIRE(srgb[1].rgba8.size() == 4);
        CHECK(srgb[1].width == 1);
        CHECK(srgb[1].height == 1);
        CHECK(near(static_cast<std::uint8_t>(srgb[1].rgba8[0]), 188, 1));
        CHECK(near(static_cast<std::uint8_t>(linear[1].rgba8[0]), 128, 1));
        CHECK(static_cast<int>(srgb[1].rgba8[3]) == 255);
    }

    TEST_CASE("UASTC KTX2 transcodes to the best format the device samples")
    {
        const as::DecodedImage src = solid(8, 8, 200, 100, 50);
        auto encoded = as::encodeKtx2(src, {.role = as::TextureRole::kColor, .mips = true, .basis = true, .uastcLevel = 1, .zstd = true});
        REQUIRE(encoded.has_value());
        CHECK(as::isKtx2(*encoded));

        auto bc7 = as::decodeKtx2(*encoded, capsWith(true, true), "u.ktx2");
        REQUIRE(bc7.has_value());
        CHECK((bc7->format == Format::kBC7Srgb));
        CHECK(bc7->width == 8);
        REQUIRE(bc7->levels.size() == 4);
        CHECK(bc7->levels[0].size() == imageBytes(Format::kBC7Srgb, 8, 8));
        CHECK(bc7->levels[3].size() == 16); // 1x1 : un bloc

        auto astc = as::decodeKtx2(*encoded, capsWith(false, true), "u.ktx2");
        REQUIRE(astc.has_value());
        CHECK((astc->format == Format::kASTC4x4Srgb));
        CHECK(astc->levels[0].size() == 64);

        auto raw = as::decodeKtx2(*encoded, capsWith(false, false), "u.ktx2");
        REQUIRE(raw.has_value());
        CHECK((raw->format == Format::kRGBA8Srgb));
        REQUIRE(raw->levels.size() == 4);
        REQUIRE(raw->levels[0].size() == 8 * 8 * 4);
        int worst = 0;
        for (std::size_t i = 0; i < raw->levels[0].size(); ++i)
        {
            worst = std::max(worst, std::abs(static_cast<int>(raw->levels[0][i]) - static_cast<int>(src.rgba8[i])));
        }
        CHECK(worst <= 8);

        // Données : variante linéaire.
        auto data = as::encodeKtx2(src, {.role = as::TextureRole::kData, .mips = false, .basis = true, .uastcLevel = 1, .zstd = false});
        REQUIRE(data.has_value());
        auto dataBc7 = as::decodeKtx2(*data, capsWith(true, false), "d.ktx2");
        REQUIRE(dataBc7.has_value());
        CHECK((dataBc7->format == Format::kBC7Unorm));
        CHECK(dataBc7->levels.size() == 1);
    }

    TEST_CASE("decodeKtx2 and loadKtx2 reject garbage and missing files")
    {
        const std::vector<std::byte> junk(128, std::byte{0x5a});
        auto bad = as::decodeKtx2(junk, capsWith(true, true), "junk.ktx2");
        REQUIRE_FALSE(bad.has_value());
        CHECK_FALSE(bad.error().message.empty());
        CHECK_FALSE(as::decodeKtx2({}, capsWith(true, true), "empty.ktx2").has_value());

        // En-tête valide mais contenu tronqué.
        auto encoded = as::encodeKtx2(gradient(8, 8));
        REQUIRE(encoded.has_value());
        encoded->resize(encoded->size() / 2);
        CHECK_FALSE(as::decodeKtx2(*encoded, capsWith(true, true), "cut.ktx2").has_value());

        tests::TempDir dir("engine_ktx");
        CHECK_FALSE(as::loadKtx2(dir.path / "absent.ktx2", capsWith(true, true)).has_value());
    }

    TEST_CASE("a UASTC KTX2 loaded through the cache is sampled with its colour on every backend")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_ktx");
            auto encoded = as::encodeKtx2(solid(8, 8, 200, 100, 50), {.role = as::TextureRole::kData, .mips = true, .basis = true, .uastcLevel = 1, .zstd = true});
            REQUIRE(encoded.has_value());
            writeBytes(dir.path / "solid.ktx2", *encoded);

            const auto &caps = gpu->capabilities();
            MESSAGE(tests::backendName(backend), ": bc7=", int(caps.bc7Textures), " astc=", int(caps.astcTextures));
            as::TextureCache cache(*gpu.device);
            auto texture = cache.load(dir.path / "solid.ktx2", {as::TextureRole::kData});
            REQUIRE(texture.has_value());
            CHECK((*texture)->width() == 8);
            CHECK((*texture)->mipLevels() == 4);
            const Format expected = caps.bc7Textures ? Format::kBC7Unorm : caps.astcTextures ? Format::kASTC4x4Unorm : Format::kRGBA8Unorm;
            CHECK(((*texture)->format() == expected));
            const auto px = sampleCenter(gpu, **texture);
            INFO("sampled ", int(px[0]), ",", int(px[1]), ",", int(px[2]));
            CHECK(closeTo(px, 200, 100, 50, 6));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("a compressed texture is sampled only: render target and storage are refused")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            if (!gpu->capabilities().bc7Textures)
            {
                MESSAGE("no BC7 on ", tests::backendName(backend));
                continue;
            }
            for (const TextureUsage usage : {TextureUsage::kSampled | TextureUsage::kRenderTarget, TextureUsage::kSampled | TextureUsage::kStorage})
            {
                TextureDesc d;
                d.format = Format::kBC7Unorm;
                d.size = {8, 8, 1};
                d.usage = usage;
                CHECK_FALSE(gpu->createTexture(d).has_value());
            }
            TextureDesc ok;
            ok.format = Format::kBC7Unorm;
            ok.size = {8, 8, 1};
            ok.usage = TextureUsage::kSampled | TextureUsage::kCopyDst;
            auto tex = gpu->createTexture(ok);
            REQUIRE(tex.has_value());
            // Envoi d'un niveau complet de blocs accepté ; lecture refusée.
            const std::vector<std::byte> blocks(imageBytes(Format::kBC7Unorm, 8, 8), std::byte{0});
            CHECK(gpu->updateTexture(*tex, {}, blocks).has_value());
            CHECK_FALSE(gpu->readTexture(*tex).has_value());
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("reloadChanged rewrites a changed file in place, keeping the handle")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_ktx");
            const auto file = dir.path / "t.png";
            REQUIRE(rd::savePng(solidPng(8, 8, 255, 0, 0), file).has_value());
            as::TextureCache cache(*gpu.device);
            auto texture = cache.load(file, {as::TextureRole::kData});
            REQUIRE(texture.has_value());
            const TextureHandle handle = (*texture)->handle();
            CHECK(closeTo(sampleCenter(gpu, **texture), 255, 0, 0, 1));

            CHECK(cache.reloadChanged() == 0); // rien n'a changé

            REQUIRE(rd::savePng(solidPng(8, 8, 0, 0, 255), file).has_value());
            bumpTime(file, 2);
            CHECK(cache.reloadChanged() == 1);
            CHECK(((*texture)->handle() == handle));
            CHECK(closeTo(sampleCenter(gpu, **texture), 0, 0, 255, 1));
            CHECK(cache.reloadChanged() == 0); // déjà relu
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("reloadChanged recreates the texture when the size changes")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_ktx");
            const auto file = dir.path / "t.png";
            REQUIRE(rd::savePng(solidPng(8, 8, 255, 0, 0), file).has_value());
            as::TextureCache cache(*gpu.device);
            auto texture = cache.load(file, {as::TextureRole::kData});
            REQUIRE(texture.has_value());
            REQUIRE((*texture)->width() == 8);

            REQUIRE(rd::savePng(solidPng(16, 4, 0, 255, 0), file).has_value());
            bumpTime(file, 2);
            CHECK(cache.reloadChanged() == 1);
            CHECK((*texture)->width() == 16); // le shared_ptr du cache voit la nouvelle taille
            CHECK((*texture)->height() == 4);
            CHECK(gpu->stats().textures == 1);
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("reloadChanged keeps the old texture when the file becomes unreadable")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_ktx");
            const auto file = dir.path / "t.png";
            REQUIRE(rd::savePng(solidPng(8, 8, 255, 0, 0), file).has_value());
            as::TextureCache cache(*gpu.device);
            auto texture = cache.load(file, {as::TextureRole::kData});
            REQUIRE(texture.has_value());
            const TextureHandle handle = (*texture)->handle();

            writeBytes(file, std::vector<std::byte>(64, std::byte{9}));
            bumpTime(file, 2);
            CHECK(cache.reloadChanged() == 0);
            CHECK(((*texture)->handle() == handle));
            CHECK((*texture)->width() == 8);
            CHECK(closeTo(sampleCenter(gpu, **texture), 255, 0, 0, 1));
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("reloadChanged reloads a KTX2 file in place")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_ktx");
            const auto file = dir.path / "t.ktx2";
            const as::Ktx2EncodeOptions options{.role = as::TextureRole::kData, .mips = true, .basis = true, .uastcLevel = 1, .zstd = true};
            auto first = as::encodeKtx2(solid(8, 8, 200, 20, 20), options);
            REQUIRE(first.has_value());
            writeBytes(file, *first);
            as::TextureCache cache(*gpu.device);
            auto texture = cache.load(file, {as::TextureRole::kData});
            REQUIRE(texture.has_value());
            const TextureHandle handle = (*texture)->handle();
            CHECK(closeTo(sampleCenter(gpu, **texture), 200, 20, 20, 6));

            auto second = as::encodeKtx2(solid(8, 8, 20, 20, 200), options);
            REQUIRE(second.has_value());
            writeBytes(file, *second);
            bumpTime(file, 2);
            CHECK(cache.reloadChanged() == 1);
            CHECK(((*texture)->handle() == handle));
            CHECK(closeTo(sampleCenter(gpu, **texture), 20, 20, 200, 6));
            gpu.expectNoValidationErrors();
        }
    }
}
