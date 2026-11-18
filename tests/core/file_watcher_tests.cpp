#include <doctest/doctest.h>

#include <engine/core/file_watcher.hpp>

#include <filesystem>
#include <fstream>

using namespace engine::core;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path path;
    explicit TempDir(const std::string &name)
    {
        path = fs::temp_directory_path() / ("engine_fw_" + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};
struct PermissionGuard // rend les droits à la sortie du test, même si un CHECK échoue
{
    fs::path path;
    ~PermissionGuard()
    {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, ec);
    }
};
void write(const fs::path &p, const std::string &text, int secondsOffset = 0)
{
    fs::create_directories(p.parent_path());
    {
        std::ofstream(p, std::ios::trunc) << text;
    }
    // Le test ne dépend pas de la finesse de l'horloge du système de fichiers : on décale la date explicitement.
    const auto base = fs::file_time_type::clock::time_point{} + std::chrono::hours(24 * 365 * 40); // date fixe
    fs::last_write_time(p, base + std::chrono::seconds(secondsOffset));
}
} // namespace

TEST_SUITE("core/file_watcher")
{
    TEST_CASE("created, modified, removed are reported once, in a directory (recursive)")
    {
        TempDir t("dir");
        write(t.path / "existing.json", "1", -100);
        FileWatcher w;
        std::vector<FileEvent> events;
        w.watch(t.path, [&](const FileEvent &e) { events.push_back(e); });
        CHECK(w.poll() == 0); // l'état initial n'est pas un changement

        write(t.path / "sub" / "new.json", "x");
        CHECK(w.poll() == 1);
        REQUIRE(events.size() == 1);
        CHECK((events[0].change == FileChange::kCreated));
        CHECK(events[0].path == t.path / "sub" / "new.json");
        CHECK(w.poll() == 0);

        write(t.path / "existing.json", "22", 10);
        CHECK(w.poll() == 1);
        CHECK((events.back().change == FileChange::kModified));

        fs::remove(t.path / "sub" / "new.json");
        CHECK(w.poll() == 1);
        CHECK((events.back().change == FileChange::kRemoved));
    }

    TEST_CASE("a single file that does not exist yet can be watched")
    {
        TempDir t("file");
        const fs::path f = t.path / "settings.json";
        FileWatcher w;
        int n = 0;
        w.watch(f, [&](const FileEvent &e) {
            ++n;
            CHECK(e.path == f);
        });
        CHECK(w.poll() == 0);
        write(f, "{}");
        CHECK(w.poll() == 1);
        write(f, "{\"a\":1}", 5);
        CHECK(w.poll() == 1);
        CHECK(n == 2);
    }

    TEST_CASE("extension filter and unwatch")
    {
        TempDir t("ext");
        FileWatcher w;
        int n = 0;
        auto id = w.watch(t.path, [&](const FileEvent &) { ++n; }, {".glsl"});
        write(t.path / "a.txt", "ignored");
        write(t.path / "a.glsl", "shader");
        CHECK(w.poll() == 1);
        w.unwatch(id);
        write(t.path / "b.glsl", "shader");
        CHECK(w.poll() == 0);
        CHECK(n == 1);
    }

    TEST_CASE("a callback may unwatch itself")
    {
        TempDir t("self");
        FileWatcher w;
        FileWatcher::WatchId id = 0;
        int n = 0;
        id = w.watch(t.path, [&](const FileEvent &) {
            ++n;
            w.unwatch(id);
        });
        write(t.path / "a", "1");
        write(t.path / "b", "2");
        w.poll();
        write(t.path / "c", "3");
        w.poll();
        CHECK(n == 2); // les deux événements du premier poll, aucun du second
    }

#ifdef __linux__
    TEST_CASE("a directory that cannot be read during a scan is not reported as removed files")
    {
        TempDir t("locked");
        write(t.path / "sub" / "a.txt", "1", -50);
        write(t.path / "b.txt", "2", -50);
        FileWatcher w;
        std::vector<FileEvent> events;
        w.watch(t.path, [&](const FileEvent &e) { events.push_back(e); });
        PermissionGuard guard{t.path / "sub"};
        fs::permissions(t.path / "sub", fs::perms::none);
        std::error_code ec;
        const bool enforced = !fs::is_directory(t.path / "sub", ec) || [&] {
            std::error_code e2;
            fs::directory_iterator it(t.path / "sub", e2);
            return static_cast<bool>(e2);
        }();
        if (enforced)
        {
            CHECK(w.poll() == 0); // scan incomplet : on garde l'état connu, pas de « removed » fantôme
            CHECK(events.empty());
            fs::permissions(t.path / "sub", fs::perms::owner_all);
            CHECK(w.poll() == 0); // à nouveau lisible : rien n'a changé
        }
    }
#endif
}
