// REN-37 (données) : BlendMode et l'état de pipeline qui en découle.
#include <doctest/doctest.h>

#include "support/result_support.hpp"

#include <orosmo/renderer/blend_mode.hpp>

using namespace orosmo::renderer;
using orosmo::rhi::BlendFactor;

TEST_SUITE("renderer/blend-mode")
{
    TEST_CASE("opaque-like modes write depth and do not blend")
    {
        for (const BlendMode m : {BlendMode::kOpaque, BlendMode::kMask, BlendMode::kMaskA2C, BlendMode::kDithered})
        {
            const BlendModeState s = blendModeState(m);
            CHECK_FALSE(s.blend.enabled);
            CHECK(s.depthWrite);
            CHECK_FALSE(s.needsSorting);
            CHECK_FALSE(isTransparent(m));
        }
        CHECK(blendModeState(BlendMode::kMask).discardInShader);
        CHECK(blendModeState(BlendMode::kDithered).discardInShader);
        CHECK_FALSE(blendModeState(BlendMode::kOpaque).discardInShader);
        CHECK(blendModeState(BlendMode::kMaskA2C).alphaToCoverage);
        CHECK_FALSE(blendModeState(BlendMode::kMask).alphaToCoverage);
    }

    TEST_CASE("transparent modes blend with the documented factors and never write depth")
    {
        const auto blend = blendModeState(BlendMode::kBlend);
        CHECK(blend.blend.enabled);
        CHECK((blend.blend.srcColor == BlendFactor::kSrcAlpha));
        CHECK((blend.blend.dstColor == BlendFactor::kOneMinusSrcAlpha));
        CHECK(blend.needsSorting);

        const auto add = blendModeState(BlendMode::kAdditive);
        CHECK((add.blend.srcColor == BlendFactor::kSrcAlpha));
        CHECK((add.blend.dstColor == BlendFactor::kOne));
        CHECK_FALSE(add.needsSorting); // l'addition commute

        const auto pre = blendModeState(BlendMode::kPremultiplied);
        CHECK((pre.blend.srcColor == BlendFactor::kOne));
        CHECK((pre.blend.dstColor == BlendFactor::kOneMinusSrcAlpha));

        const auto tr = blendModeState(BlendMode::kTransmission);
        CHECK(tr.blend.enabled);
        CHECK(tr.needsSorting);
        CHECK(tr.readsSceneColor);
        CHECK_FALSE(blend.readsSceneColor);

        for (const BlendMode m : {BlendMode::kBlend, BlendMode::kAdditive, BlendMode::kPremultiplied, BlendMode::kTransmission})
        {
            CHECK_FALSE(blendModeState(m).depthWrite);
            CHECK(isTransparent(m));
        }
    }

    TEST_CASE("names round-trip and are unique; unknown names are errors")
    {
        for (const BlendMode m : kAllBlendModes)
        {
            auto back = blendModeFromName(blendModeName(m));
            REQUIRE(back.has_value());
            CHECK((*back == m));
        }
        CHECK(std::size(kAllBlendModes) == 8);
        CHECK((tests::errorOf(blendModeFromName("glass")) == orosmo::core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("glTF alpha modes map to BlendMode")
    {
        CHECK((*blendModeFromGltf("OPAQUE") == BlendMode::kOpaque));
        CHECK((*blendModeFromGltf("MASK") == BlendMode::kMask));
        CHECK((*blendModeFromGltf("BLEND") == BlendMode::kBlend));
        CHECK_FALSE(blendModeFromGltf("blend").has_value());
    }

    TEST_CASE("applyBlendMode sets every color target and the depth write, keeping the depth test")
    {
        orosmo::rhi::GraphicsPipelineDesc desc;
        desc.colorTargets.resize(2);
        desc.depth = {.test = true, .write = true};
        applyBlendMode(desc, BlendMode::kAdditive);
        for (const auto &t : desc.colorTargets)
        {
            CHECK(t.blend.enabled);
            CHECK((t.blend.dstColor == BlendFactor::kOne));
        }
        CHECK(desc.depth.test);
        CHECK_FALSE(desc.depth.write);
        applyBlendMode(desc, BlendMode::kOpaque);
        CHECK_FALSE(desc.colorTargets[0].blend.enabled);
        CHECK(desc.depth.write);
        // sans test de profondeur, pas d'écriture non plus
        desc.depth = {.test = false, .write = false};
        applyBlendMode(desc, BlendMode::kOpaque);
        CHECK_FALSE(desc.depth.write);
    }
}
