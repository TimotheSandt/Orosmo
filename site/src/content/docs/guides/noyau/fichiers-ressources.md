---
title: "Fichiers, ressources, rechargement"
description: "Lire et écrire par chemins logiques res:// et user:// avec Vfs, trouver les ressources de l'application, partager une ressource par handle avec ResourceCache, surveiller des fichiers avec FileWatcher."
sidebar:
  order: 5
---

Quatre outils du noyau, à combiner selon le besoin :

- `Vfs` : des chemins logiques (`res://shaders/a.glsl`, `user://settings.json`) indépendants du système ;
- `GET_RESOURCE_PATH` et `setWorkingDirectoryToExecutable` : les chemins des ressources livrées avec l'application ;
- `ResourceCache<T>` : un chargement par chemin, partagé par handle, asynchrone si on veut, rechargeable ;
- `FileWatcher` : signale les fichiers créés, modifiés ou supprimés.

En-têtes : `orosmo/core/vfs.hpp`, `paths.hpp`, `resources.hpp`, `file_watcher.hpp`.

## Exemple minimal

```cpp
#include <orosmo/core/paths.hpp>
#include <orosmo/core/vfs.hpp>

orosmo::core::Vfs vfs;
vfs.mount("res", GET_RESOURCE_PATH("."));                                    // dossier des ressources (orosmo/core/paths.hpp)
vfs.mount("user", orosmo::core::Vfs::defaultUserDirectory("mygame"));

if (auto r = vfs.writeText("user://saves/slot1/state.json", R"({"a":1})"); !r) { /* r.error() */ }
auto text = vfs.readText("user://saves/slot1/state.json");                  // Result<std::string>
```

Adapté de [`tests/core/vfs_tests.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/tests/core/vfs_tests.cpp). Les chemins de
ressources suivent les [conventions des exemples](/demarrer/premiere-application/#conventions-des-exemples).

## Vfs : chemins logiques

Un chemin logique est `schéma://chemin/relatif`, avec `/` comme séparateur (`\` est accepté en entrée). Un schéma peut
avoir plusieurs montages ; le dernier monté est consulté en premier, ce qui permet à un dossier de surcharger un autre.

| Méthode | Rend | Effet |
| --- | --- | --- |
| `mount(scheme, directory)` | — | Associe `scheme` (sans `://`) à un dossier réel. |
| `unmount(scheme)` | — | Retire tous les montages du schéma. |
| `resolve(path)` | `Result<std::filesystem::path>` | Fichier existant : le premier montage qui le contient ; sinon le montage le plus récent. |
| `exists(path)` | `bool` | |
| `readText`, `readBytes` | `Result<std::string>`, `Result<std::vector<std::uint8_t>>` | `kNotFound` si le fichier ou le schéma manque. |
| `writeText`, `writeBytes` | `Result<>` | Écrit dans le montage le plus récent ; crée les dossiers manquants. |
| `list(directory)` | `Result<std::vector<std::string>>` | Chemins logiques des fichiers, tous montages confondus, triés, sans doublon. |
| `defaultUserDirectory(app)` | `std::filesystem::path` | Linux `$XDG_DATA_HOME/<app>` ou `~/.local/share/<app>`, Windows `%APPDATA%\<app>`, macOS `~/Library/Application Support/<app>`. |
| `split(path)` | `Result<Parts>` | `{"res", "a/b"}` ; erreur si le chemin n'a pas de schéma. |

Un chemin qui remonte au-dessus de sa racine (`res://../x`, `res://a/../../x`) est refusé avec `kInvalidArgument`.

## Ressources livrées avec l'application

Le projet créé par `orosmo init` et la démo ne passent pas encore par `Vfs` : ils utilisent la macro `GET_RESOURCE_PATH` et
un dossier courant fixé à celui de l'exécutable.

```cpp
#include <orosmo/core/paths.hpp>

int main()
{
    orosmo::core::setWorkingDirectoryToExecutable();          // en premier : les chemins suivants sont relatifs
    const std::filesystem::path shaderDir = GET_RESOURCE_PATH("shader/rhi");
    // ...
}
```

| Symbole | Effet |
| --- | --- |
| `GET_RESOURCE_PATH("x")` | `"res/x"` sans `RELEASE`, `"x"` avec `RELEASE` (le paquet installé pose les ressources à côté de l'exécutable). |
| `setWorkingDirectoryToExecutable()` | Le dossier courant devient celui de l'exécutable ; en cas d'échec, une erreur est journalisée et le dossier ne change pas. |
| `validateAssets(paths, &missing)` | `true` si tous les fichiers existent ; chaque absent est journalisé et ajouté à `missing`. |

:::caution
`GET_RESOURCE_PATH` colle des chaînes à la compilation : il n'accepte qu'un littéral (`GET_RESOURCE_PATH(name)` avec une
variable ne compile pas). Un chemin de la ligne de commande se résout par rapport au dossier de lancement, à noter avant
l'appel à `setWorkingDirectoryToExecutable()` (la démo garde `std::filesystem::current_path()` pour cela).
:::

## ResourceCache : partager et recharger

Un `ResourceCache<T>` gère un type de ressource. On lui donne le chargeur, une fonction `Result<T>(const std::string &path)` ;
le cache ne connaît ni le disque ni le GPU. Le même chemin rend le même handle et augmente un compteur de références.

```cpp
#include <orosmo/core/resources.hpp>

using namespace orosmo::core;

TaskSystem tasks;
// Le chargeur tourne dans les tâches : ne pas monter ni démonter `vfs` pendant les chargements.
ResourceCache<std::string> texts([&](const std::string &path) { return vfs.readText(path); }, &tasks);

Handle<std::string> h = texts.loadAsync("res://intro.txt");     // état kLoading, puis kReady ou kFailed
// ... plus tard, à l'image suivante par exemple :
if (texts.state(h) == ResourceState::kReady)
    show(*texts.get(h));                                         // get : std::shared_ptr ; show : fonction de l'application
else if (texts.state(h) == ResourceState::kFailed)
    LOG_WARNING(texts.lastError(h));

(void)texts.reload("res://intro.txt");                           // même handle, nouveau contenu
texts.release(h);                                                // compteur -1 ; à 0 le handle devient périmé
```

| Méthode | Effet |
| --- | --- |
| `ResourceCache(loader, tasks)` | `tasks` nul (défaut) : `loadAsync` charge tout de suite, sur le thread appelant. |
| `load(path)` | Charge ou retrouve ; bloquant. Au retour l'état est `kReady` ou `kFailed`. |
| `loadAsync(path)` | Charge sur le `TaskSystem`. |
| `get(handle)` | `std::shared_ptr<T>`, nul si périmé, en chargement ou en échec. |
| `state(handle)` | `kInvalid`, `kLoading`, `kReady`, `kFailed`. |
| `lastError(handle)` | Texte de la dernière erreur de chargement. |
| `reload(path)` | Relit ; en cas d'échec l'ancienne version reste et l'erreur est rendue ; `kNotFound` si le chemin n'est pas chargé. |
| `version(handle)` | Nombre de rechargements réussis (détecter un changement sans comparer le contenu). |
| `release(handle)`, `refCount(handle)`, `liveCount()` | Comptage. |

Le handle porte une génération : après `release` final, l'emplacement est réutilisé avec une autre génération et l'ancien
handle ne désigne jamais la nouvelle ressource. `get` rend un `shared_ptr` : un thread qui lit la ressource la garde vivante
même si elle est rechargée ou libérée pendant ce temps. Le cache est thread-safe ; son destructeur attend ses propres
chargements en cours.

Les textures du module `assets` ont leur propre cache, `assets::TextureCache` ([Textures](/guides/contenus/textures/)).

## FileWatcher : surveiller des fichiers

`FileWatcher` compare à chaque `poll()` la date de modification et la taille des fichiers. Les rappels s'exécutent sur le
thread qui appelle `poll()`, donc sans verrou côté application.

```cpp
#include <orosmo/core/file_watcher.hpp>

orosmo::core::FileWatcher watcher;
watcher.setPollInterval(std::chrono::milliseconds(500));   // au plus 2 interrogations par seconde
watcher.setFullScanEvery(4);                               // nouveaux fichiers cherchés un poll sur 4
auto id = watcher.watch(GET_RESOURCE_PATH("config"), [&](const orosmo::core::FileEvent &e) {
    if (e.change == orosmo::core::FileChange::kModified)
        (void)config.loadFile(e.path);
}, {".json"});

while (running)
{
    watcher.poll();   // à chaque image : la cadence évite de parcourir le disque à chaque appel
    // ...
}
```

Réglages repris de la démo ([`apps/demo/src/Game/Game.cpp`](https://github.com/TimotheSandt/Orosmo/blob/dev/apps/demo/src/Game/Game.cpp)).

| Membre | Défaut | Effet |
| --- | --- | --- |
| `watch(path, callback, extensions)` | `extensions` vide = tous | Fichier, ou dossier récursif ; il peut ne pas exister encore. Seuls les changements après l'appel sont signalés. Rend un `WatchId`. |
| `unwatch(id)` | — | Arrête la surveillance ; un rappel peut se retirer lui-même. |
| `setPollInterval(ms)` | 0 | Un `poll()` appelé moins de `ms` après le précédent (horloge réelle) ne fait rien et rend 0. |
| `setFullScanEvery(n)` | 1 | Un dossier n'est parcouru en entier (fichiers créés) qu'un `poll()` sur `n` ; les autres vérifient seulement les fichiers déjà suivis (modifiés, supprimés). |
| `poll()` | — | Compare, appelle les rappels, rend le nombre d'événements. |
| `stats()` | — | `polls`, `fullScans`, `trackedScans`. |

Un dossier illisible pendant un parcours n'est pas pris pour des fichiers supprimés : le parcours partiel est ignoré.

D'autres objets du moteur se branchent sur un `FileWatcher` : les thèmes de l'interface (`ui.themes().watch(watcher)`,
[Thèmes JSON](/guides/interface/themes/)). Les shaders ont leur propre rechargement (`ShaderLibrary::pollHotReload`,
[Shaders](/guides/avance/shaders/)).

## Limites

- `FileWatcher` interroge le disque ; il n'utilise pas les notifications du système (inotify, ReadDirectoryChangesW,
  FSEvents). Son fonctionnement est vérifié sous Linux ; sous Windows et macOS, il n'est pas encore prouvé par un test.
- La démo et le projet de départ utilisent `GET_RESOURCE_PATH`, pas encore `Vfs`.
- `Vfs` et `FileWatcher` ne sont pas thread-safe : un seul propriétaire.

## Voir aussi

- [Configuration, réflexion, sérialisation](/guides/noyau/configuration/) : recharger un fichier de réglages.
- [Tâches en parallèle](/guides/noyau/taches/)
- [Formats de fichiers](/reference/formats/)
