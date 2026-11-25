// ASSET-02 : décodage d'images, espace colorimétrique choisi par l'usage, mips filtrés dans l'espace linéaire, cache de textures.
#include <doctest/doctest.h>

#include "rhi/gpu_fixture.hpp"
#include "support/temp_dir.hpp"
#include "support/test_support.hpp"

#include <orosmo/assets/image_loader.hpp>
#include <orosmo/assets/texture_cache.hpp>
#include <orosmo/renderer/image.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace as = orosmo::assets;
namespace rd = orosmo::renderer;

namespace
{

std::vector<std::byte> fileBytes(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto *p = reinterpret_cast<const std::byte *>(raw.data());
    return {p, p + raw.size()};
}

// Damier 2x2 noir et blanc : la moyenne de son mip 1x1 vaut 0,5 en linéaire, soit 128 stocké en linéaire et 188 stocké en sRGB.
rd::Image checker()
{
    rd::Image image;
    image.width = 2;
    image.height = 2;
    image.rgba = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
    return image;
}

} // namespace

TEST_SUITE("assets/image")
{
    TEST_CASE("colour textures are sRGB, data and normal textures are linear")
    {
        CHECK((as::colorSpaceOf(as::TextureRole::kColor) == as::ColorSpace::kSrgb));
        CHECK((as::colorSpaceOf(as::TextureRole::kData) == as::ColorSpace::kLinear));
        CHECK((as::colorSpaceOf(as::TextureRole::kNormal) == as::ColorSpace::kLinear));
        CHECK((as::textureFormatFor(as::TextureRole::kColor) == Format::kRGBA8Srgb));
        CHECK((as::textureFormatFor(as::TextureRole::kNormal) == Format::kRGBA8Unorm));
    }

    TEST_CASE("decodeImage reads PNG from memory top row first; empty and corrupt inputs are errors that name the source")
    {
        tests::TempDir dir("engine_assets");
        rd::Image image;
        image.width = 2;
        image.height = 2;
        image.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 128};
        REQUIRE(rd::savePng(image, dir.path / "a.png").has_value());
        const auto bytes = fileBytes(dir.path / "a.png");

        auto decoded = as::decodeImage(bytes, "a.png");
        REQUIRE(decoded.has_value());
        CHECK(decoded->width == 2);
        CHECK(decoded->height == 2);
        CHECK(decoded->sourceChannels == 4);
        REQUIRE(decoded->rgba8.size() == 16);
        CHECK(static_cast<int>(decoded->rgba8[0]) == 255);  // haut gauche : rouge
        CHECK(static_cast<int>(decoded->rgba8[10]) == 255); // bas gauche : bleu
        CHECK(static_cast<int>(decoded->rgba8[15]) == 128); // alpha conservé

        CHECK((tests::errorOf(as::decodeImage({}, "empty")) == ErrorCode::kInvalidArgument));
        const std::vector<std::byte> junk(32, std::byte{7});
        auto bad = as::decodeImage(junk, "junk.png");
        REQUIRE_FALSE(bad.has_value());
        CHECK((bad.error().code == ErrorCode::kParse));
        CHECK(bad.error().message.find("junk.png") != std::string::npos);
        CHECK((tests::errorOf(as::loadImage(dir.path / "missing.png")) == ErrorCode::kIo));

        auto fromFile = as::loadImage(dir.path / "a.png");
        REQUIRE(fromFile.has_value());
        CHECK(fromFile->rgba8 == decoded->rgba8);
    }
}

TEST_SUITE("assets/texture")
{
    TEST_CASE("a colour texture is sRGB and its mips average in linear light; a data texture stays linear")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_assets");
            REQUIRE(rd::savePng(checker(), dir.path / "checker.png").has_value());
            auto image = as::loadImage(dir.path / "checker.png");
            REQUIRE(image.has_value());

            auto color = as::createTexture(*gpu.device, *image, {as::TextureRole::kColor}, "color");
            auto data = as::createTexture(*gpu.device, *image, {as::TextureRole::kData}, "data");
            REQUIRE(color.has_value());
            REQUIRE(data.has_value());
            CHECK((color->format() == Format::kRGBA8Srgb));
            CHECK((data->format() == Format::kRGBA8Unorm));
            REQUIRE(color->mipLevels() == 2);
            REQUIRE(data->mipLevels() == 2);

            // Niveau 0 inchangé, quel que soit le format.
            CHECK(rgba(gpu.pixel(color->handle(), 1, 0))[0] == 255);
            // Mip 1x1 : moyenne linéaire 0,5. Stockée en sRGB, elle vaut 188 (et non 128, qui serait une moyenne des octets).
            const auto srgbMip = rgba(gpu.pixel(color->handle(), 0, 0, 1));
            const auto linearMip = rgba(gpu.pixel(data->handle(), 0, 0, 1));
            INFO("sRGB mip = ", int(srgbMip[0]), ", linear mip = ", int(linearMip[0]));
            CHECK(std::abs(int(srgbMip[0]) - 188) <= 3);
            CHECK(std::abs(int(linearMip[0]) - 128) <= 2);
            gpu.expectNoValidationErrors();
        }
    }

    TEST_CASE("TextureCache shares one texture per (file, role, filter, address) and forgets textures nobody holds")
    {
        FOR_EACH_BACKEND(backend)
        {
            Gpu gpu(backend);
            REQUIRE(gpu.device);
            tests::TempDir dir("engine_assets");
            REQUIRE(rd::savePng(checker(), dir.path / "c.png").has_value());
            {
                as::TextureCache cache(*gpu.device);
                auto a = cache.load(dir.path / "c.png");
                auto b = cache.load(dir.path / "sub" / ".." / "c.png"); // même fichier, autre écriture du chemin
                REQUIRE(a.has_value());
                REQUIRE(b.has_value());
                CHECK(a->get() == b->get());
                auto linear = cache.load(dir.path / "c.png", {as::TextureRole::kData});
                REQUIRE(linear.has_value());
                CHECK(linear->get() != a->get());
                CHECK(((*linear)->format() == Format::kRGBA8Unorm));
                CHECK(cache.size() == 2);

                const auto bytes = fileBytes(dir.path / "c.png");
                auto mem = cache.loadFromMemory("model.glb#image0", bytes);
                REQUIRE(mem.has_value());
                CHECK(cache.loadFromMemory("model.glb#image0", bytes)->get() == mem->get());
                CHECK(cache.size() == 3);

                CHECK((tests::errorOf(cache.load(dir.path / "absent.png")) == ErrorCode::kIo));
                CHECK(cache.size() == 3); // un échec n'entre pas dans le cache

                linear->reset();
                mem->reset();
                CHECK(cache.purgeUnused() == 2);
                CHECK(cache.size() == 1);
                CHECK(gpu->stats().textures == 1);
            }
            CHECK(gpu->stats().textures == 0);
            gpu.expectNoValidationErrors();
        }
    }
}
