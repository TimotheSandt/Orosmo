#include <doctest/doctest.h>

#include <engine/core/vfs.hpp>

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
        path = fs::temp_directory_path() / ("engine_vfs_" + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};
void touch(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
} // namespace

TEST_SUITE("core/vfs")
{
    TEST_CASE("split a logical path, reject paths without a scheme")
    {
        auto p = Vfs::split("res://shaders/a.glsl");
        REQUIRE(p.has_value());
        CHECK(p->scheme == "res");
        CHECK(p->relative == "shaders/a.glsl");
        CHECK(Vfs::split("C:\\x").has_value() == false);
        CHECK(Vfs::split("plain/path").has_value() == false);
        CHECK(Vfs::split("res://a\\b")->relative == "a/b"); // séparateurs Windows acceptés en entrée
    }

    TEST_CASE("read and write through a mount, directories are created")
    {
        TempDir t("rw");
        Vfs vfs;
        vfs.mount("user", t.path);
        REQUIRE(vfs.writeText("user://saves/slot1/state.json", "{\"a\":1}").has_value());
        CHECK(fs::exists(t.path / "saves" / "slot1" / "state.json"));
        CHECK(vfs.exists("user://saves/slot1/state.json"));
        auto text = vfs.readText("user://saves/slot1/state.json");
        REQUIRE(text.has_value());
        CHECK(*text == "{\"a\":1}");
        const std::vector<std::uint8_t> bytes{0, 1, 2, 255};
        REQUIRE(vfs.writeBytes("user://b.bin", bytes).has_value());
        CHECK(*vfs.readBytes("user://b.bin") == bytes);
    }

    TEST_CASE("errors: unknown scheme, missing file, parent traversal")
    {
        TempDir t("err");
        Vfs vfs;
        vfs.mount("res", t.path);
        CHECK((vfs.readText("nope://x").error().code == ErrorCode::kNotFound));
        CHECK((vfs.readText("res://missing.txt").error().code == ErrorCode::kNotFound));
        CHECK((vfs.resolve("res://../secret").error().code == ErrorCode::kInvalidArgument));
        CHECK((vfs.resolve("res://a/../../secret").error().code == ErrorCode::kInvalidArgument));
        CHECK_FALSE(vfs.exists("res://missing.txt"));
    }

    TEST_CASE("a later mount overrides an earlier one, only for the files it has")
    {
        TempDir base("base"), mod("mod");
        touch(base.path / "a.txt", "base-a");
        touch(base.path / "b.txt", "base-b");
        touch(mod.path / "a.txt", "mod-a");
        Vfs vfs;
        vfs.mount("res", base.path);
        vfs.mount("res", mod.path);
        CHECK(*vfs.readText("res://a.txt") == "mod-a");
        CHECK(*vfs.readText("res://b.txt") == "base-b");
        // Un nouveau fichier est écrit dans le montage le plus récent.
        REQUIRE(vfs.writeText("res://c.txt", "new").has_value());
        CHECK(fs::exists(mod.path / "c.txt"));
        CHECK_FALSE(fs::exists(base.path / "c.txt"));
        auto names = vfs.list("res://");
        REQUIRE(names.has_value());
        CHECK(*names == std::vector<std::string>{"res://a.txt", "res://b.txt", "res://c.txt"});
    }

    TEST_CASE("list a sub directory, unmount, default user directory")
    {
        TempDir t("ls");
        touch(t.path / "themes" / "dark.json", "{}");
        touch(t.path / "themes" / "light.json", "{}");
        Vfs vfs;
        vfs.mount("res", t.path);
        CHECK(*vfs.list("res://themes") == std::vector<std::string>{"res://themes/dark.json", "res://themes/light.json"});
        vfs.unmount("res");
        CHECK_FALSE(vfs.exists("res://themes/dark.json"));
        const fs::path user = Vfs::defaultUserDirectory("EngineTest");
        CHECK(user.filename() == "EngineTest");
        CHECK(user.is_absolute());
    }
}
