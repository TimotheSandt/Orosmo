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
    // Horodatages GPU qui mesurent le travail encadré. Faux aussi quand le pilote en accepte mais n'écrit pas d'heure GPU : MoltenVK
    // sans échantillonnage de compteurs Metal (GPU paravirtualisé) date tous les horodatages d'un tampon de commandes de sa fin, sur
    // l'horloge CPU ; aucune propriété Vulkan ne le dit, le backend le constate à la création (M9-L1).
    bool timestampQueries = false;
    bool anisotropy = false;
    bool bc7Textures = false;  // Format::kBC7* échantillonnables (OpenGL : BPTC, cœur 4.2 ; Vulkan : textureCompressionBC)
    bool astcTextures = false; // Format::kASTC4x4* (OpenGL : GL_KHR_texture_compression_astc_ldr ; Vulkan : textureCompressionASTC_LDR)
    bool wireframe = false;
    // Format::kRGBA8Srgb utilisable en image de stockage (TextureUsage::kStorage). OpenGL 4.3 : jamais (pas de format d'image sRGB) ;
    // Vulkan : VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT du pilote (MoltenVK sur GPU Apple : oui).
    bool srgbStorageImages = false;
    // Couche de validation réellement active (Vulkan : VK_LAYER_KHRONOS_validation chargée ; OpenGL : contexte de debug et
    // GL_KHR_debug branché). Faux si `DeviceDesc::validation` est faux ou si la couche manque (le périphérique est créé quand même).
    bool validation = false;
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
