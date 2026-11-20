// TER-04 : streaming des tuiles. `update(caméra)` sélectionne les feuilles (TER-03), lance la génération des tuiles manquantes en
// tâches de fond (CORE-04) et libère les tuiles inutilisées les plus anciennes quand la mémoire dépasse le budget. Une tuile
// demandée par la sélection courante n'est jamais libérée (le budget peut alors être dépassé : `stats().overBudget`).
#pragma once

#include "engine/core/tasks.hpp"
#include "engine/world/lod.hpp"
#include "engine/world/terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine::world
{

struct StreamingConfig
{
    std::size_t memoryBudgetBytes = 64u * 1024u * 1024u; // hauteurs + maillages résidents
    std::uint32_t maxJobsInFlight = 8;                   // générations simultanées au plus
};

struct StreamingStats
{
    std::size_t residentTiles = 0;
    std::size_t pendingTiles = 0;   // générations en cours
    std::size_t memoryBytes = 0;    // mémoire des tuiles résidentes
    std::size_t evictions = 0;      // depuis la création
    std::size_t generated = 0;      // depuis la création
    bool overBudget = false;        // la sélection courante seule dépasse le budget
};

// Tuile prête : hauteurs et maillage cousu selon ses voisines au moment de sa construction.
struct ResidentTile
{
    Heightmap heights;
    TileMesh mesh;
    CoarserNeighbors stitch{};
    std::uint64_t lastUsedFrame = 0;
    std::size_t memoryBytes() const noexcept { return heights.memoryBytes() + mesh.vertices.size() * sizeof(float) + mesh.indices.size() * sizeof(std::uint32_t); }
};

class TileStreamer
{
  public:
    TileStreamer(TerrainParams params, std::uint32_t seed, LodConfig lod, StreamingConfig config, core::TaskSystem &tasks);
    ~TileStreamer(); // attend les générations en cours
    TileStreamer(const TileStreamer &) = delete;
    TileStreamer &operator=(const TileStreamer &) = delete;

    // Une fois par image : sélection, demandes, intégration des tuiles terminées, éviction.
    void update(float cameraX, float cameraZ);
    // Feuilles de la dernière sélection, et celles qui sont prêtes à dessiner (les autres sont encore en génération).
    const std::vector<TileKey> &selection() const noexcept { return selection_; }
    const ResidentTile *find(const TileKey &key) const;
    StreamingStats stats() const;
    // Attend toutes les générations lancées et les intègre (outils, tests).
    void finishPending();

  private:
    struct Job
    {
        TileKey key;
        CoarserNeighbors stitch{};
        core::TaskHandle task;
        std::shared_ptr<ResidentTile> result;
    };
    void collectFinished();
    void rebalance(); // recalcule overBudget_ sur les tuiles résidentes de la sélection, puis évince
    void evict();

    TerrainParams params_;
    std::uint32_t seed_;
    LodConfig lod_;
    StreamingConfig config_;
    core::TaskSystem &tasks_;
    std::vector<TileKey> selection_;
    std::unordered_map<TileKey, ResidentTile, TileKeyHash> resident_;
    std::vector<Job> jobs_;
    std::uint64_t frame_ = 0;
    std::size_t memory_ = 0;
    std::size_t evictions_ = 0;
    std::size_t generated_ = 0;
    bool overBudget_ = false;
};

} // namespace engine::world
