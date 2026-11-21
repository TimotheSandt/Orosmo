// Implémentation de VMA (une seule unité de compilation). Les fonctions Vulkan viennent de volk, chargées à l'exécution :
// aucune liaison avec le chargeur Vulkan (README.md, « Chargement »).
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1 // les autres fonctions sont lues avec vkGetInstanceProcAddr / vkGetDeviceProcAddr de volk
#define VMA_IMPLEMENTATION

#include <volk.h>

#include <vk_mem_alloc.h>
