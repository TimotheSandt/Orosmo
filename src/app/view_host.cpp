// UI-08 (A79 lot L8) : partie commune de `app::ViewHost` (version finale : vue liée dans l'exécutable). Ni bibliothèque, ni
// observateur, ni commande ici : c'est view_reloader.cpp (développement).
#include "view_host_impl.hpp"

#include <orosmo/core/log.hpp>
#include <orosmo/ui/ui.hpp>
#include <orosmo/ui/view_state.hpp>

#include <utility>

namespace orosmo::app
{

core::Result<> ViewHost::Impl::install(std::unique_ptr<ui::Widget> newRoot, std::shared_ptr<const void> newCode)
{
    if (!newRoot)
        return core::makeError(core::ErrorCode::kInvalidArgument, "view " + name + ": the view built no widget");
    const ui::Widget *built = newRoot.get();
    std::weak_ptr<char> alive = newRoot->lifetimeToken();
    if (auto replaced = ui::replaceRoot(*ui, newRoot); !replaced)
    {
        newRoot.reset(); // rendu intact : détruit tant que le code qui l'a construit est retenu (`newCode`)
        return replaced;
    }
    root = built;
    rootAlive = std::move(alive);
    code = std::move(newCode); // l'ancien arbre est détruit : l'ancienne version peut partir
    return {};
}

void ViewHost::Impl::fail(core::Error error)
{
    ++failures;
    LOG_ERROR(static_cast<int>(error.code), "ui view: " + error.describe());
    if (settings.notify)
        ui->notify(error.message, ui::NotificationKind::kError, 10.0f);
    lastError = std::move(error);
}

void ViewHost::Impl::detachTree() noexcept
{
    if (!rootAlive.expired() && ui->root() == root)
        ui->setRoot(nullptr);
    root = nullptr;
    rootAlive.reset();
}

ViewHost::ViewHost(ui::Ui &ui, ViewHostSettings settings) : impl_(std::make_unique<Impl>(ui, std::move(settings))) {}

ViewHost::~ViewHost()
{
    impl_->reloader.reset(); // attend une construction en cours
    impl_->detachTree();     // l'arbre part avant le code qui l'a construit
    if (impl_->ui->updating())
    {
        // Détruit depuis un rappel de `Ui::update` : le retrait de l'arbre est différé à la fin de l'image ; le code et la
        // structure de la vue (que ses gestionnaires visent) restent retenus jusque-là (revue PR #37). Structure relâchée d'abord.
        impl_->ui->retainUntilRootReplaced(std::move(impl_->code));
        impl_->ui->retainUntilRootReplaced(std::move(impl_->view));
    }
    impl_->code.reset();
}

core::Result<> ViewHost::mountLinked(std::shared_ptr<const void> view, LinkedBuild build)
{
    if (impl_->ui->updating())
        return core::makeError(core::ErrorCode::kBusy, "ViewHost::setRoot during Ui::update");
    std::unique_ptr<ui::Widget> root = build(view.get(), *impl_->ui);
    auto installed = impl_->install(std::move(root), nullptr);
    if (installed)
        impl_->view = std::move(view); // les gestionnaires de l'arbre peuvent viser la structure (blocs @cui-cpp)
    return installed;
}

void ViewHost::update()
{
    if (impl_->reloader)
        impl_->reloader->poll(*impl_);
}

bool ViewHost::reloadable() const noexcept { return impl_->reloader != nullptr; }
bool ViewHost::building() const noexcept { return impl_->reloader && impl_->reloader->building(); }
std::uint32_t ViewHost::rebuilds() const noexcept { return impl_->rebuilds; }
std::uint32_t ViewHost::builds() const noexcept { return impl_->builds; }
std::uint32_t ViewHost::failures() const noexcept { return impl_->failures; }
const std::optional<core::Error> &ViewHost::lastError() const noexcept { return impl_->lastError; }
const std::string &ViewHost::lastBuildOutput() const noexcept { return impl_->lastBuildOutput; }

} // namespace orosmo::app
