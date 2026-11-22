// RHI-04, RHI-18 : capacités du périphérique. Le moteur les lit ; il ne suppose jamais une fonction optionnelle (règle 11).
#pragma once

#include "orosmo/rhi/types.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace orosmo::rhi
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
    // Emplacements de tampons de stockage (OpenGL 4.3 : au moins 8). Un groupe/binding dont l'emplacement plat
    // (groupe * 8 + binding) dépasse cette limite est refusé par `setBindGroup`.
    std::uint32_t maxStorageBufferBindings = 0;

    // Fonctions courantes
    bool compute = false;
    bool indirectDraw = false;
    bool multiDrawIndirect = false;
    // `firstInstance` non nul dans les commandes indirectes (Vulkan : drawIndirectFirstInstance ; OpenGL 4.3 : toujours). Sans elle,
    // une commande indirecte doit avoir firstInstance = 0.
    bool drawIndirectFirstInstance = false;
    bool timestampQueries = false;
    bool anisotropy = false;
    bool bc7Textures = false;
    bool wireframe = false;
    bool spirvShaders = false; // ShaderLanguage::kSpirv accepté
    bool shaderFloat64 = false; // `double` dans les shaders (OpenGL 4.0+, Vulkan shaderFloat64)

    // Capacités optionnelles réservées (RHI-18) : `false` tant que non implémentées.
    bool rayQuery = false;
    bool accelerationStructure = false;
    bool bindless = false;
    bool meshShader = false;
    bool bufferDeviceAddress = false;
    bool sparseResources = false;
};

} // namespace orosmo::rhi
