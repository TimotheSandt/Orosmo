// Détails internes de `app::ViewHost` partagés entre view_host.cpp (commun, version finale) et view_reloader.cpp (développement :
// vues retrouvées dans la bibliothèque de jeu, `.cui` observés, construction lancée). view_host.cpp n'appelle le rechargement que
// par `Reloader` : une version finale qui n'appelle que la forme liée de `setRoot` ne lie pas view_reloader.o.
#pragma once

#include <orosmo/app/view_host.hpp>
#include <orosmo/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace orosmo::app
{

class ViewHost::Reloader
{
  public:
    virtual ~Reloader() = default;
    virtual void poll(Impl &impl) = 0;
    virtual bool building() const noexcept = 0;
};

struct ViewHost::Impl
{
    Impl(ui::Ui &ui, ViewHostSettings settings) noexcept : ui(&ui), settings(std::move(settings)) {}

    // Installe `root` comme racine (état gardé) ; retient `code` (version de la bibliothèque qui a construit l'arbre) APRÈS la
    // destruction de l'ancien arbre.
    core::Result<> install(std::unique_ptr<ui::Widget> root, std::shared_ptr<const void> newCode);
    void fail(core::Error error);
    // Retire l'arbre de l'interface s'il est encore le nôtre (l'`Ui` existe encore : sa racine vit).
    void detachTree() noexcept;

    ui::Ui *ui;
    ViewHostSettings settings;
    std::shared_ptr<const void> view; // structure de la vue (copie de l'hôte)
    std::shared_ptr<const void> code; // version de la bibliothèque qui a construit l'arbre (nul : liée dans l'exécutable)
    const ui::Widget *root = nullptr;
    std::weak_ptr<char> rootAlive;

    // Développement.
    std::string name;
    std::uint64_t bindings = 0;
    std::size_t size = 0;
    std::uint32_t builtVersion = 0; // version du module de jeu qui a construit l'arbre (ou qui a échoué)
    std::unique_ptr<Reloader> reloader;

    std::uint32_t rebuilds = 0;
    std::uint32_t builds = 0;
    std::uint32_t failures = 0;
    std::optional<core::Error> lastError;
    std::string lastBuildOutput;
};

} // namespace orosmo::app
