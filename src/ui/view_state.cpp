// UI-08 (A79 lot L8) : état d'interaction gardé d'un arbre à l'autre (voir view_state.hpp).
#include "orosmo/ui/view_state.hpp"

#include "orosmo/ui/ui.hpp"

#include <set>
#include <string>
#include <utility>

namespace orosmo::ui
{
namespace
{

template <typename Visit> void forEachWidget(Widget &w, const Visit &visit)
{
    visit(w);
    for (const auto &child : w.children())
        forEachWidget(*child, visit);
    if (Widget *detached = w.detached())
        forEachWidget(*detached, visit);
}

bool empty(const RetainedState &state) noexcept { return !state.scroll && !state.expanded && !state.selected; }

} // namespace

ViewState captureViewState(const Ui &ui)
{
    ViewState state;
    if (Widget *focused = ui.focused())
        state.focused = focused->id();
    state.focusVisible = ui.focusVisible();
    if (Widget *root = ui.root())
    {
        std::set<std::string, std::less<>> seen; // premier widget de chaque identifiant, même sans état à garder
        forEachWidget(*root, [&](Widget &w) {
            if (w.id().empty() || !seen.insert(w.id()).second)
                return;
            RetainedState retained;
            w.saveRetained(retained);
            if (!empty(retained))
                state.widgets.emplace(w.id(), retained);
        });
    }
    return state;
}

void restoreViewState(Ui &ui, const ViewState &state)
{
    if (Widget *root = ui.root())
    {
        std::map<std::string, bool, std::less<>> done; // premier widget de chaque identifiant seulement
        forEachWidget(*root, [&](Widget &w) {
            if (w.id().empty() || !done.emplace(w.id(), true).second)
                return;
            if (auto it = state.widgets.find(w.id()); it != state.widgets.end())
                w.restoreRetained(ui, it->second);
        });
    }
    if (!state.focused.empty())
    {
        Widget *target = ui.find(state.focused);
        if (target != nullptr && target->canTakeFocus())
        {
            ui.focus(target);
            ui.setFocusVisible(state.focusVisible);
        }
    }
}

core::Result<> replaceRoot(Ui &ui, std::unique_ptr<Widget> &root)
{
    if (ui.updating())
        return core::makeError(core::ErrorCode::kBusy, "ui: replaceRoot during Ui::update (the new tree would be installed later)");
    const ViewState state = captureViewState(ui);
    ui.setRoot(std::move(root));
    restoreViewState(ui, state);
    return {};
}

std::size_t countWidgets(const Widget &root)
{
    std::size_t count = 1;
    for (const auto &child : root.children())
        count += countWidgets(*child);
    if (const Widget *detached = root.detached())
        count += countWidgets(*detached);
    return count;
}

} // namespace orosmo::ui
