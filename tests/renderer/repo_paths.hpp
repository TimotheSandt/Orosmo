// Chemins du dépôt pour les tests : indépendants du dossier courant. On cherche `shaders/rhi/mesh.vert` en remontant depuis des points
// de départ ABSOLUS : le dossier de l'exécutable des tests (apps/demo/bin/tests/ dans le dépôt, quel que soit le dossier d'où on le
// lance), le dossier courant, puis __FILE__ (relatif au dossier de compilation). Échec : chemin vide, le test le signale.
#pragma once

#include <engine/core/system.hpp>

#include <filesystem>

namespace tests
{

inline std::filesystem::path repoRoot(const char *sourceFile)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path starts[] = {engine::core::system::executablePath().parent_path(), fs::current_path(ec),
                               fs::absolute(fs::path(sourceFile).parent_path(), ec)};
    for (const fs::path &start : starts)
    {
        for (fs::path dir = start; !dir.empty(); dir = dir.parent_path())
        {
            if (fs::exists(dir / "shaders" / "rhi" / "mesh.vert", ec))
            {
                return dir;
            }
            if (dir == dir.parent_path())
            {
                break;
            }
        }
    }
    return {};
}

} // namespace tests
