// CORE-04 : système de tâches. Pool de threads, dépendances, attente d'une tâche ou d'un groupe, arrêt propre.
// Une tâche ne doit pas lancer d'exception (règle 10) ; si elle le fait, l'exception est journalisée et la tâche
// compte comme terminée, pour que ses dépendants ne restent pas bloqués.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace orosmo::core
{

namespace detail
{
struct TaskNode;
}

// Référence partagée vers une tâche (utile comme dépendance ou pour l'attente). Valeur vide = « rien à attendre ».
class TaskHandle
{
  public:
    TaskHandle() = default;
    bool valid() const { return mNode != nullptr; }
    bool done() const;

  private:
    friend class TaskSystem;
    explicit TaskHandle(std::shared_ptr<detail::TaskNode> node) : mNode(std::move(node)) {}
    std::shared_ptr<detail::TaskNode> mNode;
};

class TaskSystem
{
  public:
    // threads == 0 : std::thread::hardware_concurrency() - 1, au moins 1.
    explicit TaskSystem(unsigned threads = 0);
    ~TaskSystem(); // termine toutes les tâches soumises, puis arrête les threads
    TaskSystem(const TaskSystem &) = delete;
    TaskSystem &operator=(const TaskSystem &) = delete;

    // `fn` s'exécute quand toutes les `dependencies` sont terminées.
    TaskHandle submit(std::function<void()> fn, std::span<const TaskHandle> dependencies = {});

    // Attend la tâche. Le thread appelant l'exécute lui-même si elle est prête, sinon il exécute d'autres tâches
    // prêtes pendant l'attente : attendre une tâche fille depuis une tâche ne bloque pas le pool, même avec un seul
    // thread. Limite connue (docs/GAPS.md G56) : une tâche prête exécutée « par aide » est empilée sur l'appelant ;
    // si elle attend elle-même cet appelant, le pool peut se bloquer (il faudrait suspendre les tâches).
    void wait(const TaskHandle &task);
    // Attend que toutes les tâches soumises soient terminées.
    void waitIdle();

    // Exécute fn(i) pour i dans [0, count), découpé en blocs de `grain`, et attend la fin.
    void parallelFor(std::size_t count, std::size_t grain, const std::function<void(std::size_t)> &fn);

    unsigned threadCount() const { return static_cast<unsigned>(mThreads.size()); }
    std::size_t pendingTasks() const { return mOutstanding.load(); }

  private:
    friend struct detail::TaskNode;
    void enqueue(std::shared_ptr<detail::TaskNode> node);
    bool runOne();                                             // exécute une tâche prête si elle existe
    bool runSpecific(const std::shared_ptr<detail::TaskNode> &); // exécute CETTE tâche si elle est dans la file
    void execute(const std::shared_ptr<detail::TaskNode> &node);
    void workerLoop();

    std::mutex mMutex;
    std::condition_variable mWake;
    std::condition_variable mIdle;
    std::deque<std::shared_ptr<detail::TaskNode>> mReady;
    std::vector<std::thread> mThreads;
    std::atomic<std::size_t> mOutstanding{0};
    bool mStopping = false;
};

// Groupe de tâches : lancer plusieurs tâches, attendre la dernière.
class TaskGroup
{
  public:
    explicit TaskGroup(TaskSystem &system) : mSystem(&system) {}
    ~TaskGroup() { wait(); }
    TaskGroup(const TaskGroup &) = delete;
    TaskGroup &operator=(const TaskGroup &) = delete;

    void run(std::function<void()> fn, std::span<const TaskHandle> dependencies = {})
    {
        mTasks.push_back(mSystem->submit(std::move(fn), dependencies));
    }
    void wait()
    {
        for (const TaskHandle &t : mTasks)
            mSystem->wait(t);
        mTasks.clear();
    }

  private:
    TaskSystem *mSystem;
    std::vector<TaskHandle> mTasks;
};

} // namespace orosmo::core
