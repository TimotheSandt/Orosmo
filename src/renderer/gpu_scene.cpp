#include "orosmo/renderer/gpu_scene.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace orosmo::renderer
{

namespace
{
// Ajoute `ranges` (triées) à `into` : trié, chevauchements et trous d'au plus `gap` emplacements réunis.
void mergeRanges(std::vector<GpuSceneRange> &into, std::span<const GpuSceneRange> ranges, std::uint32_t gap)
{
    into.insert(into.end(), ranges.begin(), ranges.end());
    std::ranges::sort(into, {}, &GpuSceneRange::first);
    std::vector<GpuSceneRange> merged;
    for (const GpuSceneRange &range : into)
    {
        const std::uint32_t end = range.first + range.count;
        if (!merged.empty() && range.first <= merged.back().first + merged.back().count + gap)
        {
            merged.back().count = std::max(merged.back().first + merged.back().count, end) - merged.back().first;
        }
        else
        {
            merged.push_back(range);
        }
    }
    into = std::move(merged);
}

GpuLodSettings toGpu(const LodSettings &settings)
{
    GpuLodSettings out{};
    const std::size_t count = std::min<std::size_t>(settings.switchDistances.size(), kGpuSceneMaxLevels - 1);
    for (std::size_t i = 0; i < count; ++i)
    {
        out.switches[i] = settings.switchDistances[i];
    }
    out.params[0] = settings.transitionWidth;
    out.params[1] = settings.cullDistance;
    out.params[2] = settings.bias;
    out.params[3] = static_cast<float>(count);
    return out;
}
} // namespace

void GpuScene::addKeys(const GpuSceneObject &record, int sign)
{
    if ((record.info[0] & kGpuSceneAlive) == 0)
    {
        return;
    }
    for (std::uint32_t level = 0; level < record.info[1]; ++level)
    {
        const InstanceKey key{record.lods[level], record.info[3]};
        if (sign > 0)
        {
            ++keyRefs_[key];
        }
        else if (auto it = keyRefs_.find(key); it != keyRefs_.end() && --it->second == 0)
        {
            keyRefs_.erase(it);
        }
    }
}

void GpuScene::writeRecord(std::uint32_t slot, const ObjectSet &objects, GpuSceneSyncStats &stats)
{
    GpuSceneObject &record = records_[slot];
    const GpuSceneObject before = record;
    addKeys(record, -1);
    if ((record.info[0] & kGpuSceneAlive) != 0)
    {
        --live_;
    }
    record = GpuSceneObject{};
    if (const auto object = objects.objectAtSlot(slot))
    {
        if (object->lodMeshes.size() > kGpuSceneMaxLevels)
        {
            ++stats.skippedObjects;
        }
        else
        {
            const GpuInstance instance = makeGpuInstance(object->transform, object->color);
            std::copy(std::begin(instance.model), std::end(instance.model), std::begin(record.model));
            for (int i = 0; i < 3; ++i)
            {
                record.normal[i] = glm::vec4(glm::vec3(instance.normal[i]), 0.0f);
            }
            record.color = object->color;
            record.boundsMin = glm::vec4(object->worldBounds.min, 0.0f);
            record.boundsMax = glm::vec4(object->worldBounds.max, 0.0f);
            record.info[0] = kGpuSceneAlive;
            record.info[1] = static_cast<std::uint32_t>(object->lodMeshes.size());
            record.info[2] = object->lodSettings;
            record.info[3] = object->material;
            std::copy(object->lodMeshes.begin(), object->lodMeshes.end(), std::begin(record.lods));
            ++live_;
        }
    }
    addKeys(record, +1);
    // Les clés ne changent qu'avec la vie de l'objet ou ses maillages (le matériau et les LOD sont fixés à l'ajout).
    if (before.info[0] != record.info[0] || before.info[1] != record.info[1] || before.info[3] != record.info[3] ||
        !std::equal(std::begin(before.lods), std::end(before.lods), std::begin(record.lods)))
    {
        ++keyVersion_;
    }
}

GpuSceneSyncStats GpuScene::collect(const ObjectSet &objects)
{
    GpuSceneSyncStats stats;
    pending_.clear();
    const std::uint32_t slots = objects.slotCount();
    if (source_ != &objects)
    {
        // Autre ensemble : on repart de zéro.
        source_ = &objects;
        records_.clear();
        keyRefs_.clear();
        live_ = 0;
        ++keyVersion_;
        fullPending_ = true;
        lodPending_ = true;
        syncedLodSettings_ = 0;
    }
    if (objects.lodSettingsCount() != syncedLodSettings_)
    {
        lodSettings_.clear();
        for (std::size_t i = 0; i < objects.lodSettingsCount(); ++i)
        {
            lodSettings_.push_back(toGpu(objects.lodSettings(static_cast<std::uint32_t>(i))));
        }
        syncedLodSettings_ = objects.lodSettingsCount();
        lodPending_ = true;
    }
    if (records_.size() < slots)
    {
        records_.resize(slots, GpuSceneObject{}); // emplacements neufs : libres tant qu'ils ne sont pas relus
    }

    if (fullPending_)
    {
        for (std::uint32_t slot = 0; slot < slots; ++slot)
        {
            writeRecord(slot, objects, stats);
        }
        stats.changedObjects = slots;
        stats.fullUpload = true;
        fullPending_ = false; // sync le remet si l'envoi échoue
        unsent_.clear();
        if (slots > 0)
        {
            pending_.push_back({0, slots});
        }
    }
    else if (objects.changeCounter() != syncedCounter_)
    {
        for (std::uint32_t slot = 0; slot < slots; ++slot)
        {
            if (objects.slotChange(slot) <= syncedCounter_)
            {
                continue;
            }
            writeRecord(slot, objects, stats);
            ++stats.changedObjects;
            if (!pending_.empty() && slot - (pending_.back().first + pending_.back().count) <= kMergeGap)
            {
                pending_.back().count = slot + 1 - pending_.back().first;
            }
            else
            {
                pending_.push_back({slot, 1});
            }
        }
    }
    syncedCounter_ = objects.changeCounter();
    // Les plages de ce `collect` s'ajoutent à celles qui n'ont pas encore été envoyées (un `collect` sans `sync` n'en perd aucune).
    mergeRanges(unsent_, pending_, kMergeGap);
    for (const GpuSceneRange &range : pending_)
    {
        stats.uploadedObjects += range.count;
    }
    stats.uploadRanges = static_cast<std::uint32_t>(pending_.size());
    stats.uploadedBytes = std::uint64_t{stats.uploadedObjects} * sizeof(GpuSceneObject);
    return stats;
}

core::Result<GpuSceneSyncStats> GpuScene::sync(rhi::Device &device, const ObjectSet &objects)
{
    // Un tampon trop petit est recréé : il faut alors tout renvoyer (collect le sait avant de choisir les plages).
    const std::uint64_t needed = std::uint64_t{std::max<std::uint32_t>(objects.slotCount(), 1)} * sizeof(GpuSceneObject);
    const bool grow = !objectBuffer_.valid() || objectBuffer_.size() < needed;
    if (grow)
    {
        fullPending_ = true;
    }
    GpuSceneSyncStats stats = collect(objects);
    if (grow)
    {
        // Croissance par puissances de deux (au moins 64 objets).
        const std::uint64_t size = std::bit_ceil(std::max<std::uint64_t>(needed, 64 * sizeof(GpuSceneObject)));
        rhi::BufferDesc desc;
        desc.size = size;
        desc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst | rhi::BufferUsage::kCopySrc;
        desc.debugName = "gpuscene.objects";
        auto buffer = GpuBuffer::create(device, desc);
        if (!buffer)
        {
            fullPending_ = true;
            return std::unexpected(buffer.error());
        }
        objectBuffer_ = std::move(*buffer);
    }
    // Envoi de tout ce qui n'a pas encore été envoyé (plages de ce `collect` et des `collect` précédents sans `sync`).
    stats.uploadedObjects = 0;
    for (const GpuSceneRange &range : unsent_)
    {
        const auto bytes = std::as_bytes(std::span(records_).subspan(range.first, range.count));
        if (auto r = objectBuffer_.update(std::uint64_t{range.first} * sizeof(GpuSceneObject), bytes); !r)
        {
            fullPending_ = true; // l'état du tampon est inconnu : tout renvoyer la prochaine fois
            return std::unexpected(r.error());
        }
        stats.uploadedObjects += range.count;
    }
    stats.uploadRanges = static_cast<std::uint32_t>(unsent_.size());
    stats.uploadedBytes = std::uint64_t{stats.uploadedObjects} * sizeof(GpuSceneObject);
    fullPending_ = false;
    pending_.clear();
    unsent_.clear();

    if (lodPending_)
    {
        const std::uint64_t bytes = std::max<std::uint64_t>(lodSettings_.size(), 1) * sizeof(GpuLodSettings);
        if (!lodBuffer_.valid() || lodBuffer_.size() < bytes)
        {
            rhi::BufferDesc desc;
            desc.size = std::bit_ceil(std::max<std::uint64_t>(bytes, 16 * sizeof(GpuLodSettings)));
            desc.usage = rhi::BufferUsage::kStorage | rhi::BufferUsage::kCopyDst;
            desc.debugName = "gpuscene.lods";
            auto buffer = GpuBuffer::create(device, desc);
            if (!buffer)
            {
                return std::unexpected(buffer.error());
            }
            lodBuffer_ = std::move(*buffer);
        }
        if (!lodSettings_.empty())
        {
            if (auto r = lodBuffer_.update(0, std::as_bytes(std::span(lodSettings_))); !r)
            {
                return std::unexpected(r.error());
            }
        }
        lodPending_ = false;
    }
    return stats;
}

void GpuScene::release() noexcept
{
    objectBuffer_.reset();
    lodBuffer_.reset();
    source_ = nullptr;
    records_.clear();
    lodSettings_.clear();
    pending_.clear();
    unsent_.clear();
    keyRefs_.clear();
    live_ = 0;
    syncedCounter_ = 0;
    syncedLodSettings_ = 0;
    fullPending_ = true;
    lodPending_ = true;
    ++keyVersion_;
}

} // namespace orosmo::renderer
