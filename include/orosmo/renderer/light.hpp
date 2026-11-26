// Lumières et leur tampon de stockage (SSBO std430) lu par `shaders/rhi/mesh.frag`. Mêmes types et même disposition mémoire que
// l'ancien `LightManager` (REN-05) ; la disposition est vérifiée par des `static_assert`.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/shadows.hpp"
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
    kPoint = 3, // atténuation 1 / (a d² + b d + 1), coupée à la portée `lightRange` (forward_plus.hpp, REN-05)
    kSpot = 4,  // comme la ponctuelle, dans le cône (outerCone, innerCone : cosinus) autour de `direction` (sens du faisceau)
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
    // REN-06 : mobilité et mode d'ombre (lus par ShadowFeature, sans effet sur le tampon GPU ni sur l'égalité ci-dessous).
    LightMobility mobility = LightMobility::kDynamic;
    ShadowMode shadowMode = ShadowMode::kNone;

    bool operator==(const Light &other) const;
};

// Fabriques d'origine (gardées, A79 choix 10). Attention : `direction` est le sens VERS la lumière pour une directionnelle et le
// sens du faisceau pour un projecteur (c'est ce que lit `mesh.frag`) ; `a`, `b` : atténuation 1 / (a d² + b d + 1) ; cônes en
// cosinus. Préférer les fabriques à descripteur ci-dessous.
Light directionalLight(const glm::vec3 &direction, const glm::vec3 &color, float strength);
Light pointLight(const glm::vec3 &position, const glm::vec3 &color, float strength, float a, float b);
Light spotLight(const glm::vec3 &position, const glm::vec3 &direction, const glm::vec3 &color, float strength, float outerCone, float innerCone,
                float a, float b);
Light ambientLight(const glm::vec3 &color, float strength);

// A75.10 : une seule convention et des unités lisibles. `direction` est toujours le sens de PROPAGATION de la lumière (du soleil
// vers le sol, du projecteur vers ce qu'il éclaire) ; la portée (m) remplace les coefficients d'atténuation ; les angles sont en
// degrés. Elles rendent une `Light` ordinaire (même tampon GPU, même image que l'ancienne fabrique aux valeurs équivalentes).
// Usage : `directionalLight({.direction = {0, -1, 0.3f}, .intensity = 2})`, `pointLight({.position = p, .range = 8})`.
struct DirectionalLightDesc
{
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // sens de propagation (défaut : soleil au zénith)
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct PointLightDesc
{
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    // Distance (m) où la lumière s'éteint (`lightRange`) : atténuation en carré inverse, ramenée à 0 à la portée. Infinie : sans
    // atténuation. Bornée à 1 mm au moins. Elle dépend de la couleur et de l'intensité : changer l'une ensuite change la portée.
    float range = 10.0f;
};

struct SpotLightDesc
{
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; // sens de propagation (axe du faisceau)
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f; // comme PointLightDesc::range
    // Demi-angles depuis l'axe, en degrés, dans [0, 180] : pleine lumière jusqu'à `innerAngleDegrees`, rien au-delà de
    // `outerAngleDegrees`, fondu entre les deux. Un angle intérieur plus grand que l'extérieur est ramené à l'extérieur (bord net).
    float outerAngleDegrees = 30.0f;
    float innerAngleDegrees = 25.0f;
};

Light directionalLight(const DirectionalLightDesc &desc);
Light pointLight(const PointLightDesc &desc);
Light spotLight(const SpotLightDesc &desc);

// Sens de propagation de la lumière, quelle que soit la fabrique : -direction pour une directionnelle, direction pour un
// projecteur, (0, 0, 0) pour une ambiante ou une ponctuelle.
glm::vec3 lightDirection(const Light &light) noexcept;

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
static_assert(offsetof(GpuLightHeader, ambientColor) == 16 && offsetof(GpuLightHeader, ambientStrength) == 32,
              "LightBlock std430 : int size (0), vec4 AmbientLightColor (16), float AmbientLightStrength (32)");
static_assert(sizeof(GpuLightHeader) % 16 == 0 && sizeof(GpuLight) % 16 == 0,
              "std430 : le tableau de LightStruct (alignement 16, celui de ses vec3) commence et avance par multiples de 16");
static_assert(offsetof(GpuLight, type) == 0 && offsetof(GpuLight, innerCone) == 68 && offsetof(GpuLight, a) == 72,
              "LightStruct std430 : type (0), cônes (64, 68), atténuation a (72), b (76)");

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
