#include "engine/ui/menus.hpp"

#include "engine/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::ui
{

namespace
{
float lineHeight(const Ui &ui, const Style &style) { return ui.font().measure("Hg", style.fontSize, style.font.get()).y; }
} // namespace

// ── Dialog ───────────────────────────────────────────────────────────────────────────────────────────────────────────

Dialog::Dialog(std::string title, Binding<bool> open, bool dismissOnOutsideClick)
    : Widget("box"), open_(std::move(open)), dismiss_(dismissOnOutsideClick), panel_(std::make_unique<VBox>("dialog"))
{
    auto &bar = panel_->emplace<HBox>();
    bar.emplace<Label>(std::move(title), "dialogTitle").setWidth(SizeSpec::fill());
    auto &close = bar.emplace<Button>("x", "dialogClose");
    close.setTooltip("Close (Escape)");
    close.onClick([this](const Event &) {
        if (ui_ != nullptr)
            this->close(*ui_);
    });
    panel_->emplace<Separator>();
    body_ = &panel_->emplace<VBox>();
}

void Dialog::close(Ui &ui)
{
    if (!isOpen() || !open_.writable())
        return;
    ui.defer([set = open_.set] { set(false); });
    emit(ui, EventKind::kChange);
}

void Dialog::onKey(Ui &ui, int key)
{
    if (key == kKeyEscape)
        close(ui);
}

void Dialog::onOutsideClick(Ui &ui)
{
    if (dismiss_)
        close(ui);
}

glm::vec2 Dialog::measureContent(Ui &ui, glm::vec2, const Style &)
{
    ui_ = &ui;
    if (isOpen())
        ui.showModal(*this, *panel_);
    return {0.0f, 0.0f};
}

// ── MenuItem, Menu, ContextMenu ──────────────────────────────────────────────────────────────────────────────────────

MenuItem::MenuItem(std::string text, std::string shortcut, Binding<bool> checked)
    : Widget("menuItem"), text_(std::move(text)), shortcut_(std::move(shortcut)), checked_(std::move(checked))
{
}

void MenuItem::activate(Ui &ui)
{
    if (checked_.writable())
        ui.defer([set = checked_.set, v = !checked_.value()] { set(v); });
    emit(ui, EventKind::kClick);
    ui.closePopup();
}

void MenuItem::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (inside)
        activate(ui);
}

bool MenuItem::onShortcut(Ui &ui, std::string_view shortcut)
{
    if (shortcut_.empty() || normalizeShortcut(shortcut_) != shortcut)
        return false;
    activate(ui);
    return true;
}

namespace
{
// Navigation au clavier dans une fenêtre de menu (éléments et sous-menus).
std::vector<Widget *> menuEntries(const Widget &list)
{
    std::vector<Widget *> out;
    for (const auto &c : list.children())
        if (c->visible() && c->enabled() && c->focusable())
            out.push_back(c.get());
    return out;
}

void focusSibling(Ui &ui, Widget &self, int direction)
{
    if (self.parent() == nullptr)
        return;
    const auto entries = menuEntries(*self.parent());
    const auto it = std::find(entries.begin(), entries.end(), &self);
    if (it == entries.end())
        return;
    const auto n = static_cast<std::ptrdiff_t>(entries.size());
    const std::ptrdiff_t i = ((it - entries.begin()) + direction + n) % n;
    ui.focus(entries[static_cast<std::size_t>(i)]);
}

// Menu de la barre qui contient (par ses sous-menus) la fenêtre où se trouve `w` ; nul hors d'une barre.
Menu *barMenuOf(const Ui &ui, const Widget &w)
{
    const Widget *at = &w;
    while (Widget *owner = ui.popupOwnerOf(*at))
    {
        if (auto *menu = dynamic_cast<Menu *>(owner); menu != nullptr && menu->inBar())
            return menu;
        at = owner;
    }
    return nullptr;
}

// Menu voisin dans la barre (direction ±1), ouvert avec le focus sur son premier élément.
void switchBarMenu(Ui &ui, Menu &current, int direction, bool openIt)
{
    std::vector<Menu *> menus;
    for (const auto &c : current.parent()->children())
        if (auto *m = dynamic_cast<Menu *>(c.get()); m != nullptr && m->visible() && m->enabled())
            menus.push_back(m);
    const auto it = std::find(menus.begin(), menus.end(), &current);
    if (it == menus.end() || menus.size() < 2)
        return;
    const auto n = static_cast<std::ptrdiff_t>(menus.size());
    Menu *next = menus[static_cast<std::size_t>(((it - menus.begin()) + direction + n) % n)];
    if (openIt)
        next->open(ui, true);
    else
        ui.focus(next);
}

// Touches communes aux entrées d'une fenêtre de menu ; vrai si la touche est prise.
bool menuEntryKey(Ui &ui, Widget &self, int key)
{
    switch (key)
    {
    case kKeyUp: focusSibling(ui, self, -1); return true;
    case kKeyDown: focusSibling(ui, self, 1); return true;
    case kKeyLeft:
    case kKeyEscape:
    {
        Widget *owner = ui.popupOwnerOf(self);
        if (owner == nullptr)
            return false;
        auto *menu = dynamic_cast<Menu *>(owner);
        if (key == kKeyLeft && menu != nullptr && menu->inBar())
        {
            switchBarMenu(ui, *menu, -1, true);
            return true;
        }
        if (key == kKeyLeft && menu == nullptr)
            return true; // menu contextuel : rien à gauche
        ui.closePopupOf(*owner);
        ui.focus(owner->focusable() ? owner : nullptr);
        return true;
    }
    case kKeyRight:
        if (Menu *bar = barMenuOf(ui, self))
            switchBarMenu(ui, *bar, 1, true);
        return true;
    default: return false;
    }
}
} // namespace

void MenuItem::onKey(Ui &ui, int key)
{
    if (key == kKeyEnter || key == kKeySpace)
        activate(ui);
    else
        (void)menuEntryKey(ui, *this, key);
}

glm::vec2 MenuItem::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const float check = style.width + style.spacing; // colonne de la coche, même vide : les textes s'alignent
    float w = check + ui.font().measure(text_, style.fontSize, style.font.get()).x;
    if (!shortcut_.empty())
        w += 3.0f * style.spacing + ui.font().measure(shortcut_, style.fontSize, style.font.get()).x;
    return {w, lineHeight(ui, style)};
}

void MenuItem::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect c = inset(rect_, style.padding);
    if (checked_.get && checked_.value())
    {
        const float d = style.width * 0.5f;
        list.rect(renderer::DrawRect{.position = {c.position.x + d * 0.5f, c.position.y + (c.size.y - d) * 0.5f}, .size = glm::vec2(d), .color = style.accent,
                                     .radius = d * 0.25f});
    }
    const float text = style.width + style.spacing;
    drawText(ui, list, text_, {{c.position.x + text, c.position.y}, {c.size.x - text, c.size.y}}, style, Align::kStart);
    if (!shortcut_.empty())
    {
        Color muted = style.foreground;
        muted.a *= 0.6f;
        drawText(ui, list, shortcut_, c, style, Align::kEnd, &muted);
    }
}

Menu::Menu(std::string title) : Widget("menu"), title_(std::move(title)), items_(std::make_unique<VBox>("popup")) {}

bool Menu::inBar() const { return dynamic_cast<const MenuBar *>(parent()) != nullptr; }

void Menu::open(Ui &ui, bool focusFirst)
{
    if (items_->children().empty())
        return;
    ui.openPopup(*this, *items_, rect_, inBar() ? PopupPlacement::kBelow : PopupPlacement::kRight);
    if (focusFirst)
    {
        const auto entries = menuEntries(*items_);
        if (!entries.empty())
            ui.focus(entries.front());
    }
}

void Menu::onKey(Ui &ui, int key)
{
    if (inBar())
    {
        switch (key)
        {
        case kKeyDown:
        case kKeyEnter:
        case kKeySpace: open(ui, true); break;
        case kKeyLeft: switchBarMenu(ui, *this, -1, ui.popupOpenFor(*this)); break;
        case kKeyRight: switchBarMenu(ui, *this, 1, ui.popupOpenFor(*this)); break;
        case kKeyEscape:
            if (ui.popupOpenFor(*this))
                ui.closePopupOf(*this);
            break;
        default: break;
        }
        return;
    }
    // Sous-menu (entrée d'une fenêtre de menu).
    if (key == kKeyRight || key == kKeyEnter || key == kKeySpace)
        open(ui, true);
    else
        (void)menuEntryKey(ui, *this, key);
}

void Menu::onPress(Ui &ui, glm::vec2)
{
    if (ui.popupOpenFor(*this) && inBar())
        ui.closePopup(); // second clic sur le titre : le menu se ferme
    else
        open(ui);
}

void Menu::onHoverChanged(Ui &ui, bool hovered)
{
    if (!hovered || ui.popupOpenFor(*this))
        return;
    // Sous-menu : s'ouvre au survol. Barre de menus : quand un menu de la barre est ouvert, survoler un autre titre passe à lui.
    if (!inBar())
    {
        open(ui);
        return;
    }
    for (const auto &sibling : parent()->children())
    {
        if (sibling.get() != this && ui.popupOpenFor(*sibling))
        {
            open(ui);
            return;
        }
    }
}

glm::vec2 Menu::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const float arrow = inBar() ? 0.0f : 2.0f * style.spacing + ui.font().measure(">", style.fontSize, style.font.get()).x;
    return {ui.font().measure(title_, style.fontSize, style.font.get()).x + arrow + (inBar() ? 0.0f : style.width + style.spacing), lineHeight(ui, style)};
}

void Menu::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect c = inset(rect_, style.padding);
    if (inBar())
    {
        drawText(ui, list, title_, c, style, Align::kCenter);
        return;
    }
    const float text = style.width + style.spacing; // aligné sur les MenuItem
    drawText(ui, list, title_, {{c.position.x + text, c.position.y}, {c.size.x - text, c.size.y}}, style, Align::kStart);
    drawText(ui, list, ">", c, style, Align::kEnd);
}

ContextMenu::ContextMenu() : Widget("box"), items_(std::make_unique<VBox>("popup")) {}

bool ContextMenu::openContextMenu(Ui &ui, glm::vec2 mouse)
{
    if (items_->children().empty())
        return false;
    ui.openPopup(*this, *items_, {mouse, {0.0f, 0.0f}}, PopupPlacement::kAtPoint);
    return true;
}

// ── Notifications ────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
// Barre de couleur à gauche d'une notification : même largeur à la mesure et au dessin (style "toast" de base).
float toastBar(const Style &toast) { return std::max(3.0f, toast.width * 0.25f); }

const char *toastStyle(NotificationKind kind)
{
    switch (kind)
    {
    case NotificationKind::kSuccess: return "toastSuccess";
    case NotificationKind::kWarning: return "toastWarning";
    case NotificationKind::kError: return "toastError";
    default: return "toast";
    }
}
} // namespace

Notifications::Notifications(int maxCount, float maxHeight, float width)
    : Widget("box"), maxCount_(std::max(maxCount, 1)), maxHeight_(std::max(maxHeight, 0.0f)), width_(width)
{
}

glm::vec2 Notifications::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    auto &queue = ui.notifications();
    // Trop de notifications : les plus anciennes partent (par nombre, puis par hauteur).
    while (static_cast<int>(queue.size()) > maxCount_)
        queue.pop_front();
    const Style &toast = ui.theme().style("toast", WidgetState::kNormal);
    const float one = lineHeight(ui, toast) + 2.0f * toast.padding;
    auto heightOf = [&](std::size_t n) { return n == 0 ? 0.0f : static_cast<float>(n) * one + static_cast<float>(n - 1) * style.spacing; };
    while (maxHeight_ > 0.0f && !queue.empty() && heightOf(queue.size()) > maxHeight_)
        queue.pop_front();
    float widest = 0.0f;
    for (const Notification &n : queue)
        widest = std::max(widest, ui.font().measure(n.text, toast.fontSize, toast.font.get()).x);
    const float w = width_ > 0.0f ? width_ : std::min(widest + 2.0f * toast.padding + toastBar(toast), available.x);
    return {queue.empty() ? 0.0f : w, heightOf(queue.size())};
}

void Notifications::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    placed_.clear();
    const Rect c = inset(rect_, style.padding);
    const Style &base = ui.theme().style("toast", WidgetState::kNormal);
    const float one = lineHeight(ui, base) + 2.0f * base.padding;
    float y = c.position.y;
    for (const Notification &n : ui.notifications())
    {
        Style s = ui.theme().style(toastStyle(n.kind), WidgetState::kNormal);
        // Apparition et disparition progressives (0,15 s et 0,3 s).
        const float fade = std::clamp(std::min(n.remaining / 0.3f, (n.duration - n.remaining) / 0.15f), 0.0f, 1.0f);
        for (Color *col : {&s.background, &s.foreground, &s.border, &s.accent, &s.shadowColor})
            col->a *= fade;
        const Rect r{{c.position.x, y}, {c.size.x, one}};
        drawBox(list, r, s);
        const float bar = toastBar(base);
        list.rect(renderer::DrawRect{.position = r.position, .size = {bar, r.size.y}, .color = s.accent, .radius = std::min(s.radius, bar * 0.5f)});
        drawText(ui, list, n.text, {{r.position.x + bar + s.padding, r.position.y}, {r.size.x - bar - 2.0f * s.padding, r.size.y}}, s, Align::kStart);
        placed_.push_back({n.id, r});
        y += one + style.spacing;
    }
}

void Notifications::onRelease(Ui &ui, glm::vec2 mouse, bool inside)
{
    if (!inside)
        return;
    for (const Placed &p : placed_)
        if (p.rect.contains(mouse))
            ui.dismissNotification(p.id); // un clic ferme la notification
}

// ── DynamicImage, ImageView ──────────────────────────────────────────────────────────────────────────────────────────

DynamicImage::DynamicImage(std::uint32_t width, std::uint32_t height) { resize(width, height); }

void DynamicImage::resize(std::uint32_t width, std::uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    // Octets RGBA8 calculés sans débordement (2^31 x 2^31 x 4 reviendrait à 0) ; une taille impossible garde l'image précédente.
    if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4 / height)
    {
        if (!image_)
            resize(1, 1);
        return;
    }
    auto image = std::make_shared<renderer::AtlasImage>();
    image->id = renderer::newAtlasId();
    image->width = width;
    image->height = height;
    image->rgba.assign(static_cast<std::size_t>(image->width) * image->height * 4, 0);
    image_ = std::move(image);
}

void DynamicImage::setPixel(std::uint32_t x, std::uint32_t y, const Color &c) noexcept
{
    if (x >= image_->width || y >= image_->height)
        return;
    std::uint8_t *p = &image_->rgba[(static_cast<std::size_t>(y) * image_->width + x) * 4];
    for (int k = 0; k < 4; ++k)
        p[k] = static_cast<std::uint8_t>(std::lround(std::clamp(c[k], 0.0f, 1.0f) * 255.0f));
}

void DynamicImage::fill(const Color &c) noexcept
{
    for (std::uint32_t y = 0; y < image_->height; ++y)
        for (std::uint32_t x = 0; x < image_->width; ++x)
            setPixel(x, y, c);
}

ImageView::ImageView(std::function<std::shared_ptr<const renderer::AtlasImage>()> source, ImageFit fit)
    : Widget("image"), source_(std::move(source)), fit_(fit)
{
}

glm::vec2 ImageView::measureContent(Ui &, glm::vec2, const Style &style)
{
    const auto image = source_ ? source_() : nullptr;
    if (!image)
        return glm::vec2(style.width * 6.0f);
    return {static_cast<float>(image->width), static_cast<float>(image->height)};
}

void ImageView::drawContent(const Ui &, renderer::DrawList &list, const Style &style) const
{
    const auto image = source_ ? source_() : nullptr;
    const Rect c = inset(rect_, style.padding);
    if (!image || image->width == 0 || image->height == 0 || c.size.x <= 0.0f || c.size.y <= 0.0f)
        return;
    const float imageAspect = static_cast<float>(image->width) / static_cast<float>(image->height);
    const float boxAspect = c.size.x / c.size.y;
    glm::vec2 pos = c.position, size = c.size, uv0{0.0f}, uv1{1.0f};
    if (fit_ == ImageFit::kContain)
    {
        size = imageAspect > boxAspect ? glm::vec2(c.size.x, c.size.x / imageAspect) : glm::vec2(c.size.y * imageAspect, c.size.y);
        pos = c.position + (c.size - size) * 0.5f;
    }
    else if (fit_ == ImageFit::kCover)
    {
        // Partie centrale de l'image, aux proportions du widget.
        const glm::vec2 keep = imageAspect > boxAspect ? glm::vec2(boxAspect / imageAspect, 1.0f) : glm::vec2(1.0f, imageAspect / boxAspect);
        uv0 = (glm::vec2(1.0f) - keep) * 0.5f;
        uv1 = uv0 + keep;
    }
    const glm::vec2 p1 = pos + size;
    const float q[] = {pos.x, pos.y, uv0.x, uv0.y, pos.x, p1.y, uv0.x, uv1.y, p1.x, p1.y, uv1.x, uv1.y,
                       pos.x, pos.y, uv0.x, uv0.y, p1.x, p1.y, uv1.x, uv1.y, p1.x, pos.y, uv1.x, uv0.y};
    list.image(renderer::DrawImage{.image = image, .vertices = std::vector<float>(std::begin(q), std::end(q)), .tint = tint_});
}

} // namespace engine::ui
