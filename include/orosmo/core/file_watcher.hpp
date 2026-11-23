// CORE-13 : observateur de fichiers (shaders, thèmes, interface, paramètres, bibliothèque de jeu).
// Implémentation par interrogation (date de modification + taille) : identique sur Linux, Windows et macOS,
// sans dépendance. poll() est appelé par la boucle principale ; les rappels s'exécutent sur le thread appelant,
// ce qui convient au rechargement à chaud (pas de verrou côté utilisateur). Les API natives (inotify,
// ReadDirectoryChangesW, FSEvents) pourront remplacer poll() derrière la même interface (docs/GAPS.md).
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace orosmo::core
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

    // Cadence (horloge réelle, steady_clock) : un poll() appelé moins de `interval` après le précédent ne fait rien et renvoie 0, ce
    // qui permet de l'appeler à chaque image sans parcourir les dossiers 1000 fois par seconde. 0 (défaut) : chaque appel interroge.
    void setPollInterval(std::chrono::milliseconds interval) noexcept { mPollInterval = interval; }
    // Un dossier n'est parcouru en entier (détection des fichiers CRÉÉS) qu'un poll() sur `every` ; les autres ne regardent que les
    // fichiers déjà suivis (date et taille : modifié, supprimé). 1 (défaut) : parcours complet à chaque poll().
    void setFullScanEvery(unsigned every) noexcept { mFullScanEvery = every == 0 ? 1 : every; }

    // Compte les interrogations effectives (hors appels ignorés par la cadence), pour les tests et les diagnostics.
    struct Stats
    {
        std::uint64_t polls = 0;       // poll() qui ont interrogé le disque
        std::uint64_t fullScans = 0;   // parcours complets de dossier ou de fichier racine
        std::uint64_t trackedScans = 0; // vérifications des seuls fichiers suivis
    };
    Stats stats() const noexcept { return mStats; }

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
        unsigned sinceFullScan = 0;
    };
    // `complete` vaut false si le parcours a été interrompu (répertoire illisible, erreur d'E/S) : le résultat est alors
    // partiel et ne doit pas être comparé à l'état connu.
    static std::vector<std::pair<std::filesystem::path, Stamp>> scan(const Watch &w, bool &complete);

    // Date et taille des seuls fichiers de `w.known` (ceux qui n'existent plus sont absents du résultat).
    static std::vector<std::pair<std::filesystem::path, Stamp>> scanTracked(const Watch &w);

    std::vector<Watch> mWatches;
    std::chrono::milliseconds mPollInterval{0};
    unsigned mFullScanEvery = 1;
    std::optional<std::chrono::steady_clock::time_point> mLastPoll;
    Stats mStats;
    WatchId mNextId = 0;
};

} // namespace orosmo::core
