// Lumières et leur tampon de stockage (SSBO std430) lu par `shaders/rhi/mesh.frag`. Mêmes types et même disposition mémoire que
// l'ancien `LightManager` (REN-05) ; la disposition est vérifiée par des `static_assert`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orosmo::renderer
{

enum class LightType : std::int32_t
{
    kNone = 0,
    kAmbient = 1,
    kDirectional = 2,
    kPoint = 3, // réservés : les shaders actuels ne les rendent pas (comme avant)
    kSpot = 4,
};

struct Light
{
    LightType type = LightType::kNone;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f};
    glm::vec3 color{0.0f};
    float strength = 0.0f;
    float outerCone = 0.0f;
    float innerCone = 0.0f;
    float a = 0.0f;
    float b = 0.0f;

    bool operator==(const Light &other) const;
};

Light directionalLight(const glm::vec3 &direction, const glm::vec3 &color, float strength);
Light pointLight(const glm::vec3 &position, const glm::vec3 &color, float strength, float a, float b);
Light spotLight(const glm::vec3 &position, const glm::vec3 &direction, const glm::vec3 &color, float strength, float outerCone, float innerCone,
                float a, float b);
Light ambientLight(const glm::vec3 &color, float strength);

// Disposition GPU (std430) : écrite champ par champ, sans `glm::vec3` (qui n'a pas la même taille qu'un vec3 GLSL).
struct GpuLight
{
    std::int32_t type;
    float pad0[3];
    float position[3];
    float pad1;
    float direction[3];
    float pad2;
    float color[3];
    float strength;
    float outerCone;
    float innerCone;
    float a;
    float b;
    float pad3[4];
};
static_assert(sizeof(GpuLight) == 96, "LightStruct std430 : 96 octets");
static_assert(offsetof(GpuLight, position) == 16 && offsetof(GpuLight, direction) == 32 && offsetof(GpuLight, color) == 48,
              "les vec3 de LightStruct sont alignés sur 16 octets");
static_assert(offsetof(GpuLight, strength) == 60 && offsetof(GpuLight, outerCone) == 64 && offsetof(GpuLight, b) == 76,
              "strength suit color à l'offset 60 (std430), puis les quatre flottants des cônes");

struct GpuLightHeader
{
    std::int32_t size;
    std::int32_t pad0[3];
    float ambientColor[4];
    float ambientStrength;
    float pad1[3];
};
static_assert(sizeof(GpuLightHeader) == 48, "le tableau de lumières commence à l'offset 48");

class LightManager
{
  public:
    LightManager() = default;
    ~LightManager() = default;
    LightManager(const LightManager &) = delete;
    LightManager &operator=(const LightManager &) = delete;
    LightManager(LightManager &&) noexcept = default;
    LightManager &operator=(LightManager &&) noexcept = default;

    static core::Result<LightManager> create(rhi::Device &device, std::uint32_t initialCapacity = 16);

    void setAmbientLight(const glm::vec3 &color, float strength);
    void resetAmbientLight();
    void addLight(const Light &light);
    void addLights(const std::vector<Light> &lights);
    // Seule façon de modifier une lumière existante (marque le tampon à renvoyer) ; kOutOfRange pour un index périmé.
    core::Result<> setLight(std::size_t index, const Light &light);
    void removeLight(std::size_t index);
    void removeLight(const Light &light); // premier élément égal
    // Lecture seule : une modification par référence contournerait `dirty_` (le GPU garderait l'ancienne valeur). Nul si hors limites.
    const Light *light(std::size_t index) const noexcept { return index < lights_.size() ? &lights_[index] : nullptr; }
    const std::vector<Light> &lights() const noexcept { return lights_; }
    const Light &ambient() const noexcept { return ambient_; }

    // Envoie les changements au GPU (agrandit le tampon au besoin ; le handle change alors : relire `buffer()`).
    core::Result<> upload();

    const GpuBuffer &buffer() const noexcept { return buffer_; }
    bool dirty() const noexcept { return dirty_; }

  private:
    std::vector<Light> lights_;
    Light ambient_ = ambientLight(glm::vec3(1.0f), 0.0f);
    GpuBuffer buffer_;
    std::uint32_t capacity_ = 0;
    bool dirty_ = true;
};

} // namespace orosmo::renderer
