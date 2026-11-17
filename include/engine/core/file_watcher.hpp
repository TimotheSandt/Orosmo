// CORE-13 : observateur de fichiers (shaders, thèmes, interface, paramètres, bibliothèque de jeu).
// Implémentation par interrogation (date de modification + taille) : identique sur Linux, Windows et macOS,
// sans dépendance. poll() est appelé par la boucle principale ; les rappels s'exécutent sur le thread appelant,
// ce qui convient au rechargement à chaud (pas de verrou côté utilisateur). Les API natives (inotify,
// ReadDirectoryChangesW, FSEvents) pourront remplacer poll() derrière la même interface (docs/GAPS.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace engine::core
{

enum class FileChange : std::uint8_t
{
    kCreated,
    kModified,
    kRemoved,
};

struct FileEvent
{
    std::filesystem::path path;
    FileChange change;
};

class FileWatcher
{
  public:
    using Callback = std::function<void(const FileEvent &)>;
    using WatchId = std::uint64_t;

    // Observe un fichier, ou un dossier (récursif). Le fichier/dossier peut ne pas exister encore.
    // `extensions` (ex. {".glsl", ".json"}) filtre les fichiers d'un dossier ; vide = tous.
    // L'état initial est enregistré : seuls les changements APRÈS l'appel sont signalés.
    WatchId watch(std::filesystem::path path, Callback callback, std::vector<std::string> extensions = {});
    void unwatch(WatchId id);

    // Compare à l'état précédent, appelle les rappels, renvoie le nombre d'événements.
    std::size_t poll();

  private:
    struct Stamp
    {
        std::filesystem::file_time_type time;
        std::uintmax_t size;
        bool operator==(const Stamp &) const = default;
    };
    struct Watch
    {
        WatchId id;
        std::filesystem::path root;
        Callback callback;
        std::vector<std::string> extensions;
        std::vector<std::pair<std::filesystem::path, Stamp>> known; // trié par chemin
    };
    static std::vector<std::pair<std::filesystem::path, Stamp>> scan(const Watch &w);

    std::vector<Watch> mWatches;
    WatchId mNextId = 0;
};

} // namespace engine::core
