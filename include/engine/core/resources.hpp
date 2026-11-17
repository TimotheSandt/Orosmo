// CORE-10 : gestionnaire de ressources. Handles avec génération (une poignée périmée ne retombe jamais sur une autre
// ressource), comptage de références, chargement asynchrone (TaskSystem), rechargement à chaud (même poignée, nouveau contenu).
//
// Un ResourceCache<T> gère un type de ressource. Le chargeur est une fonction `Result<T>(path)` fournie par l'appelant
// (le cache ne connaît ni le disque ni le GPU : couche core). get() renvoie un shared_ptr : un rechargement ou une
// libération pendant qu'un thread lit la ressource ne l'invalide pas.
#pragma once

#include <engine/core/error.hpp>
#include <engine/core/tasks.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::core
{

template <typename T> struct Handle
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 = poignée vide ; les générations valides commencent à 1
    bool valid() const { return generation != 0; }
    friend bool operator==(const Handle &, const Handle &) = default;
};

enum class ResourceState : std::uint8_t
{
    kInvalid, // poignée périmée ou vide
    kLoading,
    kReady,
    kFailed,
};

template <typename T> class ResourceCache
{
  public:
    using Loader = std::function<Result<T>(const std::string &path)>;

    // `tasks` peut être nul : loadAsync() charge alors de façon synchrone.
    explicit ResourceCache(Loader loader, TaskSystem *tasks = nullptr) : mLoader(std::move(loader)), mTasks(tasks) {}
    ~ResourceCache()
    {
        if (mTasks)
            mTasks->waitIdle(); // aucun chargement en vol ne doit survivre au cache
    }
    ResourceCache(const ResourceCache &) = delete;
    ResourceCache &operator=(const ResourceCache &) = delete;

    // Charge (ou retrouve) la ressource : même chemin = même poignée, compteur +1. Bloquant.
    Handle<T> load(const std::string &path)
    {
        auto [handle, needsLoad] = acquire(path);
        if (needsLoad)
            runLoad(handle, path);
        return handle;
    }

    // Comme load(), mais le chargement se fait sur le TaskSystem. state() passe de kLoading à kReady/kFailed.
    Handle<T> loadAsync(const std::string &path)
    {
        auto [handle, needsLoad] = acquire(path);
        if (needsLoad)
        {
            if (mTasks)
                mTasks->submit([this, handle, path] { runLoad(handle, path); });
            else
                runLoad(handle, path);
        }
        return handle;
    }

    // Compteur -1 ; à 0 l'emplacement est libéré et sa génération change (les anciennes poignées deviennent périmées).
    void release(Handle<T> handle)
    {
        std::shared_ptr<T> dropped; // détruit hors verrou
        std::lock_guard lock(mMutex);
        Slot *s = slot(handle);
        if (!s)
            return;
        if (--s->refCount > 0)
            return;
        mByPath.erase(s->path);
        dropped = std::move(s->object);
        s->path.clear();
        s->state = ResourceState::kInvalid;
        ++s->generation;
        if (s->generation == 0)
            s->generation = 1;
        mFree.push_back(handle.index);
    }

    // nullptr si périmée, en chargement ou en échec.
    std::shared_ptr<T> get(Handle<T> handle) const
    {
        std::lock_guard lock(mMutex);
        const Slot *s = slot(handle);
        return (s && s->state == ResourceState::kReady) ? s->object : nullptr;
    }

    ResourceState state(Handle<T> handle) const
    {
        std::lock_guard lock(mMutex);
        const Slot *s = slot(handle);
        return s ? s->state : ResourceState::kInvalid;
    }

    // Dernière erreur de chargement (kFailed), sinon vide.
    std::string lastError(Handle<T> handle) const
    {
        std::lock_guard lock(mMutex);
        const Slot *s = slot(handle);
        return s ? s->error : std::string();
    }

    std::uint32_t refCount(Handle<T> handle) const
    {
        std::lock_guard lock(mMutex);
        const Slot *s = slot(handle);
        return s ? s->refCount : 0;
    }

    // Rechargement à chaud : relit `path` (si chargé) ; en cas de succès le contenu est remplacé, la poignée reste.
    // En cas d'échec l'ancienne version est conservée (et l'erreur renvoyée). kNotFound si le chemin n'est pas chargé.
    Result<> reload(const std::string &path)
    {
        Handle<T> handle;
        {
            std::lock_guard lock(mMutex);
            auto it = mByPath.find(path);
            if (it == mByPath.end())
                return makeError(ErrorCode::kNotFound, "resource not loaded: " + path);
            handle = Handle<T>{it->second, mSlots[it->second].generation};
        }
        Result<T> loaded = mLoader(path);
        std::shared_ptr<T> previous;
        std::lock_guard lock(mMutex);
        Slot *s = slot(handle);
        if (!s)
            return makeError(ErrorCode::kCancelled, "resource released during reload: " + path);
        if (!loaded)
        {
            s->error = loaded.error().describe();
            return std::unexpected(loaded.error());
        }
        previous = std::move(s->object);
        s->object = std::make_shared<T>(std::move(*loaded));
        s->state = ResourceState::kReady;
        s->error.clear();
        ++s->version;
        return {};
    }

    // Nombre de rechargements réussis depuis le premier chargement (détecter un changement sans comparer le contenu).
    std::uint32_t version(Handle<T> handle) const
    {
        std::lock_guard lock(mMutex);
        const Slot *s = slot(handle);
        return s ? s->version : 0;
    }

    std::size_t liveCount() const
    {
        std::lock_guard lock(mMutex);
        return mByPath.size();
    }

  private:
    struct Slot
    {
        std::uint32_t generation = 1;
        std::uint32_t refCount = 0;
        std::uint32_t version = 0;
        std::string path;
        std::string error;
        ResourceState state = ResourceState::kInvalid;
        std::shared_ptr<T> object;
    };

    Slot *slot(Handle<T> h) { return (h.valid() && h.index < mSlots.size() && mSlots[h.index].generation == h.generation) ? &mSlots[h.index] : nullptr; }
    const Slot *slot(Handle<T> h) const { return const_cast<ResourceCache *>(this)->slot(h); }

    // Renvoie la poignée et si l'appelant doit lancer le chargement (première demande de ce chemin).
    std::pair<Handle<T>, bool> acquire(const std::string &path)
    {
        std::lock_guard lock(mMutex);
        if (auto it = mByPath.find(path); it != mByPath.end())
        {
            Slot &s = mSlots[it->second];
            ++s.refCount;
            return {Handle<T>{it->second, s.generation}, false};
        }
        std::uint32_t index;
        if (!mFree.empty())
        {
            index = mFree.back();
            mFree.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(mSlots.size());
            mSlots.emplace_back();
        }
        Slot &s = mSlots[index];
        s.refCount = 1;
        s.path = path;
        s.state = ResourceState::kLoading;
        s.error.clear();
        s.version = 0;
        mByPath[path] = index;
        return {Handle<T>{index, s.generation}, true};
    }

    void runLoad(Handle<T> handle, const std::string &path)
    {
        Result<T> loaded = mLoader(path);
        std::lock_guard lock(mMutex);
        Slot *s = slot(handle);
        if (!s)
            return; // libérée pendant le chargement
        if (loaded)
        {
            s->object = std::make_shared<T>(std::move(*loaded));
            s->state = ResourceState::kReady;
        }
        else
        {
            s->error = loaded.error().describe();
            s->state = ResourceState::kFailed;
        }
    }

    Loader mLoader;
    TaskSystem *mTasks;
    mutable std::mutex mMutex;
    std::vector<Slot> mSlots;
    std::vector<std::uint32_t> mFree;
    std::unordered_map<std::string, std::uint32_t> mByPath;
};

} // namespace engine::core
