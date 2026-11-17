// CORE-11 : système de fichiers virtuel. Les chemins logiques (`res://shaders/a.glsl`, `user://settings.json`)
// sont indépendants de l'OS : séparateur `/`, pas de lettre de lecteur. Un schéma peut avoir plusieurs montages ;
// le plus récent est consulté d'abord (un dossier `user://` peut ainsi surcharger `res://`).
// Un chemin qui remonte au-dessus de sa racine (`res://../x`) est refusé.
#pragma once

#include <engine/core/error.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core
{

class Vfs
{
  public:
    // Associe `scheme` (sans « :// ») à un dossier réel. Plusieurs montages possibles ; le dernier prime.
    void mount(std::string scheme, std::filesystem::path directory);
    void unmount(std::string_view scheme);

    // Chemin réel d'un chemin logique. Pour un fichier existant : le premier montage qui le contient ;
    // sinon (écriture d'un nouveau fichier) : le montage le plus récent.
    Result<std::filesystem::path> resolve(std::string_view logicalPath) const;
    bool exists(std::string_view logicalPath) const;

    Result<std::string> readText(std::string_view logicalPath) const;
    Result<std::vector<std::uint8_t>> readBytes(std::string_view logicalPath) const;
    // Crée les dossiers manquants. Écrit dans le montage le plus récent du schéma.
    Result<> writeText(std::string_view logicalPath, std::string_view content) const;
    Result<> writeBytes(std::string_view logicalPath, const std::vector<std::uint8_t> &content) const;

    // Chemins logiques des fichiers d'un dossier (tous montages confondus, triés, sans doublon).
    Result<std::vector<std::string>> list(std::string_view logicalDirectory) const;

    // Dossier de données utilisateur par défaut : Linux $XDG_DATA_HOME ou ~/.local/share/<app>, Windows %APPDATA%\<app>,
    // macOS ~/Library/Application Support/<app>.
    static std::filesystem::path defaultUserDirectory(const std::string &appName);

    // Sépare "res://a/b" en {"res", "a/b"} ; vide si le chemin n'a pas de schéma.
    struct Parts
    {
        std::string scheme;
        std::string relative;
    };
    static Result<Parts> split(std::string_view logicalPath);

  private:
    Result<std::vector<std::filesystem::path>> candidates(std::string_view logicalPath) const;
    std::map<std::string, std::vector<std::filesystem::path>, std::less<>> mMounts; // montages par ordre d'ajout
};

} // namespace engine::core
