// Bibliothèque dynamique chargée à l'exécution (M8, A79 lot L1 ; base de GAME-01 et UI-08) : `dlopen` (Linux, macOS) ou
// `LoadLibraryW` (Windows). Par défaut, le fichier est d'abord COPIÉ sous un nom unique (`<nom>-<pid>-<n><ext>`, à côté de
// l'original) et c'est la copie qui est chargée : l'original reste libre d'être réécrit par l'éditeur de liens pendant que
// l'ancienne version tourne (Windows verrouille un .dll chargé), et deux versions successives ont deux chemins différents (le
// chargeur ne rend pas l'ancienne image pour un chemin déjà ouvert). La copie est effacée au déchargement.
//
// Liaison avec le moteur : la bibliothèque n'embarque PAS le moteur ; ses symboles du moteur sont résolus dans l'exécutable hôte
// (même `Logger`, mêmes registres). Options de lien données par `tools/engine-sdk/engine.mk` (`ENGINE_HOST_LDFLAGS`,
// `ENGINE_MODULE_LDFLAGS`) ; preuve : `tests/platform/dynamic_library_tests.cpp`.
#pragma once

#include <orosmo/core/error.hpp>

#include <bit>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <utility>

namespace orosmo::platform
{

struct DynamicLibraryOptions
{
    // Charger une copie sous un nom unique (défaut) plutôt que le fichier lui-même.
    bool copyBeforeLoad = true;
    // Dossier de la copie (relatif : au dossier courant au moment du chargement) ; vide = dossier de l'original. Sous Windows, les
    // dépendances sont cherchées d'abord dans le dossier de la COPIE (LOAD_WITH_ALTERED_SEARCH_PATH) : un autre dossier doit
    // contenir les DLL voisines dont la bibliothèque dépend.
    std::filesystem::path copyDirectory = {};
};

class DynamicLibrary
{
  public:
    // Extension des bibliothèques dynamiques de la plateforme : ".dll", ".dylib" ou ".so".
    static std::string_view extension() noexcept;

    // Charge `path`. Erreurs : kNotFound (fichier absent), kIo (copie impossible, ou refus du chargeur : format, symbole du
    // moteur introuvable, dépendance absente ; le message du système est repris).
    static core::Result<DynamicLibrary> load(const std::filesystem::path &path, const DynamicLibraryOptions &options = {});

    DynamicLibrary() = default;
    DynamicLibrary(DynamicLibrary &&other) noexcept;
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept;
    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    ~DynamicLibrary();

    bool isLoaded() const noexcept { return handle_ != nullptr; }

    // Adresse d'un symbole exporté (nom C, `extern "C"`). kNotFound s'il est absent, kInvalidArgument si rien n'est chargé.
    core::Result<void *> symbol(std::string_view name) const;

    // Même chose, typé : `lib.function<int(int, int)>("add")`.
    template <typename Signature> core::Result<Signature *> function(std::string_view name) const
    {
        static_assert(std::is_function_v<Signature>, "DynamicLibrary::function<R(Args...)>");
        core::Result<void *> address = symbol(name);
        if (!address)
            return std::unexpected(std::move(address.error()));
        return std::bit_cast<Signature *>(*address);
    }

    // Décharge la bibliothèque et efface sa copie. Sans effet si rien n'est chargé. Tout pointeur obtenu par `symbol` devient
    // invalide.
    void unload() noexcept;

    const std::filesystem::path &sourcePath() const noexcept { return sourcePath_; }
    // Fichier réellement chargé : la copie, ou `sourcePath()` sans copie.
    const std::filesystem::path &loadedPath() const noexcept { return loadedPath_; }

  private:
    void *handle_ = nullptr;
    bool ownsCopy_ = false;
    std::filesystem::path sourcePath_;
    std::filesystem::path loadedPath_;
};

} // namespace orosmo::platform
