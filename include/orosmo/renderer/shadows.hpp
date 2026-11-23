// REN-06 à REN-08 : partie des ombres SANS dépendance graphique (testable sans GPU). Mobilité des lumières et mode d'ombre (avec le
// comportement résolu de chaque combinaison), découpage des cascades, ajustement stable d'une cascade (sphère englobante de taille
// constante, ancrée sur la grille de texels : pas de scintillement quand la caméra bouge ou tourne), matrices des projecteurs,
// allocateur de l'atlas d'ombres (tuiles carrées en puissances de deux, « buddy »), hachage des listes de casters (cache).
// Le plugin qui dessine les ombres est `shadow_feature.hpp`.
#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace orosmo::renderer
{

// docs/03 §11 : chaque lumière porte sa mobilité et son mode d'ombre (`Light::mobility`, `Light::shadowMode`).
enum class LightMobility : std::uint8_t
{
    kStatic,     // ne bouge pas et ne change pas (ni position, ni direction, ni couleur)
    kStationary, // ne bouge pas ; couleur et intensité peuvent changer
    kDynamic,    // tout peut changer
};

enum class ShadowMode : std::uint8_t
{
    kNone,    // pas d'ombre
    kBaked,   // lightmaps ou sondes cuites (REN-16, absent) : voir resolveShadowBehavior
    kDynamic, // toutes les casters redessinées à chaque mise à jour de la vue
    kCached,  // géométrie fixe gardée en cache ; seules les casters mobiles sont redessinées par-dessus
};

enum class CasterMobility : std::uint8_t
{
    kStatic,  // géométrie fixe : gardée dans le cache des lumières en mode Cached (la déplacer invalide ce cache)
    kMovable, // redessinée par-dessus le cache quand elle bouge
};

enum class ShadowStatus : std::uint8_t
{
    kNone,               // pas d'ombre demandée (mode None, lumière ambiante)
    kRealtime,           // ombre temps réel (Dynamic ou Cached)
    kBakedFallback,      // Baked sur une lumière Static ou Stationary : pas de cuisson (REN-16), repli temps réel en Cached
    kInvalidCombination, // Baked sur une lumière Dynamic (une lumière mobile ne se cuit pas) : repli Dynamic, avertissement
    kUnsupportedType,    // lumière ponctuelle (cubemaps : REN-09) ou directionnelle au-delà de la première ombrée
    kFaded,              // projecteur au-delà de sa distance de fondu : aucune vue allouée
    kAtlasFull,          // aucune place dans l'atlas, même à la résolution minimale
};

// Comportement résolu d'une combinaison (mobilité, mode). Tableau complet dans src/renderer/README.md (« Ombres »).
struct ShadowBehavior
{
    bool realtime = false;    // des vues d'ombre sont dessinées
    bool cacheStatic = false; // la géométrie fixe est gardée en cache (ne se redessine que si elle ou la vue change)
    bool mayMove = true;      // position, direction, cône peuvent changer (sinon : violation comptée, cache invalidé)
    bool mayRecolor = true;   // couleur, intensité peuvent changer (Static : non ; sans effet sur l'ombre, seulement compté)
    ShadowStatus status = ShadowStatus::kNone;
};
ShadowBehavior resolveShadowBehavior(LightMobility mobility, ShadowMode mode) noexcept;

// ── Cascades (REN-07) ──────────────────────────────────────────────────────────────────────────────────────────────
// Distances de fin des `count` cascades entre `nearPlane` et `farPlane` : schéma « pratique » (mélange `lambda` du découpage
// logarithmique et du découpage uniforme ; 0 : uniforme, 1 : logarithmique). Croissantes, la dernière vaut `farPlane`.
std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count, float lambda);

// Forme de la caméra dans le monde, sans jitter : de quoi calculer les tranches de sa pyramide.
struct CameraShape
{
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    bool orthographic = false;
    float halfX = 1.0f; // perspective : tan(fov x / 2) ; orthographique : demi-largeur (m)
    float halfY = 1.0f; // perspective : tan(fov y / 2) ; orthographique : demi-hauteur (m)
    float nearPlane = 0.1f;
};
// Depuis les matrices d'une image (FrameData : view, proj reverse-Z, jitter ignoré) et l'origine du repère des shaders (monde =
// repère des shaders + origin ; 0 hors rendu relatif à la caméra).
CameraShape cameraShapeFrom(const glm::mat4 &view, const glm::mat4 &projection, const glm::vec3 &origin) noexcept;
// Les 8 coins de la tranche [nearDistance, farDistance] (distances le long de `forward`) : proches puis lointains.
std::array<glm::vec3, 8> sliceCorners(const CameraShape &camera, float nearDistance, float farDistance) noexcept;

// Vue d'ombre : matrices monde -> clip (profondeur classique [0, 1] : 0 près de la lumière), taille d'un texel dans le monde.
struct ShadowViewFit
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::vec3 center{0.0f}; // centre de la sphère englobante (monde)
    float radius = 0.0f;
    float texelWorld = 0.0f; // côté d'un texel (m)
    float depthRange = 1.0f; // distance couverte par la profondeur [0, 1] (m)
    float nearPlane = 0.0f;  // projecteur : plans proche et lointain réellement utilisés par la projection (après bornage)
    float farPlane = 0.0f;
    bool valid = true;       // faux : la vue ne peut pas couvrir la zone demandée (projecteur plus large que ce que la perspective représente)
};
// Cascade de la tranche [nearDistance, farDistance] pour une lumière directionnelle (`toLight` : vers la lumière, comme
// `Light::direction`). Stabilité : le rayon ne dépend que de la tranche et du champ (pas de l'orientation), le centre et la
// profondeur sont ancrés sur la grille de texels dans le repère de la lumière : un déplacement de la caméra décale la projection
// d'un nombre ENTIER de texels (les bords des ombres ne scintillent pas). `depthExtension` : distance ajoutée vers la lumière
// (casters hors de la sphère qui projettent une ombre dedans).
ShadowViewFit fitCascade(const CameraShape &camera, float nearDistance, float farDistance, const glm::vec3 &toLight, std::uint32_t resolution,
                         float depthExtension) noexcept;
// Projecteur : perspective depuis `position` le long de `direction`, demi-angle `halfAngle` (radians), profondeur classique [0, 1].
// `texelWorld` est la taille d'un texel à 1 m (à multiplier par la distance). Un cône de demi-angle au-delà de `kMaxSpotShadowHalfAngle`
// ne se projette pas sans être tronqué : la vue est alors refusée (`valid == false`) au lieu de réduire le cône.
inline constexpr float kMaxSpotShadowHalfAngle = 1.4835299f; // 85 degrés
ShadowViewFit fitSpot(const glm::vec3 &position, const glm::vec3 &direction, float halfAngle, float nearPlane, float farPlane,
                      std::uint32_t resolution) noexcept;

// ── Atlas (REN-08) ─────────────────────────────────────────────────────────────────────────────────────────────────
struct AtlasTile
{
    std::uint32_t x = 0; // texels, origine en haut à gauche (repère du RHI)
    std::uint32_t y = 0;
    std::uint32_t size = 0;
    bool operator==(const AtlasTile &) const = default;
};

// Matrice qui envoie le clip d'une vue (NDC x, y dans [-1, 1], y = +1 en haut, A26) vers les coordonnées de l'atlas (u, v dans
// [0, 1], v = 0 en haut) de la tuile ; z inchangé.
glm::mat4 atlasTileMatrix(const AtlasTile &tile, std::uint32_t atlasSize) noexcept;

// Allocateur « buddy » : tuiles carrées de côté puissance de deux, de `minTile` à `atlasSize`. Déterministe (la tuile libre la plus
// haute puis la plus à gauche) ; une tuile libérée fusionne avec sa voisine libre.
class ShadowAtlasAllocator
{
  public:
    explicit ShadowAtlasAllocator(std::uint32_t atlasSize = 4096, std::uint32_t minTile = 64);
    void reset(std::uint32_t atlasSize, std::uint32_t minTile);
    // `size` arrondi à la puissance de deux supérieure (au moins minTile) ; nullopt si plus grand que l'atlas ou sans place.
    std::optional<AtlasTile> allocate(std::uint32_t size);
    void release(const AtlasTile &tile);
    std::uint32_t atlasSize() const noexcept { return atlasSize_; }
    std::uint32_t minTile() const noexcept { return minTile_; }
    std::uint64_t freeArea() const noexcept; // texels libres

  private:
    std::uint32_t levelOf(std::uint32_t size) const noexcept;
    std::uint32_t atlasSize_ = 0;
    std::uint32_t minTile_ = 0;
    std::vector<std::vector<AtlasTile>> free_; // par niveau (0 : l'atlas entier), triés (y, x)
};

// Puissance de deux supérieure ou égale ; saturée à 2^31 (le plus grand arrondi représentable) au lieu de déborder.
std::uint32_t nextPowerOfTwo(std::uint32_t value) noexcept;

// Ordre de service des vues à mettre à jour sous budget : jamais dessinée d'abord ; puis une vue en retard (âgée d'au moins
// `deadlineFrames` images) passe devant, la plus ancienne d'abord ; sinon priorité de la lumière, puis la plus ancienne. L'ancienneté
// ne supplante donc la priorité qu'après l'échéance : une vue dynamique de basse priorité ne reste pas indéfiniment sans mise à jour.
struct ShadowServiceKey
{
    bool rendered = false;
    int priority = 0;
    std::uint64_t age = 0; // images depuis la dernière mise à jour
};
inline constexpr std::uint64_t kShadowServiceDeadlineFrames = 32;
bool shadowServiceBefore(const ShadowServiceKey &a, const ShadowServiceKey &b, std::uint64_t deadlineFrames = kShadowServiceDeadlineFrames) noexcept;

// Hachage FNV-1a 64 bits (listes de casters et matrices : le cache compare les hachages d'une image à l'autre).
class ShadowHash
{
  public:
    void bytes(std::span<const std::byte> data) noexcept;
    template <typename T> void value(const T &v) noexcept { bytes(std::as_bytes(std::span<const T>(&v, 1))); }
    std::uint64_t digest() const noexcept { return state_; }

  private:
    std::uint64_t state_ = 14695981039346656037ull;
};

} // namespace orosmo::renderer
