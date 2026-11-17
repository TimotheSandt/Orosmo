#include <doctest/doctest.h>

#include <engine/core/resources.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <string>

using namespace engine::core;

namespace
{
struct Text
{
    std::string value;
};

struct FakeDisk
{
    std::mutex mutex;
    std::map<std::string, std::string> files;
    std::atomic<int> reads{0};
    int delayMs = 0;
    ResourceCache<Text>::Loader loader()
    {
        return [this](const std::string &path) -> Result<Text> {
            ++reads;
            if (delayMs)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            std::lock_guard lock(mutex);
            auto it = files.find(path);
            if (it == files.end())
                return makeError(ErrorCode::kNotFound, path);
            return Text{it->second};
        };
    }
};
} // namespace

TEST_SUITE("core/resources")
{
    TEST_CASE("load, share by path, reference counting")
    {
        FakeDisk disk;
        disk.files["a.txt"] = "A";
        ResourceCache<Text> cache(disk.loader());
        Handle<Text> h1 = cache.load("a.txt");
        Handle<Text> h2 = cache.load("a.txt");
        CHECK(h1 == h2);
        CHECK(disk.reads.load() == 1);
        CHECK(cache.refCount(h1) == 2);
        REQUIRE(cache.get(h1) != nullptr);
        CHECK(cache.get(h1)->value == "A");
        cache.release(h1);
        CHECK(cache.get(h2) != nullptr);
        cache.release(h2);
        CHECK(cache.get(h2) == nullptr);
        CHECK(cache.liveCount() == 0);
    }

    TEST_CASE("generation: a stale handle never reaches the resource that reuses its slot")
    {
        FakeDisk disk;
        disk.files["a"] = "A";
        disk.files["b"] = "B";
        ResourceCache<Text> cache(disk.loader());
        Handle<Text> a = cache.load("a");
        cache.release(a);
        Handle<Text> b = cache.load("b");
        CHECK(a.index == b.index);          // emplacement réutilisé
        CHECK(a.generation != b.generation);
        CHECK(cache.get(a) == nullptr);     // l'ancienne poignée ne voit pas « b »
        CHECK((cache.state(a) == ResourceState::kInvalid));
        REQUIRE(cache.get(b) != nullptr);
        CHECK(cache.get(b)->value == "B");
        CHECK(cache.get(Handle<Text>{}) == nullptr);
        cache.release(a); // sans effet sur b
        CHECK(cache.refCount(b) == 1);
    }

    TEST_CASE("failed load is reported, not thrown")
    {
        FakeDisk disk;
        ResourceCache<Text> cache(disk.loader());
        Handle<Text> h = cache.load("missing");
        CHECK((cache.state(h) == ResourceState::kFailed));
        CHECK(cache.get(h) == nullptr);
        CHECK(cache.lastError(h).find("kNotFound") != std::string::npos);
    }

    TEST_CASE("async load: kLoading then kReady, no duplicate read")
    {
        FakeDisk disk;
        disk.files["big"] = "BIG";
        disk.delayMs = 30;
        TaskSystem tasks(2);
        ResourceCache<Text> cache(disk.loader(), &tasks);
        Handle<Text> h = cache.loadAsync("big");
        Handle<Text> again = cache.loadAsync("big");
        CHECK(h == again);
        CHECK((cache.state(h) == ResourceState::kLoading));
        CHECK(cache.get(h) == nullptr);
        tasks.waitIdle();
        CHECK((cache.state(h) == ResourceState::kReady));
        CHECK(cache.get(h)->value == "BIG");
        CHECK(disk.reads.load() == 1);
    }

    TEST_CASE("hot reload keeps the handle and swaps the content; a failing reload keeps the old content")
    {
        FakeDisk disk;
        disk.files["cfg"] = "v1";
        ResourceCache<Text> cache(disk.loader());
        Handle<Text> h = cache.load("cfg");
        auto held = cache.get(h); // un lecteur garde l'ancienne version
        CHECK(cache.version(h) == 0);
        disk.files["cfg"] = "v2";
        REQUIRE(cache.reload("cfg").has_value());
        CHECK(cache.get(h)->value == "v2");
        CHECK(held->value == "v1"); // l'ancien objet reste valide pour qui le tient
        CHECK(cache.version(h) == 1);

        disk.files.erase("cfg");
        CHECK_FALSE(cache.reload("cfg").has_value());
        CHECK(cache.get(h)->value == "v2");
        CHECK(cache.version(h) == 1);
        CHECK((cache.reload("never-loaded").error().code == ErrorCode::kNotFound));
    }

    TEST_CASE("release during an async load is safe")
    {
        FakeDisk disk;
        disk.files["x"] = "X";
        disk.delayMs = 30;
        TaskSystem tasks(1);
        ResourceCache<Text> cache(disk.loader(), &tasks);
        Handle<Text> h = cache.loadAsync("x");
        cache.release(h);
        tasks.waitIdle();
        CHECK(cache.get(h) == nullptr);
        CHECK(cache.liveCount() == 0);
    }
}
