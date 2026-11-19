#include "engine/ui/ui.hpp"

#include "Logger.h"

#include <algorithm>
#include <unordered_set>

namespace engine::ui
{

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
        return;
    }
    closePopup();
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
    return popup_ ? popup_->find(id) : nullptr;
}

bool Ui::wantsKeyboard() const noexcept { return focused_ != nullptr && focused_->acceptsText(); }

Widget *Ui::hitTestIn(Widget &w, glm::vec2 p)
{
    if (!w.visible())
    {
        return nullptr;
    }
    // Un enfant qui dépasse de son parent reste cliquable là où il est dessiné, sauf si le parent coupe son contenu.
    const bool inside = w.rect().contains(p);
    if (!inside && (w.clipsChildren() || w.overflow == Overflow::kHidden))
    {
        return nullptr;
    }
    const auto children = w.children();
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        if (Widget *hit = hitTestIn(**it, p))
        {
            return hit;
        }
    }
    return inside ? &w : nullptr;
}

Widget *Ui::hitTest(glm::vec2 p) const
{
    if (popup_)
    {
        if (Widget *hit = hitTestIn(*popup_, p))
        {
            return hit;
        }
    }
    return root_ ? hitTestIn(*root_, p) : nullptr;
}

void Ui::setHovered(Widget *target)
{
    if (target == hovered_)
    {
        return;
    }
    if (hovered_ != nullptr)
    {
        hovered_->hovered_ = false;
        hovered_->emit(*this, EventKind::kHover, false);
    }
    hovered_ = target;
    if (hovered_ != nullptr)
    {
        hovered_->hovered_ = true;
        hovered_->emit(*this, EventKind::kHover, true);
    }
}

void Ui::focus(Widget *widget)
{
    if (widget == focused_)
    {
        return;
    }
    Widget *old = focused_;
    focused_ = widget;
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
    if (root_)
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
    // Une action peut en programmer d'autres (un changement qui en déclenche un autre) : on vide jusqu'à stabilité, borné.
    for (int round = 0; round < 8 && !deferred_.empty(); ++round)
    {
        std::vector<std::function<void()>> batch;
        batch.swap(deferred_);
        for (auto &action : batch)
        {
            action();
        }
    }
    deferred_.clear();
}

void Ui::openPopup(Widget &owner, std::unique_ptr<Widget> content, const Rect &anchor)
{
    closePopup();
    popup_ = std::move(content);
    popupOwner_ = &owner;
    popupAnchor_ = anchor;
    layoutPopup();
}

void Ui::closePopup()
{
    if (!popup_)
    {
        return;
    }
    if (inSubtree(popup_.get(), hovered_))
    {
        hovered_ = nullptr;
    }
    if (inSubtree(popup_.get(), active_))
    {
        active_ = nullptr;
    }
    if (inSubtree(popup_.get(), focused_))
    {
        focused_ = nullptr;
    }
    // Détruit après l'image : un événement en attente peut encore viser un widget de la fenêtre.
    Widget *raw = popup_.release();
    popupOwner_ = nullptr;
    defer([raw] { delete raw; });
    if (!updating_)
    {
        flushDeferred();
    }
}

void Ui::forget(const Widget &widget) noexcept
{
    if (hovered_ == &widget)
        hovered_ = nullptr;
    if (active_ == &widget)
        active_ = nullptr;
    if (focused_ == &widget)
        focused_ = nullptr;
}

void Ui::layoutPopup()
{
    if (!popup_)
    {
        return;
    }
    const glm::vec2 m = popup_->measure(*this, viewport_);
    const float w = std::min(std::max(m.x, popupAnchor_.size.x), viewport_.x);
    const float h = std::min(m.y, viewport_.y);
    float y = popupAnchor_.end().y;
    if (y + h > viewport_.y && popupAnchor_.position.y - h >= 0.0f)
    {
        y = popupAnchor_.position.y - h;
    }
    const float x = std::clamp(popupAnchor_.position.x, 0.0f, std::max(0.0f, viewport_.x - w));
    popup_->layout(*this, {{x, y}, {w, h}});
}

void Ui::layout(glm::vec2 viewport)
{
    viewport_ = viewport;
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
    layoutPopup();
}

void Ui::update(const InputFrame &input, glm::vec2 viewport)
{
    updating_ = true;
    time_ += input.deltaTime;
    (void)themes_.reloadIfChanged();

    // Pointeurs vers des widgets retirés de l'arbre depuis l'image précédente : oubliés (sans les déréférencer).
    {
        std::unordered_set<const Widget *> alive;
        if (root_)
            collect(*root_, alive);
        if (popup_)
            collect(*popup_, alive);
        for (Widget **p : {&hovered_, &active_, &focused_})
        {
            if (*p != nullptr && !alive.contains(*p))
            {
                *p = nullptr;
            }
        }
    }

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

    if (pressed)
    {
        if (popup_ && !inSubtree(popup_.get(), hit))
        {
            // Clic hors de la fenêtre surgissante : elle se ferme et le clic s'arrête là.
            closePopup();
            target = nullptr;
            setHovered(nullptr);
        }
        else
        {
            focus(target != nullptr && target->focusable() ? target : nullptr);
            active_ = target;
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
    if (released && active_ != nullptr)
    {
        Widget *a = active_;
        active_ = nullptr;
        a->pressed_ = false;
        a->onRelease(*this, mouse_, target == a && a->rect().contains(mouse_));
    }

    if (secondaryPressed && active_ == nullptr)
    {
        if (popup_ && !inSubtree(popup_.get(), hit))
            closePopup();
        else if (target != nullptr)
            target->onSecondaryClick(*this, mouse_);
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

    for (int key : input.keys)
    {
        if (key == kKeyTab)
        {
            focusNext(input.shift);
        }
        else if (focused_ != nullptr)
        {
            focused_->onKey(*this, key);
        }
        else if (key == kKeyEscape)
        {
            closePopup();
        }
    }
    if (!input.text.empty() && focused_ != nullptr)
    {
        focused_->onText(*this, input.text);
    }

    wantsMouse_ = active_ != nullptr || (hit != nullptr && (blocksMouse(*this, hit) || inSubtree(popup_.get(), hit)));

    flushDeferred();
    updating_ = false;
    if (pendingRoot_)
    {
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
    if (popup_)
    {
        popup_->draw(*this, list);
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

} // namespace engine::ui
