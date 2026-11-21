// Tests du périphérique OpenGL qui ne sont PAS un contrat du RHI : ils tournent donc seulement sur OpenGL.
// Le contrat commun (tampons, textures, passes, compute, copies, minuteries...) est dans rhi_device_tests.cpp, exécuté sur
// chaque backend disponible. Sans pilote OpenGL 4.3 (machine sans affichage), chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"
#include "rhi/gpu_fixture.hpp"

#include <orosmo/rhi/device.hpp>

#include <cstdint>
#include <string>

using namespace orosmo::rhi;
using orosmo::core::ErrorCode;

TEST_SUITE("rhi/opengl/device")
{
    // Vulkan accepte du SPIR-V et son compilateur a ses propres messages : propre à OpenGL.
    TEST_CASE("shaders: SPIR-V is refused, compile errors carry the driver log")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        ShaderDesc spirv;
        spirv.language = ShaderLanguage::kSpirv;
        const std::byte word{0x03};
        spirv.code = std::span(&word, 1);
        CHECK((tests::errorOf(gpu->createShader(spirv)) == ErrorCode::kUnsupported));

        ShaderDesc broken;
        broken.stage = ShaderStage::kVertex;
        broken.code = bytes("#version 430 core\nvoid main() { this is not glsl }");
        broken.debugName = "broken";
        auto r = gpu->createShader(broken);
        REQUIRE_FALSE(r.has_value());
        CHECK((tests::errorOf(r) == ErrorCode::kParse));
        CHECK(r.error().message.find("broken") != std::string::npos);
        // Pas de contrôle de la validation ici : Mesa signale lui-même l'erreur de compilation comme message d'erreur GL_KHR_debug.
        CHECK(gpu->validationErrorCount() <= 2);
    }

    // Le défaut visé est la liaison de framebuffer de GL (une lecture au milieu d'une passe changeait la cible du dessin suivant).
    TEST_CASE("a synchronous read during a pass does not redirect the next draw (revue CodeRabbit)")
    {
        Gpu gpu;
        SKIP_TEST_IF_NULL(gpu.device.get(), "OpenGL 4.3 context");
        const TextureHandle color = gpu.target(Format::kRGBA8Unorm, {4, 4});
        const TextureHandle other = gpu.target(Format::kRGBA8Unorm, {4, 4}, "other");
        const PipelineHandle pipe = gpu.pipeline(gpu.shader(ShaderStage::kVertex, kFullscreenVsGlsl450), gpu.shader(ShaderStage::kFragment, kRedFsGlsl450));
        gpu.run(
            [&](CommandEncoder &e)
            {
                e.beginRenderPass(passOn(color, {0, 1, 0, 1}));
                CHECK(gpu->readTexture(other).has_value()); // lecture synchrone au milieu de la passe
                e.setPipeline(pipe);
                e.draw(3);
                e.endRenderPass();
            });
        CHECK(rgba(gpu.pixel(color, 1, 1)) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        gpu.expectNoValidationErrors();
    }
}
