#include "orosmo/ui/ui.hpp"

#include <orosmo/core/log.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_set>

namespace orosmo::ui
{

// ── Raccourcis ───────────────────────────────────────────────────────────────────────────────────────────────────────

std::string normalizeShortcut(std::string_view text)
{
    auto lower = [](std::string_view s)
    {
        std::string out(s);
        for (char &c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    bool ctrl = false, alt = false, shift = false, super = false;
    std::string key;
    std::size_t start = 0;
    while (start <= text.size())
    {
        // « + » seul comme touche : "Ctrl++" ; sinon « + » sépare.
        std::size_t end = text.find('+', start);
        if (end == start && end + 1 == text.size())
            end = std::string_view::npos;
        std::string_view part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
            part.remove_prefix(1);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
            part.remove_suffix(1);
        const std::string l = lower(part);
        if (l == "ctrl" || l == "control" || l == "ctl")
            ctrl = true;
        else if (l == "alt" || l == "option" || l == "opt")
            alt = true;
        else if (l == "shift" || l == "maj")
            shift = true;
        else if (l == "super" || l == "cmd" || l == "command" || l == "meta" || l == "win")
            super = true;
        else if (!part.empty())
        {
            if (!key.empty())
                return {}; // deux touches
            if (part.size() == 1)
                key = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
            else
            {
                static const std::pair<const char *, const char *> kNames[] = {
                    {"del", "Delete"},         {"delete", "Delete"},     {"suppr", "Delete"},  {"enter", "Enter"},      {"return", "Enter"},
                    {"esc", "Escape"},         {"escape", "Escape"},     {"tab", "Tab"},       {"space", "Space"},      {"backspace", "Backspace"},
                    {"left", "Left"},          {"right", "Right"},       {"up", "Up"},         {"down", "Down"},        {"home", "Home"},
                    {"end", "End"},            {"pageup", "PageUp"},     {"pgup", "PageUp"},   {"pagedown", "PageDown"}, {"pgdn", "PageDown"},
                    {"insert", "Insert"},      {"ins", "Insert"},        {"plus", "+"},        {"minus", "-"},
                };
                key.clear();
                for (const auto &[from, to] : kNames)
                    if (l == from)
                        key = to;
                if (key.empty() && (l[0] == 'f') && l.size() <= 3 && std::all_of(l.begin() + 1, l.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                    key = "F" + l.substr(1);
                if (key.empty())
                    return {};
            }
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    if (key.empty())
        return {};
    std::string out;
    if (ctrl)
        out += "Ctrl+";
    if (alt)
        out += "Alt+";
    if (shift)
        out += "Shift+";
    if (super)
        out += "Super+";
    return out + key;
}

// ── ActionRegistry ───────────────────────────────────────────────────────────────────────────────────────────────────

void ActionRegistry::add(std::string name, Handler action)
{
    warned_.erase(name);
    actions_[std::move(name)] = std::move(action);
}

void ActionRegistry::remove(std::string_view name)
{
    if (auto it = actions_.find(name); it != actions_.end())
    {
        actions_.erase(it);
    }
}

bool ActionRegistry::contains(std::string_view name) const { return actions_.contains(name); }

bool ActionRegistry::invoke(std::string_view name, const Event &event)
{
    auto it = actions_.find(name);
    if (it == actions_.end() || !it->second)
    {
        if (warned_.insert(std::string(name)).second)
        {
            LOG_WARNING("UI: no action named '", std::string(name), "' is registered (ActionRegistry::add)");
        }
        return false;
    }
    it->second(event);
    return true;
}

Handler ActionRegistry::bind(std::string name)
{
    requested_.insert(name);
    return [this, name = std::move(name)](const Event &event) { (void)invoke(name, event); };
}

std::vector<std::string> ActionRegistry::missing() const
{
    std::vector<std::string> out;
    for (const std::string &name : requested_)
    {
        if (!actions_.contains(name))
        {
            out.push_back(name);
        }
    }
    return out;
}

// ── Ui ───────────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
bool inSubtree(const Widget *root, const Widget *w)
{
    for (const Widget *p = w; p != nullptr; p = p->parent())
    {
        if (p == root)
        {
            return true;
        }
    }
    return false;
}

void collect(const Widget &w, std::unordered_set<const Widget *> &out)
{
    out.insert(&w);
    for (const auto &child : w.children())
    {
        collect(*child, out);
    }
    if (const Widget *d = w.detached())
    {
        collect(*d, out);
    }
}

Widget *interactiveAncestor(Widget *w)
{
    for (Widget *p = w; p != nullptr; p = p->parent())
    {
        if (p->interactive())
        {
            return p->enabled() ? p : nullptr;
        }
    }
    return nullptr;
}

// Le widget arrête la souris : il est interactif ou il a un fond visible (un conteneur transparent la laisse passer à la scène).
bool blocksMouse(const Ui &ui, const Widget *w)
{
    for (const Widget *p = w; p != nullptr; p = p->parent())
    {
        const Style s = p->style(ui.theme());
        if (p->interactive() || s.background.a > 0.0f || s.image)
        {
            return true;
        }
    }
    return false;
}
} // namespace

Ui::Ui(const Font &font) : font_(&font) {}
Ui::~Ui() = default;

void Ui::setRoot(std::unique_ptr<Widget> root)
{
    if (updating_)
    {
        pendingRoot_ = std::move(root);
        hasPendingRoot_ = true; // setRoot(nullptr) pendant la mise à jour retire aussi l'arbre à la fin de l'image
        return;
    }
    closePopup();
    modals_.clear();
    hovered_ = active_ = focused_ = nullptr;
    tooltipText_.clear();
    root_ = std::move(root);
}

Widget *Ui::find(std::string_view id) const noexcept
{
    if (root_)
    {
        if (Widget *w = root_->find(id))
        {
            return w;
        }
    }
    for (const PopupEntry &p : popups_)
    {
        if (Widget *w = p.view->find(id))
        {
            return w;
        }
    }
    return nullptr;
}

// Le widget focalisé a pu être détruit depuis la dernière mise à jour (`clearChildren` public, sans `forget`) : son jeton de durée
// de vie est vérifié avant tout accès.
bool Ui::wantsKeyboard() const noexcept { return focused_ != nullptr && !focusedAlive_.expired() && focused_->acceptsText(); }

Widget *Ui::hitTestIn(Widget &w, glm::vec2 p)
{
    if (!w.visible())
    {
        return nullptr;
    }
    // Un enfant qui dépasse de son parent reste cliquable là où il est dessiné, sauf si le parent coupe son contenu : alors
    // seulement dans la zone de découpage de ses enfants, la même qu'au dessin (ScrollView : sa zone intérieure).
    const bool inside = w.rect().contains(p);
    const bool clips = w.clipsChildren() || w.overflow == Overflow::kHidden;
    if (!inside && clips)
    {
        return nullptr;
    }
    if (!clips || (w.clipsChildren() ? w.childClip() : w.rect()).contains(p))
    {
        const auto children = w.children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (Widget *hit = hitTestIn(**it, p))
            {
                return hit;
            }
        }
    }
    return inside ? &w : nullptr;
}

Widget *Ui::hitTest(glm::vec2 p) const
{
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
    {
        if (Widget *hit = hitTestIn(*it->view, p))
        {
            return hit;
        }
    }
    if (!modals_.empty())
    {
        return hitTestIn(*modals_.back().panel, p); // une fenêtre modale masque le reste de l'interface
    }
    return root_ ? hitTestIn(*root_, p) : nullptr;
}

void Ui::dropDestroyed() noexcept
{
    if (hovered_ != nullptr && hoveredAlive_.expired())
        hovered_ = nullptr;
    if (active_ != nullptr && activeAlive_.expired())
        active_ = nullptr;
    if (focused_ != nullptr && focusedAlive_.expired())
        focused_ = nullptr;
}

void Ui::setHovered(Widget *target)
{
    dropDestroyed();
    if (target == hovered_ && (target == nullptr || !hoveredAlive_.expired()))
    {
        return;
    }
    if (hovered_ != nullptr)
    {
        hovered_->hovered_ = false;
        hovered_->onHoverChanged(*this, false);
        hovered_->emit(*this, EventKind::kHover, false);
    }
    hovered_ = target;
    hoveredAlive_ = target != nullptr ? std::weak_ptr<char>(target->lifetime_) : std::weak_ptr<char>();
    if (hovered_ != nullptr)
    {
        hovered_->hovered_ = true;
        hovered_->onHoverChanged(*this, true);
        hovered_->emit(*this, EventKind::kHover, true);
    }
}

void Ui::focus(Widget *widget)
{
    if (widget == focused_ && (widget == nullptr || !focusedAlive_.expired())) // même adresse qu'un widget détruit : nouveau focus
    {
        return;
    }
    Widget *old = focusedAlive_.expired() ? nullptr : focused_; // détruit depuis : rien à prévenir
    focused_ = widget;
    focusedAlive_ = widget != nullptr ? std::weak_ptr<char>(widget->lifetime_) : std::weak_ptr<char>();
    if (old != nullptr)
    {
        old->focused_ = false;
        old->onFocusChanged(*this, false);
    }
    if (widget != nullptr)
    {
        widget->focused_ = true;
        widget->onFocusChanged(*this, true);
    }
}

void Ui::collectFocusable(Widget &w, std::vector<Widget *> &out) const
{
    if (!w.visible())
    {
        return;
    }
    if (w.focusable() && w.enabled())
    {
        out.push_back(&w);
    }
    for (const auto &child : w.children())
    {
        collectFocusable(*child, out);
    }
}

void Ui::focusNext(bool backwards)
{
    std::vector<Widget *> order;
    if (!modals_.empty())
    {
        collectFocusable(*modals_.back().panel, order); // le focus reste dans la fenêtre modale
    }
    else if (root_)
    {
        collectFocusable(*root_, order);
    }
    if (order.empty())
    {
        focus(nullptr);
        return;
    }
    auto it = std::find(order.begin(), order.end(), focused_);
    std::size_t index = 0;
    if (it == order.end())
    {
        index = backwards ? order.size() - 1 : 0;
    }
    else
    {
        const auto current = static_cast<std::size_t>(it - order.begin());
        index = backwards ? (current + order.size() - 1) % order.size() : (current + 1) % order.size();
    }
    focus(order[index]);
}

void Ui::defer(std::function<void()> action) { deferred_.push_back(std::move(action)); }

void Ui::flushDeferred()
{
    // Une action peut en programmer d'autres (un changement qui en déclenche un autre) : on vide jusqu'à stabilité, borné à
    // huit vagues par image. Ce qui reste (chaîne plus longue) est GARDÉ, dans l'ordre, pour l'image suivante : aucune écriture
    // n'est perdue.
    for (int round = 0; round < 8 && !deferred_.empty(); ++round)
    {
        std::vector<std::function<void()>> batch;
        batch.swap(deferred_);
        for (auto &action : batch)
        {
            action();
        }
    }
}

// ── Fenêtres surgissantes ─────────────────────────────────────────────────────────────────────────────────────────────

void Ui::pushPopup(Widget &owner, std::unique_ptr<Widget> owned, Widget &view, const Rect &anchor, PopupPlacement placement)
{
    // Ouverte depuis une fenêtre déjà ouverte (sous-menu) : celles au-dessus du propriétaire se ferment ; sinon, toutes.
    std::size_t keep = 0;
    for (std::size_t i = popups_.size(); i-- > 0;)
    {
        if (inSubtree(popups_[i].view, &owner))
        {
            keep = i + 1;
            break;
        }
    }
    closePopupsFrom(keep);
    PopupEntry entry;
    entry.owner = &owner;
    entry.owned = std::move(owned);
    entry.view = &view;
    entry.anchor = anchor;
    entry.placement = placement;
    popups_.push_back(std::move(entry));
    layoutPopups();
}

void Ui::openPopup(Widget &owner, std::unique_ptr<Widget> content, const Rect &anchor, PopupPlacement placement)
{
    Widget &view = *content;
    pushPopup(owner, std::move(content), view, anchor, placement);
}

void Ui::openPopup(Widget &owner, Widget &content, const Rect &anchor, PopupPlacement placement)
{
    pushPopup(owner, nullptr, content, anchor, placement);
}

bool Ui::popupOpenFor(const Widget &owner) const noexcept
{
    return std::any_of(popups_.begin(), popups_.end(), [&](const PopupEntry &p) { return p.owner == &owner; });
}

Widget *Ui::popup() const noexcept { return popups_.empty() ? nullptr : popups_.back().view; }

Widget *Ui::popupOwnerOf(const Widget &w) const noexcept
{
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (inSubtree(it->view, &w))
            return it->owner;
    return nullptr;
}

void Ui::closePopupOf(const Widget &owner)
{
    for (std::size_t i = 0; i < popups_.size(); ++i)
    {
        if (popups_[i].owner == &owner)
        {
            closePopupsFrom(i);
            return;
        }
    }
}

namespace
{
bool offerShortcut(Ui &ui, Widget &w, std::string_view shortcut)
{
    if (!w.visible() || !w.enabled())
        return false;
    if (w.onShortcut(ui, shortcut))
        return true;
    for (const auto &child : w.children())
        if (offerShortcut(ui, *child, shortcut))
            return true;
    // Sous-arbre détaché (éléments d'un menu fermé, corps d'une boîte fermée) : un menu fermé garde ses raccourcis.
    if (Widget *d = w.detached(); d != nullptr && d->visible())
    {
        if (d->onShortcut(ui, shortcut))
            return true;
        for (const auto &child : d->children())
            if (offerShortcut(ui, *child, shortcut))
                return true;
    }
    return false;
}
} // namespace

bool Ui::dispatchShortcut(std::string_view shortcut)
{
    const std::string s = normalizeShortcut(shortcut);
    if (s.empty())
        return false;
    if (!modals_.empty())
        return offerShortcut(*this, *modals_.back().panel, s);
    return root_ && offerShortcut(*this, *root_, s);
}

void Ui::forgetSubtree(const Widget *root)
{
    dropDestroyed(); // détruits (survol, appui, focus) : ni parcours de leurs parents ni notification
    for (Widget **p : {&hovered_, &active_, &focused_})
    {
        if (*p != nullptr && inSubtree(root, *p))
        {
            // Le widget survit (contenu emprunté d'un menu) : ses états survolé, appuyé, focus sont remis à zéro, sinon il reste
            // dessiné « survolé » à la prochaine ouverture.
            if (p == &hovered_)
                hovered_->hovered_ = false;
            else if (p == &active_)
                active_->pressed_ = false;
            else
            {
                Widget *w = focused_;
                focused_ = nullptr;
                w->focused_ = false;
                w->onFocusChanged(*this, false);
                continue;
            }
            *p = nullptr;
        }
    }
}

void Ui::closePopupsFrom(std::size_t level, bool flush)
{
    while (popups_.size() > level)
    {
        PopupEntry entry = std::move(popups_.back());
        popups_.pop_back();
        forgetSubtree(entry.view);
        if (entry.owned)
        {
            // Détruit après l'image : un événement en attente peut encore viser un widget de la fenêtre. Possédé par l'action
            // elle-même : libéré aussi si elle n'est jamais exécutée (interface détruite avant l'image suivante).
            defer([owned = std::shared_ptr<Widget>(std::move(entry.owned))]() mutable { owned.reset(); });
        }
    }
    if (flush && !updating_)
    {
        flushDeferred();
    }
}

void Ui::closePopup() { closePopupsFrom(0); }

void Ui::forget(const Widget &widget) noexcept
{
    if (hovered_ == &widget)
        hovered_ = nullptr;
    if (active_ == &widget)
        active_ = nullptr;
    if (focused_ == &widget)
        focused_ = nullptr;
}

void Ui::layoutPopups()
{
    for (PopupEntry &p : popups_)
    {
        const glm::vec2 m = p.view->measure(*this, viewport_);
        const Rect &a = p.anchor;
        float w = std::min(m.x, viewport_.x);
        const float h = std::min(m.y, viewport_.y);
        float x = a.position.x;
        float y = a.position.y;
        if (p.placement == PopupPlacement::kBelow)
        {
            w = std::min(std::max(m.x, a.size.x), viewport_.x);
            y = a.end().y;
            if (y + h > viewport_.y && a.position.y - h >= 0.0f)
                y = a.position.y - h;
        }
        else if (p.placement == PopupPlacement::kRight)
        {
            x = a.end().x;
            if (x + w > viewport_.x && a.position.x - w >= 0.0f)
                x = a.position.x - w;
        }
        x = std::clamp(x, 0.0f, std::max(0.0f, viewport_.x - w));
        y = std::clamp(y, 0.0f, std::max(0.0f, viewport_.y - h));
        p.view->layout(*this, {{x, y}, {w, h}});
    }
}

// ── Fenêtres modales, notifications, presse-papiers ───────────────────────────────────────────────────────────────────

void Ui::showModal(Widget &owner, Widget &panel)
{
    if (std::none_of(modals_.begin(), modals_.end(), [&](const ModalEntry &m) { return m.panel == &panel; }))
    {
        modals_.push_back({&owner, &panel});
    }
}

void Ui::closePopupsOutsideModal()
{
    if (modals_.empty() || popups_.empty())
        return;
    // Une fenêtre est gardée si son propriétaire est dans la fenêtre modale active, ou dans une fenêtre gardée au-dessous d'elle
    // (sous-menu d'un menu de la modale). La pile se ferme à partir de la première qui ne l'est pas. Idempotent : appelé à chaque
    // mise en page, il ne ferme jamais celles de la modale.
    // Les propriétaires sont cherchés par adresse dans les sous-arbres vivants (modale, fenêtres gardées), sans les déréférencer :
    // un gestionnaire de l'image a pu en détruire un (la fenêtre est alors fermée ici).
    std::unordered_set<const Widget *> inside;
    collect(*modals_.back().panel, inside);
    for (std::size_t i = 0; i < popups_.size(); ++i)
    {
        if (inside.contains(popups_[i].owner))
            collect(*popups_[i].view, inside);
        else
        {
            // Pendant la mise en page : les actions différées (perte du focus, destruction) attendent l'image suivante, sans
            // exécuter de gestionnaire au milieu de la mise en page.
            closePopupsFrom(i, false);
            return;
        }
    }
}

void Ui::notify(std::string text, NotificationKind kind, float seconds)
{
    Notification n;
    n.id = nextNotification_++;
    n.text = std::move(text);
    n.kind = kind;
    n.duration = n.remaining = std::max(seconds, 0.1f);
    notifications_.push_back(std::move(n));
}

void Ui::dismissNotification(std::uint64_t id)
{
    std::erase_if(notifications_, [id](const Notification &n) { return n.id == id; });
}

void Ui::setClipboard(std::function<std::string()> get, std::function<void(const std::string &)> set)
{
    clipboardGet_ = std::move(get);
    clipboardSet_ = std::move(set);
}

std::string Ui::clipboardText() const { return clipboardGet_ ? clipboardGet_() : clipboard_; }

void Ui::setClipboardText(const std::string &text)
{
    clipboard_ = text;
    if (clipboardSet_)
        clipboardSet_(text);
}

// ── Image ────────────────────────────────────────────────────────────────────────────────────────────────────────────

void Ui::layout(glm::vec2 viewport)
{
    viewport_ = viewport;
    modals_.clear(); // les boîtes de dialogue ouvertes s'inscrivent pendant la mise en page
    if (root_)
    {
        // La racine est placée dans la vue comme l'enfant d'une superposition : étirée par défaut, ou à sa taille et alignée.
        const glm::vec2 m = root_->measure(*this, viewport);
        auto axis = [](Align align, const SizeSpec &spec, float avail, float measured) -> std::pair<float, float>
        {
            const float size = spec.mode == SizeMode::kFill || (align == Align::kStretch && spec.mode == SizeMode::kFit) ? spec.clamp(avail) : measured;
            if (align == Align::kCenter)
                return {(avail - size) * 0.5f, size};
            if (align == Align::kEnd)
                return {avail - size, size};
            return {0.0f, size};
        };
        auto [x, w] = axis(root_->alignX, root_->width, viewport.x - root_->margin.horizontal(), m.x);
        auto [y, h] = axis(root_->alignY, root_->height, viewport.y - root_->margin.vertical(), m.y);
        root_->layout(*this, {{x + root_->margin.left, y + root_->margin.top}, {w, h}});
    }
    // Parcours indexé : mesurer un dialogue peut en inscrire un autre (dialogue ouvert dans le panneau d'un dialogue), après lui.
    for (std::size_t i = 0; i < modals_.size(); ++i)
    {
        Widget &panel = *modals_[i].panel;
        const glm::vec2 m = glm::min(panel.measure(*this, viewport * 0.9f), viewport);
        panel.layout(*this, {glm::round((viewport - m) * 0.5f), m});
    }
    // Toutes les modales sont inscrites (une modale imbriquée s'inscrit pendant la mesure de la précédente) : la souris et le
    // clavier vont à la dernière, les fenêtres surgissantes ouvertes hors d'elle se ferment.
    closePopupsOutsideModal();
    layoutPopups();
}

void Ui::update(const InputFrame &input, glm::vec2 viewport)
{
    updating_ = true;
    time_ += input.deltaTime;
    (void)themes_.reloadIfChanged();

    // Pointeurs vers des widgets retirés de l'arbre depuis l'image précédente : oubliés (sans les déréférencer).
    dropDestroyed(); // détruits (même si un nouveau widget occupe la même adresse)
    {
        std::unordered_set<const Widget *> alive;
        if (root_)
            collect(*root_, alive);
        for (const PopupEntry &p : popups_)
            if (p.owned)
                collect(*p.owned, alive);
        // Fenêtre dont le propriétaire a disparu (menu d'un arbre remplacé) : fermée. Les widgets des fenêtres POSSÉDÉES qui vont
        // être détruites sortent d'abord de `alive` (les vues empruntées à un arbre disparu ne sont pas parcourues).
        for (std::size_t i = 0; i < popups_.size(); ++i)
        {
            if (!alive.contains(popups_[i].owner) || (!popups_[i].owned && !alive.contains(popups_[i].view)))
            {
                std::unordered_set<const Widget *> doomed;
                for (std::size_t j = i; j < popups_.size(); ++j)
                    if (popups_[j].owned)
                        collect(*popups_[j].owned, doomed);
                for (const Widget *w : doomed)
                    alive.erase(w);
                for (Widget **p : {&hovered_, &active_, &focused_})
                    if (*p != nullptr && !alive.contains(*p))
                        *p = nullptr;
                popups_.resize(i);
                break;
            }
        }
        for (Widget **p : {&hovered_, &active_, &focused_})
        {
            if (*p != nullptr && !alive.contains(*p))
            {
                *p = nullptr;
            }
        }
        std::erase_if(modals_, [&](const ModalEntry &m) { return !alive.contains(m.owner); });
    }
    // Le focus ne reste pas sur un widget devenu inactif ou masqué (lui ou une branche parente : page d'onglet cachée), ni hors
    // de la fenêtre modale ouverte (les touches ordinaires iraient sinon derrière le voile).
    if (focused_ != nullptr)
    {
        bool reachable = focused_->enabled();
        for (const Widget *w = focused_; reachable && w != nullptr; w = w->parent())
            reachable = w->visible();
        const auto inPopup = std::any_of(popups_.begin(), popups_.end(), [&](const PopupEntry &p) { return inSubtree(p.view, focused_); });
        if (reachable && !modals_.empty() && !inPopup && !inSubtree(modals_.back().panel, focused_))
            reachable = false;
        if (!reachable)
            focus(nullptr);
    }

    // Notifications : le temps passe ; celles qui ont fini disparaissent.
    for (Notification &n : notifications_)
        n.remaining -= input.deltaTime;
    std::erase_if(notifications_, [](const Notification &n) { return n.remaining <= 0.0f; });

    mouse_ = input.mouse;
    const bool pressed = input.mouseDown && !mouseWasDown_;
    const bool released = !input.mouseDown && mouseWasDown_;
    mouseWasDown_ = input.mouseDown;
    const bool secondaryPressed = input.secondaryDown && !secondaryWasDown_;
    secondaryWasDown_ = input.secondaryDown;
    shift_ = input.shift;

    Widget *hit = hitTest(mouse_);
    Widget *target = interactiveAncestor(hit);
    // Pendant un glissement, seul le widget appuyé peut être survolé (état « appuyé » = appuyé ET survolé).
    setHovered(active_ != nullptr ? (target == active_ ? active_ : nullptr) : target);

    // Infobulle : le premier parent (ou le widget) qui en a une, après un délai sans bouger de widget ni cliquer.
    const Widget *tipSource = nullptr;
    for (const Widget *p = hit; p != nullptr; p = p->parent())
    {
        if (!p->tooltip().empty())
        {
            tipSource = p;
            break;
        }
    }
    hoverTime_ = tipSource != nullptr && !pressed && !input.mouseDown && tipSource->tooltip() == tooltipCandidate_ ? hoverTime_ + input.deltaTime : 0.0f;
    tooltipCandidate_ = tipSource != nullptr ? tipSource->tooltip() : std::string();
    if (tipSource == nullptr || hoverTime_ < tooltipDelay_)
    {
        tooltipText_.clear();
    }
    else if (tooltipText_.empty())
    {
        tooltipText_ = tipSource->tooltip();
        tooltipAt_ = mouse_;
    }

    const auto inAnyPopup = [&](const Widget *w)
    { return std::any_of(popups_.begin(), popups_.end(), [&](const PopupEntry &p) { return inSubtree(p.view, w); }); };

    dropDestroyed(); // un gestionnaire (survol, rappel différé) a pu détruire le widget appuyé ou survolé
    if (pressed)
    {
        if (!popups_.empty() && !inAnyPopup(hit))
        {
            // Clic hors des fenêtres surgissantes : elles se ferment et le clic s'arrête là.
            closePopup();
            target = nullptr;
            setHovered(nullptr);
        }
        else if (!modals_.empty() && popups_.empty() && !inSubtree(modals_.back().panel, hit))
        {
            modals_.back().owner->onOutsideClick(*this); // clic sur le voile
            target = nullptr;
        }
        else
        {
            if (target == nullptr || target->focusOnClick())
                focus(target != nullptr && target->focusable() ? target : nullptr);
            active_ = target;
            activeAlive_ = target != nullptr ? std::weak_ptr<char>(target->lifetime_) : std::weak_ptr<char>();
            if (active_ != nullptr)
            {
                active_->pressed_ = true;
                active_->onPress(*this, mouse_);
            }
        }
    }
    else if (input.mouseDown && active_ != nullptr)
    {
        active_->onDrag(*this, mouse_);
    }
    dropDestroyed();
    if (released && active_ != nullptr)
    {
        Widget *a = active_;
        active_ = nullptr;
        a->pressed_ = false;
        a->onRelease(*this, mouse_, target == a && a->rect().contains(mouse_));
    }

    if (secondaryPressed && active_ == nullptr)
    {
        if (!popups_.empty() && !inAnyPopup(hit))
        {
            closePopup();
        }
        else if (target == nullptr || !target->onSecondaryClick(*this, mouse_))
        {
            // Menu contextuel : parmi les enfants du widget sous le pointeur, puis de ses parents.
            bool opened = false;
            for (Widget *p = hit; p != nullptr && !opened; p = p->parent())
            {
                for (const auto &child : p->children())
                {
                    if (child->openContextMenu(*this, mouse_))
                    {
                        opened = true;
                        break;
                    }
                }
            }
        }
    }

    if (input.wheel != glm::vec2(0.0f))
    {
        for (Widget *w = hit; w != nullptr; w = w->parent())
        {
            if (w->enabled() && w->onWheel(*this, input.wheel))
            {
                break;
            }
        }
    }

    const auto enterPopup = [&](bool first)
    {
        std::vector<Widget *> items;
        collectFocusable(*popups_.back().view, items);
        if (items.empty())
            return false;
        focus(first ? items.front() : items.back());
        return true;
    };
    auto dispatchKey = [&](int key, bool shift)
    {
        shift_ = shift; // Maj au moment de CET appui (les widgets la lisent par shiftHeld)
        if (key == kKeyTab && !(focused_ != nullptr && focused_->wantsTab()))
        {
            focusNext(shift);
        }
        else if ((key == kKeyDown || key == kKeyUp) && !popups_.empty() && !(focused_ != nullptr && inAnyPopup(focused_)) && enterPopup(key == kKeyDown))
        {
            // Menu ouvert à la souris : les flèches entrent dans la fenêtre du dessus (premier ou dernier élément).
        }
        else if (key == kKeyEscape && !popups_.empty() && !(focused_ != nullptr && inAnyPopup(focused_)))
        {
            closePopupsFrom(popups_.size() - 1); // la fenêtre du dessus
        }
        else if (focused_ != nullptr)
        {
            // Échap : un champ de saisie le prend (perte du focus) ; sinon il remonte à la fenêtre modale ouverte.
            Widget *before = focused_;
            focused_->onKey(*this, key);
            if (key == kKeyEscape && focused_ == before && !modals_.empty())
                modals_.back().owner->onKey(*this, kKeyEscape);
        }
        else if (key == kKeyEscape && !modals_.empty())
        {
            modals_.back().owner->onKey(*this, kKeyEscape);
        }
    };
    if (!input.events.empty())
    {
        // Ordre réel de saisie : « a » puis Retour arrière n'est pas Retour arrière puis « a ».
        for (const InputFrame::Event &e : input.events)
        {
            if (e.key != kKeyNone)
                dispatchKey(e.key, e.shift);
            else if (!e.text.empty() && focused_ != nullptr)
                focused_->onText(*this, e.text);
        }
    }
    else
    {
        for (int key : input.keys)
            dispatchKey(key, input.shift);
        if (!input.text.empty() && focused_ != nullptr)
            focused_->onText(*this, input.text);
    }
    shift_ = input.shift;
    for (const std::string &raw : input.shortcuts)
    {
        const std::string s = normalizeShortcut(raw);
        if (s.empty())
            continue;
        // Champ de texte : commandes d'édition (Super sur macOS comme Ctrl ailleurs).
        if (focused_ != nullptr && focused_->handlesEditCommands())
        {
            static const std::pair<const char *, int> kEdit[] = {
                {"Ctrl+A", kKeySelectAll}, {"Ctrl+C", kKeyCopy}, {"Ctrl+X", kKeyCut},        {"Ctrl+V", kKeyPaste},      {"Ctrl+Z", kKeyUndo},
                {"Ctrl+Y", kKeyRedo},      {"Ctrl+Shift+Z", kKeyRedo}, {"Super+A", kKeySelectAll}, {"Super+C", kKeyCopy}, {"Super+X", kKeyCut},
                {"Super+V", kKeyPaste},    {"Super+Z", kKeyUndo},  {"Super+Shift+Z", kKeyRedo},
            };
            const auto edit = std::find_if(std::begin(kEdit), std::end(kEdit), [&](const auto &e) { return s == e.first; });
            if (edit != std::end(kEdit))
            {
                focused_->onKey(*this, edit->second);
                continue;
            }
        }
        (void)dispatchShortcut(s);
    }

    wantsMouse_ = active_ != nullptr || !modals_.empty() || (hit != nullptr && (blocksMouse(*this, hit) || inAnyPopup(hit)));

    flushDeferred();
    updating_ = false;
    if (hasPendingRoot_)
    {
        hasPendingRoot_ = false;
        setRoot(std::move(pendingRoot_));
    }
    layout(viewport);
}

void Ui::draw(renderer::DrawList &list) const
{
    list.logicalSize = viewport_;
    if (root_)
    {
        root_->draw(*this, list);
    }
    for (const ModalEntry &modal : modals_)
    {
        const Style &veil = theme().style("modalBackdrop", WidgetState::kNormal);
        if (veil.background.a > 0.0f)
        {
            list.rect({0.0f, 0.0f}, viewport_, veil.background);
        }
        modal.panel->draw(*this, list);
    }
    for (const PopupEntry &p : popups_)
    {
        p.view->draw(*this, list);
    }
    if (!tooltipText_.empty())
    {
        const Style &s = theme().style("tooltip", WidgetState::kNormal);
        const glm::vec2 text = font().measure(tooltipText_, s.fontSize, s.font.get());
        const glm::vec2 size = text + glm::vec2(2.0f * s.padding);
        glm::vec2 at = tooltipAt_ + glm::vec2(12.0f, 18.0f); // sous le pointeur, décalé de sa taille habituelle
        at = glm::clamp(at, glm::vec2(0.0f), glm::max(viewport_ - size, glm::vec2(0.0f)));
        Widget::drawBox(list, {at, size}, s);
        font().draw(list, tooltipText_, at + glm::vec2(s.padding), s.fontSize, s.foreground, s.font.get());
    }
}

} // namespace orosmo::ui
