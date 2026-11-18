// Chemins du dépôt pour les tests : indépendants du dossier courant. On cherche `shaders/rhi/mesh.vert` en remontant depuis le dossier
// courant (apps/demo sous `make demo-test`, racine du dépôt ou autre quand on lance le binaire à la main), puis depuis __FILE__.
#pragma once

#include <filesystem>

namespace tests
{

inline std::filesystem::path repoRoot(const char *sourceFile)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::path start : {fs::current_path(ec), fs::absolute(fs::path(sourceFile).parent_path(), ec)})
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
    return fs::path(sourceFile).parent_path().parent_path().parent_path(); // repli : ancien comportement
}

} // namespace tests
