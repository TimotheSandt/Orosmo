// RHI-04, RHI-18 : capacités du périphérique. Le moteur les lit ; il ne suppose jamais une fonction optionnelle (règle 11).
#pragma once

#include "engine/rhi/types.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace engine::rhi
{

struct Capabilities
{
    Backend backend = Backend::kOpenGL;
    std::string deviceName;    // GL_RENDERER, nom du GPU Vulkan...
    std::string driverVersion; // GL_VERSION...

    // Limites
    std::uint32_t maxTextureSize = 0;
    std::uint32_t max3DTextureSize = 0;
    std::uint32_t maxColorAttachments = 0;
    std::array<std::uint32_t, 3> maxComputeWorkGroupSize{0, 0, 0};
    std::array<std::uint32_t, 3> maxComputeWorkGroupCount{0, 0, 0};
    std::uint32_t maxComputeInvocations = 0; // par groupe
    std::uint64_t minUniformAlignment = 256; // décalage dynamique minimal d'un tampon uniforme
    std::uint64_t minStorageAlignment = 16;
    std::uint64_t maxUniformBufferSize = 0;
    std::uint32_t maxPushConstantSize = 128; // émulé par un petit tampon sur OpenGL

    // Fonctions courantes
    bool compute = false;
    bool indirectDraw = false;
    bool multiDrawIndirect = false;
    bool timestampQueries = false;
    bool anisotropy = false;
    bool bc7Textures = false;
    bool wireframe = false;
    bool spirvShaders = false; // ShaderLanguage::kSpirv accepté

    // Capacités optionnelles réservées (RHI-18) : `false` tant que non implémentées.
    bool rayQuery = false;
    bool accelerationStructure = false;
    bool bindless = false;
    bool meshShader = false;
    bool bufferDeviceAddress = false;
    bool sparseResources = false;
};

} // namespace engine::rhi
