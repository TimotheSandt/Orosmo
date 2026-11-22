// REN-29 : scène GPU persistante. Miroir sur le GPU d'un ObjectSet (object_set.hpp) : un enregistrement par EMPLACEMENT d'objet
// (ObjectId::index, stable), gardé d'une image à l'autre. `sync` ne relit que les emplacements modifiés depuis la synchronisation
// précédente (ObjectSet::slotChange) et n'envoie que ceux-là, par plages contiguës ; une croissance du tampon renvoie tout.
// Lu par le culling GPU (gpu_driven.hpp, shaders/rhi/gpu_cull.comp). La partie CPU (`collect`) est testable sans GPU.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/renderer/gpu_buffer.hpp"
#include "orosmo/renderer/instance_batch.hpp"
#include "orosmo/renderer/object_set.hpp"
#include "orosmo/rhi/device.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace orosmo::renderer
{

// Niveaux de détail d'un objet sur le GPU (un objet qui en a plus n'est pas envoyé : `GpuSceneSyncStats::skippedObjects`).
inline constexpr std::uint32_t kGpuSceneMaxLevels = 8;

// Enregistrement d'un objet (std430, même disposition que `SceneObject` de shaders/rhi/gpu_cull.comp).
struct GpuSceneObject
{
    glm::vec4 model[4];     // transformation (monde)
    glm::vec4 normal[3];    // matrice des normales (inverse transposée), w = 0
    glm::vec4 color;
    glm::vec4 boundsMin;    // boîte monde (w = 0)
    glm::vec4 boundsMax;
    std::uint32_t info[4];  // drapeaux (bit 0 : vivant), nombre de niveaux, réglages de LOD, matériau
    std::uint32_t lods[kGpuSceneMaxLevels]; // maillage de chaque niveau
};
static_assert(sizeof(GpuSceneObject) == 208, "GpuSceneObject : 13 vec4 (std430)");
inline constexpr std::uint32_t kGpuSceneAlive = 1u;

// Réglages de LOD (std430, `LodSettings` de gpu_cull.comp) : bascules (7 au plus : 8 niveaux), largeur du fondu, distance de rejet,
// biais, nombre de bascules (en flottant, exact).
struct GpuLodSettings
{
    float switches[8];
    float params[4];
};
static_assert(sizeof(GpuLodSettings) == 48, "GpuLodSettings : 3 vec4 (std430)");

// Plage d'emplacements à renvoyer : [first, first + count).
struct GpuSceneRange
{
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    bool operator==(const GpuSceneRange &) const = default;
};

struct GpuSceneSyncStats
{
    std::uint32_t changedObjects = 0; // emplacements relus (modifiés depuis la synchronisation précédente)
    std::uint32_t uploadedObjects = 0; // emplacements envoyés (plages fusionnées comprises)
    std::uint32_t uploadRanges = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint32_t skippedObjects = 0; // plus de kGpuSceneMaxLevels niveaux : jamais dessinés par le GPU
    bool fullUpload = false;           // tout renvoyé (premier envoi, autre ObjectSet, croissance du tampon, invalidate)
};

class GpuScene
{
  public:
    // Deux plages séparées par au plus `kMergeGap` emplacements inchangés sont envoyées ensemble (moins d'appels).
    static constexpr std::uint32_t kMergeGap = 8;

    // CPU seulement : met le miroir à jour depuis `objects` et calcule les plages modifiées depuis le `collect` précédent
    // (`pendingRanges`) ; elles s'ajoutent à celles qui n'ont pas encore été envoyées (`unsentRanges`). Sans GPU.
    GpuSceneSyncStats collect(const ObjectSet &objects);
    // `collect` puis envoi de toutes les plages non envoyées (tampons créés ou agrandis au besoin) ; elles ne sont oubliées qu'après
    // un envoi réussi : un `collect` appelé avant `sync` ne fait perdre aucune modification.
    core::Result<GpuSceneSyncStats> sync(rhi::Device &device, const ObjectSet &objects);
    // Tout sera renvoyé à la prochaine synchronisation.
    void invalidate() noexcept { fullPending_ = true; }
    void release() noexcept;

    std::span<const GpuSceneObject> records() const noexcept { return records_; }
    std::span<const GpuLodSettings> lodSettings() const noexcept { return lodSettings_; }
    std::span<const GpuSceneRange> pendingRanges() const noexcept { return pending_; }
    std::span<const GpuSceneRange> unsentRanges() const noexcept { return unsent_; }
    std::uint32_t slotCount() const noexcept { return static_cast<std::uint32_t>(records_.size()); }
    std::uint32_t liveObjects() const noexcept { return live_; }

    // Nombre d'instances qu'une clé (maillage, matériau) peut recevoir au plus dans une image : une par (objet vivant, niveau) qui
    // la désigne. Sert à découper le tampon d'instances du culling GPU. `keyVersion` change quand ces nombres changent.
    const std::map<InstanceKey, std::uint32_t> &keyReferences() const noexcept { return keyRefs_; }
    std::uint64_t keyVersion() const noexcept { return keyVersion_; }

    rhi::BufferHandle objectBuffer() const noexcept { return objectBuffer_.handle(); }
    rhi::BufferHandle lodBuffer() const noexcept { return lodBuffer_.handle(); }
    std::uint64_t objectBufferSize() const noexcept { return objectBuffer_.size(); }

  private:
    void writeRecord(std::uint32_t slot, const ObjectSet &objects, GpuSceneSyncStats &stats);
    void addKeys(const GpuSceneObject &record, int sign);

    const ObjectSet *source_ = nullptr;
    std::uint64_t syncedCounter_ = 0;
    std::size_t syncedLodSettings_ = 0;
    std::vector<GpuSceneObject> records_;
    std::vector<GpuLodSettings> lodSettings_;
    std::vector<GpuSceneRange> pending_; // modifiées au dernier `collect`
    std::vector<GpuSceneRange> unsent_;  // à envoyer au prochain `sync` (fusion des `collect` depuis le dernier envoi réussi)
    bool fullPending_ = true;
    bool lodPending_ = true;
    std::uint32_t live_ = 0;
    std::map<InstanceKey, std::uint32_t> keyRefs_;
    std::uint64_t keyVersion_ = 0;
    GpuBuffer objectBuffer_;
    GpuBuffer lodBuffer_;
};

} // namespace orosmo::renderer
