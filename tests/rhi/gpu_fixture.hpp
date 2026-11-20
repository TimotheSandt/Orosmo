// Aides communes aux tests GPU du RHI : périphérique hors écran, ressources de test, lecture de pixels.
#pragma once

#include <doctest/doctest.h>

#include <orosmo/rhi/device.hpp>

#include "support/result_support.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace orosmo::rhi;
using orosmo::core::ErrorCode;

namespace
{

inline std::vector<std::byte> toVec(std::span<const std::byte> s) { return {s.begin(), s.end()}; }

inline std::span<const std::byte> bytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

template <typename T> inline std::span<const std::byte> bytesOf(const std::vector<T> &v) { return std::as_bytes(std::span(v.data(), v.size())); }
template <typename T> inline std::span<const std::byte> bytesOf(const T &v) { return std::as_bytes(std::span(&v, 1)); }

// Triangle plein écran sans tampon de sommets.
constexpr const char *kFullscreenVs = R"(#version 430 core
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); })";

constexpr const char *kRedFs = R"(#version 430 core
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0, 0.0, 0.0, 1.0); })";

struct Gpu
{
    std::unique_ptr<Device> device;

    explicit Gpu(Extent2D size = {8, 8})
    {
        DeviceDesc desc;
        desc.surface.size = size;
        auto created = createDevice(desc);
        if (created)
        {
            device = std::move(*created);
        }
        else
        {
            MESSAGE("no OpenGL 4.3 context: ", created.error().message);
        }
    }

    // Adopte un périphérique déjà créé (fenêtré) : aucun second contexte n'est créé, le contexte courant reste le sien.
    explicit Gpu(std::unique_ptr<Device> adopted) : device(std::move(adopted)) {}

    Device &operator*() { return *device; }
    Device *operator->() { return device.get(); }

    ShaderHandle shader(ShaderStage stage, std::string_view source, const char *name = "test")
    {
        ShaderDesc d;
        d.stage = stage;
        d.code = bytes(source);
        d.debugName = name;
        auto r = device->createShader(d);
        INFO("shader ", name, ": ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
        return *r;
    }

    TextureHandle target(Format format, Extent2D size, const char *name = "target", TextureDimension dim = TextureDimension::k2D,
                         std::uint32_t layers = 1)
    {
        TextureDesc d;
        d.dimension = dim;
        d.format = format;
        d.size = {size.width, size.height, 1};
        d.layers = layers;
        d.usage = TextureUsage::kRenderTarget | TextureUsage::kSampled | TextureUsage::kCopySrc;
        d.debugName = name;
        auto r = device->createTexture(d);
        INFO("texture ", name, ": ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
        return *r;
    }

    BufferHandle buffer(BufferUsage usage, std::span<const std::byte> data, std::uint64_t size = 0, const char *name = "buffer",
                        MemoryKind memory = MemoryKind::kGpuOnly)
    {
        BufferDesc d;
        d.size = size != 0 ? size : data.size();
        d.usage = usage;
        d.memory = memory;
        d.debugName = name;
        auto r = device->createBuffer(d, data);
        INFO("buffer ", name, ": ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
        return *r;
    }

    // Pipeline graphique : triangle plein écran, une cible RGBA8 par défaut.
    PipelineHandle pipeline(ShaderHandle vs, ShaderHandle fs, std::vector<ColorTargetState> targets = {ColorTargetState{}},
                            Format depth = Format::kUndefined, bool depthTest = false, const char *name = "pipeline")
    {
        GraphicsPipelineDesc d;
        d.vertex = vs;
        d.fragment = fs;
        d.colorTargets = std::move(targets);
        d.depthFormat = depth;
        d.depth = {depthTest, depthTest, CompareOp::kLess};
        d.raster.cull = CullMode::kNone;
        d.debugName = name;
        auto r = device->createGraphicsPipeline(d);
        INFO("pipeline ", name, ": ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
        return *r;
    }

    std::vector<std::byte> pixel(TextureHandle t, std::int32_t x, std::int32_t y, std::uint32_t mip = 0, std::uint32_t layer = 0)
    {
        TextureCopy c;
        c.mipLevel = mip;
        c.layer = layer;
        c.offset = {x, y, 0};
        c.size = {1, 1, 1};
        auto r = device->readTexture(t, c);
        INFO("readTexture: ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
        return *r;
    }

    // Exécute `record` dans un encodeur, soumet et exige le succès.
    template <typename F> void run(F &&record)
    {
        auto encoder = device->createCommandEncoder();
        record(*encoder);
        auto r = device->submit(std::move(encoder));
        INFO("submit: ", r ? "" : r.error().message);
        REQUIRE(r.has_value());
    }

    void expectNoValidationErrors() { CHECK(device->validationErrorCount() == 0); }
};

inline std::array<std::uint8_t, 4> rgba(const std::vector<std::byte> &px)
{
    REQUIRE(px.size() >= 4);
    return {static_cast<std::uint8_t>(px[0]), static_cast<std::uint8_t>(px[1]), static_cast<std::uint8_t>(px[2]), static_cast<std::uint8_t>(px[3])};
}

inline bool near(std::uint8_t a, int b, int tolerance = 1) { return std::abs(static_cast<int>(a) - b) <= tolerance; }

inline RenderPassDesc passOn(TextureHandle color, std::array<float, 4> clear = {0, 0, 0, 1}, const char *name = "pass")
{
    RenderPassDesc p;
    p.colors.push_back({.texture = color, .clearColor = clear});
    p.debugName = name;
    return p;
}

} // namespace
