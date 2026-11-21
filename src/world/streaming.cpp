#include "orosmo/world/streaming.hpp"

#include <algorithm>
#include <unordered_set>

namespace orosmo::world
{

TileStreamer::TileStreamer(TerrainParams params, std::uint32_t seed, LodConfig lod, StreamingConfig config, core::TaskSystem &tasks)
    : params_(params), seed_(seed), lod_(lod), config_(config), tasks_(tasks)
{
}

TileStreamer::~TileStreamer()
{
    for (const Job &job : jobs_)
    {
        tasks_.wait(job.task);
    }
}

void TileStreamer::collectFinished()
{
    std::erase_if(jobs_,
                  [&](Job &job)
                  {
                      if (!job.task.done())
                      {
                          return false;
                      }
                      if (!*job.complete)
                      {
                          // La tâche s'est arrêtée sur une exception (journalisée par TaskSystem) : tuile incomplète, non publiée.
                          // Elle sera redemandée par update() après kRetryDelayFrames images.
                          ++failed_;
                          retryAt_.insert_or_assign(job.key, frame_ + kRetryDelayFrames);
                          return true;
                      }
                      retryAt_.erase(job.key);
                      job.result->lastUsedFrame = frame_;
                      // La sélection a pu changer pendant le travail : la couture calculée au lancement ne vaut plus forcément.
                      if (std::ranges::find(selection_, job.key) != selection_.end())
                      {
                          const CoarserNeighbors stitch = coarserNeighbors(selection_, job.key);
                          if (stitch != job.result->stitch)
                          {
                              job.result->mesh = buildTileMesh(params_, seed_, job.result->heights, stitch);
                              job.result->stitch = stitch;
                          }
                      }
                      memory_ += job.result->memoryBytes();
                      ++generated_;
                      resident_.insert_or_assign(job.key, std::move(*job.result));
                      return true;
                  });
}

void TileStreamer::update(float cameraX, float cameraZ)
{
    ++frame_;
    collectFinished();
    selection_ = selectTiles(params_, lod_, cameraX, cameraZ);
    for (const TileKey &key : selection_)
    {
        const CoarserNeighbors stitch = coarserNeighbors(selection_, key);
        if (auto it = resident_.find(key); it != resident_.end())
        {
            it->second.lastUsedFrame = frame_;
            if (it->second.stitch != stitch)
            {
                // Les voisines ont changé de niveau : seul le maillage est refait (les hauteurs ne dépendent que de la tuile).
                memory_ -= it->second.memoryBytes();
                it->second.mesh = buildTileMesh(params_, seed_, it->second.heights, stitch);
                it->second.stitch = stitch;
                memory_ += it->second.memoryBytes();
            }
            continue;
        }
        const bool pending = std::ranges::any_of(jobs_, [&](const Job &j) { return j.key == key; });
        if (pending || jobs_.size() >= config_.maxJobsInFlight)
        {
            continue;
        }
        if (auto retry = retryAt_.find(key); retry != retryAt_.end() && frame_ < retry->second)
        {
            continue; // échec récent : pas de nouvelle tentative à chaque image
        }
        Job job;
        job.key = key;
        job.stitch = stitch;
        job.result = std::make_shared<ResidentTile>();
        job.complete = std::make_shared<bool>(false);
        // La tâche ne lit que des copies (paramètres, graine, clé) : aucun état partagé avec le streamer. `complete` n'est vrai
        // que si elle va au bout (lu après done(), comme le résultat).
        job.task = tasks_.submit(
            [params = params_, seed = seed_, key, stitch, out = job.result, complete = job.complete]()
            {
                out->heights = generateTile(params, seed, key);
                out->mesh = buildTileMesh(params, seed, out->heights, stitch);
                out->stitch = stitch;
                *complete = true;
            });
        jobs_.push_back(std::move(job));
    }
    rebalance();
}

void TileStreamer::rebalance()
{
    std::size_t needed = 0;
    for (const TileKey &key : selection_)
    {
        if (auto it = resident_.find(key); it != resident_.end())
        {
            needed += it->second.memoryBytes();
        }
    }
    overBudget_ = needed > config_.memoryBudgetBytes;
    evict();
}

void TileStreamer::evict()
{
    if (memory_ <= config_.memoryBudgetBytes)
    {
        return;
    }
    const std::unordered_set<TileKey, TileKeyHash> wanted(selection_.begin(), selection_.end());
    std::vector<std::pair<std::uint64_t, TileKey>> candidates;
    for (const auto &[key, tile] : resident_)
    {
        if (!wanted.contains(key))
        {
            candidates.emplace_back(tile.lastUsedFrame, key);
        }
    }
    // Les plus anciennes d'abord ; à âge égal, ordre des clés (déterministe).
    std::ranges::sort(candidates, [](const auto &a, const auto &b)
                      {
                          if (a.first != b.first) return a.first < b.first;
                          if (a.second.lod != b.second.lod) return a.second.lod < b.second.lod;
                          return a.second.z != b.second.z ? a.second.z < b.second.z : a.second.x < b.second.x;
                      });
    for (const auto &[age, key] : candidates)
    {
        if (memory_ <= config_.memoryBudgetBytes)
        {
            break;
        }
        auto it = resident_.find(key);
        memory_ -= it->second.memoryBytes();
        resident_.erase(it);
        ++evictions_;
    }
}

const ResidentTile *TileStreamer::find(const TileKey &key) const
{
    auto it = resident_.find(key);
    return it == resident_.end() ? nullptr : &it->second;
}

StreamingStats TileStreamer::stats() const
{
    return {resident_.size(), jobs_.size(), memory_, evictions_, generated_, failed_, overBudget_};
}

void TileStreamer::finishPending()
{
    for (const Job &job : jobs_)
    {
        tasks_.wait(job.task);
    }
    collectFinished();
    rebalance(); // les tuiles intégrées comptent : budget et éviction recalculés sans attendre le prochain update()
}

} // namespace orosmo::world
