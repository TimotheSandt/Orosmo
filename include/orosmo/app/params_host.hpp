// GAME-05 (A79 lot L6) : paramètres de jeu dans un fichier JSON, relus à chaud sans compilation.
//
// `ParamsFile` : une structure décrite (`scene::ComponentDesc` : taille, alignement, champs réfléchis, valeur par défaut, tous
// copiés côté moteur) dont la valeur vient d'un fichier JSON {champ: valeur}. Champ absent : valeur par défaut ; champ inconnu :
// ignoré avec un avertissement. Le fichier est observé (`core::FileWatcher`) ; `poll()` (une fois par image) relit un fichier
// modifié : s'il est valide, la nouvelle valeur remplace l'ancienne À LA MÊME ADRESSE et `revision()` augmente ; s'il est
// invalide (JSON illisible, type ou plage d'un champ), la dernière valeur valide est gardée, l'erreur journalisée et rendue par
// `lastError()` (comme `ui::ThemeHost`). Fichier absent au départ : valeurs par défaut, `lastError()` ; le créer l'installe.
//
// `ParamsHost<T>` : la même chose, typée, pour l'hôte. `Params<T>` : poignée légère rendue au jeu par
// `GameContext::registerParams<T>` (fichier tenu par le moteur, `app::GameModule`) ; à capturer par valeur dans un système.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/world.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orosmo::app
{

struct ParamsSettings
{
    bool watch = true;
    // Cadence d'interrogation du fichier (`core::FileWatcher::setPollInterval`).
    std::chrono::milliseconds pollInterval{100};
};

class ParamsFile
{
  public:
    // Ouvre (et lit) le fichier. Seule une description invalide est une erreur (kInvalidArgument) ; un fichier absent ou
    // invalide donne les valeurs par défaut et `lastError()`.
    static core::Result<ParamsFile> open(std::filesystem::path path, scene::ComponentDesc desc, ParamsSettings settings = {});

    ParamsFile(ParamsFile &&) noexcept;
    ParamsFile &operator=(ParamsFile &&) noexcept;
    ParamsFile(const ParamsFile &) = delete;
    ParamsFile &operator=(const ParamsFile &) = delete;
    ~ParamsFile();

    // Relit le fichier s'il a changé depuis le dernier appel ; vrai si une nouvelle valeur est installée.
    bool poll();
    // Relit le fichier tout de suite ; en cas d'erreur, la dernière valeur valide est gardée.
    core::Result<> reload();

    // `desc().size` octets, alignés sur `desc().alignment` ; adresse stable pendant toute la vie de l'objet.
    const void *data() const noexcept;
    const scene::ComponentDesc &desc() const noexcept;
    const std::filesystem::path &path() const noexcept;
    // Nombre de valeurs installées depuis un fichier (0 : valeurs par défaut seulement).
    std::uint64_t revision() const noexcept;
    const std::optional<core::Error> &lastError() const noexcept;
    // Avertissements de la dernière lecture valide (champs inconnus).
    const std::vector<std::string> &warnings() const noexcept;

  private:
    struct State;
    explicit ParamsFile(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

template <typename T> class Params
{
  public:
    Params() = default;
    explicit Params(const ParamsFile *file) noexcept : file_(file) {}
    const T &get() const noexcept { return *scene::detail::componentCast<const T>(file_->data()); }
    const T *operator->() const noexcept { return &get(); }
    const ParamsFile *file() const noexcept { return file_; }

  private:
    const ParamsFile *file_ = nullptr;
};

template <core::Reflected T>
    requires scene::ComponentData<T>
class ParamsHost
{
  public:
    static core::Result<ParamsHost> open(std::filesystem::path path, ParamsSettings settings = {})
    {
        // Nom des messages : celui du fichier, sans extension.
        std::string name = path.stem().string();
        auto file = ParamsFile::open(std::move(path), scene::describeComponent<T>(name.empty() ? "params" : name), settings);
        if (!file)
            return std::unexpected(std::move(file.error()));
        return ParamsHost(std::move(*file));
    }

    const T &get() const noexcept { return *scene::detail::componentCast<const T>(file_.data()); }
    const T *operator->() const noexcept { return &get(); }
    bool poll() { return file_.poll(); }
    core::Result<> reload() { return file_.reload(); }
    std::uint64_t revision() const noexcept { return file_.revision(); }
    const std::optional<core::Error> &lastError() const noexcept { return file_.lastError(); }
    const ParamsFile &file() const noexcept { return file_; }

  private:
    explicit ParamsHost(ParamsFile file) noexcept : file_(std::move(file)) {}
    ParamsFile file_;
};

} // namespace orosmo::app
