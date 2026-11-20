// Dossier temporaire propre au processus et à l'instance, créé de façon EXCLUSIVE (create_directory échoue s'il existe) puis
// supprimé à la destruction : deux exécutions des tests en parallèle ne nettoient jamais le dossier de l'autre (G100).
#pragma once

#include <doctest/doctest.h>
#include <engine/core/system.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace tests
{

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(const std::string &prefix = "engine_test")
    {
        static std::atomic<std::uint64_t> counter{0};
        for (;;)
        {
            path = std::filesystem::temp_directory_path() /
                   (prefix + "_" + std::to_string(engine::core::system::processId()) + "_" + std::to_string(counter.fetch_add(1)));
            std::error_code ec;
            if (std::filesystem::create_directory(path, ec))
            {
                break;
            }
            // Seule une collision de nom justifie un nouvel essai ; toute autre erreur (dossier temporaire non inscriptible...) arrête
            // le test tout de suite au lieu de boucler sans fin.
            if (ec || !std::filesystem::exists(path))
            {
                FAIL("cannot create the temporary directory " << path.string() << ": " << (ec ? ec.message() : std::string("unknown error")));
            }
        }
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    void write(const std::string &name, const std::string &text) const
    {
        std::filesystem::create_directories((path / name).parent_path());
        std::ofstream(path / name, std::ios::trunc | std::ios::binary) << text;
    }
};

} // namespace tests
