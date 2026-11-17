#include <engine/core/tasks.hpp>

#include <algorithm>
#include <exception>

#include "Utilities/Logger.h"

namespace engine::core
{

namespace detail
{
struct TaskNode
{
    std::function<void()> fn;
    std::mutex mutex; // protège done et dependents
    std::condition_variable doneCv;
    bool done = false;
    std::vector<std::shared_ptr<TaskNode>> dependents;
    std::atomic<int> pending{1}; // dépendances non terminées + 1 de retenue pendant l'enregistrement
};
} // namespace detail

bool TaskHandle::done() const
{
    if (!mNode)
        return true;
    std::lock_guard lock(mNode->mutex);
    return mNode->done;
}

TaskSystem::TaskSystem(unsigned threads)
{
    if (threads == 0)
    {
        const unsigned hw = std::thread::hardware_concurrency();
        threads = hw > 1 ? hw - 1 : 1;
    }
    mThreads.reserve(threads);
    for (unsigned i = 0; i < threads; ++i)
        mThreads.emplace_back([this] { workerLoop(); });
}

TaskSystem::~TaskSystem()
{
    waitIdle();
    {
        std::lock_guard lock(mMutex);
        mStopping = true;
    }
    mWake.notify_all();
    for (std::thread &t : mThreads)
        t.join();
}

TaskHandle TaskSystem::submit(std::function<void()> fn, std::span<const TaskHandle> dependencies)
{
    auto node = std::make_shared<detail::TaskNode>();
    node->fn = std::move(fn);
    mOutstanding.fetch_add(1);

    for (const TaskHandle &dep : dependencies)
    {
        if (!dep.mNode)
            continue;
        std::lock_guard lock(dep.mNode->mutex);
        if (!dep.mNode->done)
        {
            node->pending.fetch_add(1);
            dep.mNode->dependents.push_back(node);
        }
    }
    if (node->pending.fetch_sub(1) == 1) // retenue levée, plus aucune dépendance en cours
        enqueue(node);
    return TaskHandle(std::move(node));
}

void TaskSystem::enqueue(std::shared_ptr<detail::TaskNode> node)
{
    {
        std::lock_guard lock(mMutex);
        mReady.push_back(std::move(node));
    }
    mWake.notify_one();
}

void TaskSystem::execute(const std::shared_ptr<detail::TaskNode> &node)
{
    try
    {
        if (node->fn)
            node->fn();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(1, std::string("task threw an exception: ") + e.what());
    }
    catch (...)
    {
        LOG_ERROR(1, "task threw an unknown exception");
    }
    node->fn = nullptr; // libère les captures

    std::vector<std::shared_ptr<detail::TaskNode>> dependents;
    {
        std::lock_guard lock(node->mutex);
        node->done = true;
        dependents.swap(node->dependents);
    }
    node->doneCv.notify_all();
    for (auto &d : dependents)
        if (d->pending.fetch_sub(1) == 1)
            enqueue(d);

    if (mOutstanding.fetch_sub(1) == 1)
    {
        std::lock_guard lock(mMutex); // évite de rater le réveil de waitIdle
        mIdle.notify_all();
    }
}

bool TaskSystem::runOne()
{
    std::shared_ptr<detail::TaskNode> node;
    {
        std::lock_guard lock(mMutex);
        if (mReady.empty())
            return false;
        node = std::move(mReady.front());
        mReady.pop_front();
    }
    execute(node);
    return true;
}

void TaskSystem::workerLoop()
{
    for (;;)
    {
        std::shared_ptr<detail::TaskNode> node;
        {
            std::unique_lock lock(mMutex);
            mWake.wait(lock, [this] { return mStopping || !mReady.empty(); });
            if (mReady.empty())
                return; // mStopping et plus rien à faire
            node = std::move(mReady.front());
            mReady.pop_front();
        }
        execute(node);
    }
}

void TaskSystem::wait(const TaskHandle &task)
{
    if (!task.mNode)
        return;
    for (;;)
    {
        {
            std::lock_guard lock(task.mNode->mutex);
            if (task.mNode->done)
                return;
        }
        if (runOne())
            continue;
        std::unique_lock lock(task.mNode->mutex);
        task.mNode->doneCv.wait_for(lock, std::chrono::milliseconds(1), [&] { return task.mNode->done; });
    }
}

void TaskSystem::waitIdle()
{
    while (mOutstanding.load() != 0)
    {
        if (runOne())
            continue;
        std::unique_lock lock(mMutex);
        mIdle.wait_for(lock, std::chrono::milliseconds(1), [this] { return mOutstanding.load() == 0; });
    }
}

void TaskSystem::parallelFor(std::size_t count, std::size_t grain, const std::function<void(std::size_t)> &fn)
{
    if (count == 0)
        return;
    grain = std::max<std::size_t>(1, grain);
    TaskGroup group(*this);
    for (std::size_t begin = 0; begin < count; begin += grain)
    {
        const std::size_t end = std::min(count, begin + grain);
        group.run([&fn, begin, end] {
            for (std::size_t i = begin; i < end; ++i)
                fn(i);
        });
    }
    group.wait();
}

} // namespace engine::core
