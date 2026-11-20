// CORE-10 : gestionnaire de ressources. Handles avec génération (une poignée périmée ne retombe jamais sur une autre
// ressource), comptage de références, chargement asynchrone (TaskSystem), rechargement à chaud (même poignée, nouveau contenu).
//
// Un ResourceCache<T> gère un type de ressource. Le chargeur est une fonction `Result<T>(path)` fournie par l'appelant
// (le cache ne connaît ni le disque ni le GPU : couche core). get() renvoie un shared_ptr : un rechargement ou une
// libération pendant qu'un thread lit la ressource ne l'invalide pas.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/tasks.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace orosmo::core
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
        // Aucun chargement lancé par CE cache ne doit lui survivre. On n'attend que ceux-là (pas waitIdle()) : un cache
        // détruit depuis une tâche du même TaskSystem ne doit pas attendre cette tâche elle-même.
        std::vector<TaskHandle> pending;
        {
            std::lock_guard lock(mMutex);
            pending.swap(mPending);
        }
        if (mTasks)
            for (const TaskHandle &t : pending)
                mTasks->wait(t);
    }
    ResourceCache(const ResourceCache &) = delete;
    ResourceCache &operator=(const ResourceCache &) = delete;

    // Charge (ou retrouve) la ressource : même chemin = même poignée, compteur +1. Bloquant : si un chargement de ce
    // chemin est déjà en cours (loadAsync, autre thread), attend sa fin ; au retour l'état est kReady ou kFailed.
    Handle<T> load(const std::string &path)
    {
        Acquired a = acquire(path, false);
        if (a.needsLoad)
            runLoad(a.handle, path, a.op);
        else
            waitLoaded(a);
        return a.handle;
    }

    // Comme load(), mais le chargement se fait sur le TaskSystem. state() passe de kLoading à kReady/kFailed.
    Handle<T> loadAsync(const std::string &path)
    {
        Acquired a = acquire(path, true); // avec un TaskSystem, la tâche est soumise sous le verrou (voir acquire)
        if (a.needsLoad)
            runLoad(a.handle, path, a.op);
        return a.handle;
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
        s->task = TaskHandle();
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
        std::uint64_t op = 0;
        {
            std::lock_guard lock(mMutex);
            auto it = mByPath.find(path);
            if (it == mByPath.end())
                return makeError(ErrorCode::kNotFound, "resource not loaded: " + path);
            handle = Handle<T>{it->second, mSlots[it->second].generation};
            op = ++mSlots[it->second].lastOp; // numéro d'opération : une complétion plus ancienne ne le remplacera pas
        }
        Result<T> loaded = mLoader(path);
        std::shared_ptr<T> previous;
        std::lock_guard lock(mMutex);
        Slot *s = slot(handle);
        if (!s)
            return makeError(ErrorCode::kCancelled, "resource released during reload: " + path);
        if (!loaded)
        {
            if (op > s->publishedOp)
                s->error = loaded.error().describe();
            return std::unexpected(loaded.error());
        }
        if (op < s->publishedOp)
            return {}; // un chargement plus récent a déjà publié son résultat
        previous = std::move(s->object);
        s->object = std::make_shared<T>(std::move(*loaded));
        s->publishedOp = op;
        s->state = ResourceState::kReady;
        s->error.clear();
        ++s->version;
        mLoaded.notify_all();
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
        std::uint64_t lastOp = 0;      // dernier numéro d'opération (chargement initial ou rechargement) attribué
        std::uint64_t publishedOp = 0; // numéro de l'opération dont le résultat est publié
        TaskHandle task;               // chargement asynchrone initial (vide si synchrone)
    };

    struct Acquired
    {
        Handle<T> handle;
        bool needsLoad;     // l'appelant doit appeler runLoad lui-même
        std::uint64_t op;   // numéro de l'opération de chargement initial
        TaskHandle task;    // chargement déjà en cours à attendre (load() sur un chemin déjà demandé)
    };

    Slot *slot(Handle<T> h) { return (h.valid() && h.index < mSlots.size() && mSlots[h.index].generation == h.generation) ? &mSlots[h.index] : nullptr; }
    const Slot *slot(Handle<T> h) const { return const_cast<ResourceCache *>(this)->slot(h); }

    // Retrouve ou crée l'emplacement du chemin. Avec async et un TaskSystem, la tâche de chargement est soumise ici,
    // sous le verrou, et rangée dans l'emplacement : un load() concurrent la trouve toujours pour l'attendre.
    Acquired acquire(const std::string &path, bool async)
    {
        std::lock_guard lock(mMutex);
        if (auto it = mByPath.find(path); it != mByPath.end())
        {
            Slot &s = mSlots[it->second];
            ++s.refCount;
            return {Handle<T>{it->second, s.generation}, false, 0, s.task};
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
        s.lastOp = 0;
        s.publishedOp = 0;
        const std::uint64_t op = ++s.lastOp;
        mByPath[path] = index;
        const Handle<T> handle{index, s.generation};
        if (async && mTasks)
        {
            std::erase_if(mPending, [](const TaskHandle &t) { return t.done(); });
            s.task = mTasks->submit([this, handle, path, op] { runLoad(handle, path, op); });
            mPending.push_back(s.task);
            return {handle, false, op, s.task};
        }
        return {handle, true, op, TaskHandle()};
    }

    // Attend la fin du chargement initial d'une ressource déjà demandée. Avec une tâche : TaskSystem::wait (le thread
    // aide, donc pas d'interblocage même depuis un worker) ; sans tâche (autre thread en train de charger) : variable
    // de condition.
    void waitLoaded(const Acquired &a)
    {
        if (a.task.valid() && mTasks)
            mTasks->wait(a.task);
        std::unique_lock lock(mMutex);
        mLoaded.wait(lock, [&] {
            const Slot *s = slot(a.handle);
            return !s || s->state != ResourceState::kLoading;
        });
    }

    void runLoad(Handle<T> handle, const std::string &path, std::uint64_t op)
    {
        Result<T> loaded = mLoader(path);
        std::lock_guard lock(mMutex);
        Slot *s = slot(handle);
        if (!s)
        {
            mLoaded.notify_all();
            return; // libérée pendant le chargement
        }
        if (op >= s->publishedOp)
        {
            // Sinon un rechargement plus récent a déjà publié : cette complétion tardive est ignorée.
            if (loaded)
            {
                s->object = std::make_shared<T>(std::move(*loaded));
                s->state = ResourceState::kReady;
                s->error.clear();
                s->publishedOp = op;
            }
            else
            {
                s->error = loaded.error().describe();
                s->state = ResourceState::kFailed;
                s->publishedOp = op;
            }
        }
        mLoaded.notify_all();
    }

    Loader mLoader;
    TaskSystem *mTasks;
    mutable std::mutex mMutex;
    std::vector<Slot> mSlots;
    std::vector<std::uint32_t> mFree;
    std::unordered_map<std::string, std::uint32_t> mByPath;
    std::vector<TaskHandle> mPending; // chargements asynchrones lancés par ce cache (attendus par le destructeur)
    std::condition_variable mLoaded;
};

} // namespace orosmo::core
