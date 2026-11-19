#include "engine/ui/widgets.hpp"

#include "engine/ui/ui.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace engine::ui
{

namespace
{
float lineHeight(const Ui &ui, const Style &style) { return ui.font().measure("Hg", style.fontSize, style.font.get()).y; }

std::string formatNumber(double v, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, v);
    std::string out = buffer;
    if (out == "-0" || (out.starts_with("-0.") && out.find_first_not_of("-0.") == std::string::npos))
    {
        out.erase(0, 1);
    }
    return out;
}
} // namespace

// ── Label ────────────────────────────────────────────────────────────────────────────────────────────────────────────

Label::Label(std::string text, std::string styleName) : Widget(std::move(styleName)), text_(std::move(text)) {}

Label &Label::setText(std::string text)
{
    text_ = std::move(text);
    source_ = nullptr;
    return *this;
}

Label &Label::bindText(std::function<std::string()> text)
{
    source_ = std::move(text);
    return *this;
}

std::string Label::text() const { return source_ ? source_() : text_; }

glm::vec2 Label::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const std::string t = text();
    return {ui.font().measure(t, style.fontSize, style.font.get()).x, lineHeight(ui, style)};
}

void Label::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    drawText(ui, list, text(), inset(rect_, style.padding), style, textAlign_);
    drawChildren(ui, list);
}

// ── Button ───────────────────────────────────────────────────────────────────────────────────────────────────────────

Button::Button(std::string text, std::string styleName) : Widget(std::move(styleName)), text_(std::move(text)) {}

void Button::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (inside)
    {
        emit(ui, EventKind::kClick);
    }
}

void Button::onKey(Ui &ui, int key)
{
    if (key == kKeyEnter || key == kKeySpace)
    {
        emit(ui, EventKind::kClick);
    }
}

Button &Button::setShortcut(std::string shortcut)
{
    shortcut_ = normalizeShortcut(shortcut);
    return *this;
}

bool Button::onShortcut(Ui &ui, std::string_view shortcut)
{
    if (shortcut_.empty() || shortcut_ != shortcut)
        return false;
    emit(ui, EventKind::kClick);
    return true;
}

glm::vec2 Button::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    return {ui.font().measure(text(), style.fontSize, style.font.get()).x, lineHeight(ui, style)};
}

void Button::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    drawText(ui, list, text(), inset(rect_, style.padding), style, Align::kCenter);
}

// ── Checkbox ─────────────────────────────────────────────────────────────────────────────────────────────────────────

Checkbox::Checkbox(std::string text, Binding<bool> value) : Widget("checkbox"), text_(std::move(text)), value_(std::move(value)) {}

void Checkbox::toggle(Ui &ui)
{
    if (!value_.writable())
    {
        return;
    }
    const bool next = !checked();
    ui.defer([set = value_.set, next] { set(next); });
    emit(ui, EventKind::kChange);
}

void Checkbox::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (inside)
    {
        toggle(ui);
        emit(ui, EventKind::kClick);
    }
}

void Checkbox::onKey(Ui &ui, int key)
{
    if (key == kKeyEnter || key == kKeySpace)
    {
        toggle(ui);
    }
}

glm::vec2 Checkbox::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const glm::vec2 text = ui.font().measure(text_, style.fontSize, style.font.get());
    const float box = style.width;
    return {box + (text_.empty() ? 0.0f : style.spacing + text.x), std::max(box, lineHeight(ui, style))};
}

void Checkbox::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const float box = style.width;
    const Rect boxRect{{content.position.x, content.position.y + (content.size.y - box) * 0.5f}, {box, box}};
    drawBox(list, boxRect, style);
    if (checked())
    {
        const float m = std::round(box * 0.25f);
        list.rect(renderer::DrawRect{.position = boxRect.position + glm::vec2(m), .size = boxRect.size - glm::vec2(2.0f * m), .color = style.accent,
                                     .radius = style.radius * 0.5f});
    }
    if (!text_.empty())
    {
        const Rect textRect{{boxRect.end().x + style.spacing, content.position.y}, {content.end().x - boxRect.end().x - style.spacing, content.size.y}};
        drawText(ui, list, text_, textRect, style, Align::kStart);
    }
}

// ── Slider ───────────────────────────────────────────────────────────────────────────────────────────────────────────

Slider::Slider(Binding<double> value, double min, double max, double step)
    : Widget("slider"), value_(std::move(value)), min_(std::min(min, max)), max_(std::max(min, max)), step_(std::max(step, 0.0))
{
    default_ = value_.get ? quantize(value_.value()) : min_;
}

double Slider::value() const { return hasPending_ ? pending_ : value_.value(); }

double Slider::quantize(double v) const
{
    v = std::clamp(v, min_, max_);
    if (step_ > 0.0)
    {
        v = min_ + std::round((v - min_) / step_) * step_;
        v = std::clamp(v, min_, max_);
    }
    return v;
}

std::string Slider::formatted() const
{
    if (step_ <= 0.0)
    {
        // Continu : quatre chiffres significatifs (« 260 », « 2.1 », « 0.0011 »).
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.4g", value());
        return prefix_ + buffer;
    }
    const int decimals = std::abs(step_ - std::round(step_)) < 1e-9 ? 0 : std::clamp(static_cast<int>(std::ceil(-std::log10(step_) - 1e-9)), 0, 6);
    return prefix_ + formatNumber(value(), decimals);
}

void Slider::write(Ui &ui, double v)
{
    const double q = quantize(v);
    if (!value_.writable() || q == value())
    {
        return;
    }
    pending_ = q;
    hasPending_ = true;
    ui.defer([this, q] {
        value_.set(q);
        hasPending_ = false;
    });
    emit(ui, EventKind::kChange);
}

void Slider::setFromMouse(Ui &ui, float x)
{
    const Style s = style(ui.theme());
    const Rect track = inset(rect_, s.padding);
    if (track.size.x <= 0.0f)
    {
        return;
    }
    const double t = std::clamp(static_cast<double>((x - track.position.x) / track.size.x), 0.0, 1.0);
    write(ui, min_ + t * (max_ - min_));
}

void Slider::onPress(Ui &, glm::vec2 mouse)
{
    dragging_ = false;
    pressX_ = lastX_ = mouse.x;
    dragValue_ = value();
}

void Slider::onDrag(Ui &ui, glm::vec2 mouse)
{
    if (!dragging_)
    {
        if (std::abs(mouse.x - pressX_) <= kDragThreshold)
        {
            return;
        }
        dragging_ = true;
    }
    // Déplacement relatif depuis la valeur courante : la largeur de la piste vaut toute la plage (dix fois moins avec Maj).
    const Style s = style(ui.theme());
    const float width = std::max(inset(rect_, s.padding).size.x, 1.0f);
    const double perPixel = (max_ - min_) / static_cast<double>(width) * (ui.shiftHeld() ? 0.1 : 1.0);
    dragValue_ = std::clamp(dragValue_ + static_cast<double>(mouse.x - lastX_) * perPixel, min_, max_);
    lastX_ = mouse.x;
    write(ui, dragValue_);
}

void Slider::onRelease(Ui &ui, glm::vec2 mouse, bool inside)
{
    if (!dragging_ && inside)
    {
        setFromMouse(ui, mouse.x); // simple clic : la valeur sous le pointeur
    }
    dragging_ = false;
}

bool Slider::onSecondaryClick(Ui &ui, glm::vec2)
{
    write(ui, default_);
    return true;
}

void Slider::onKey(Ui &ui, int key)
{
    const double step = step_ > 0.0 ? step_ : (max_ - min_) / 100.0;
    if (key == kKeyLeft || key == kKeyDown)
        write(ui, value() - step);
    else if (key == kKeyRight || key == kKeyUp)
        write(ui, value() + step);
    else if (key == kKeyHome)
        write(ui, min_);
    else if (key == kKeyEnd)
        write(ui, max_);
}

glm::vec2 Slider::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const float a = ui.font().measure(prefix_ + formatNumber(min_, 3), style.fontSize, style.font.get()).x;
    const float b = ui.font().measure(prefix_ + formatNumber(max_, 3), style.fontSize, style.font.get()).x;
    return {std::max(a, b) + 2.0f * style.width, std::max(style.width, lineHeight(ui, style))};
}

void Slider::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect track = rect_;
    const double range = max_ - min_;
    const float t = range > 0.0 ? static_cast<float>((value() - min_) / range) : 0.0f;
    if (t > 0.0f)
    {
        list.rect(renderer::DrawRect{.position = track.position, .size = {track.size.x * std::clamp(t, 0.0f, 1.0f), track.size.y},
                                     .color = style.accent, .radius = style.radius});
    }
    drawText(ui, list, formatted(), inset(rect_, style.padding), style, Align::kCenter);
}

// ── Dropdown ─────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
class DropdownItem final : public Widget
{
  public:
    DropdownItem(Dropdown &owner, int index, std::string text) : Widget("item"), owner_(&owner), index_(index), text_(std::move(text)) {}
    bool interactive() const override { return true; }
    void onRelease(Ui &ui, glm::vec2, bool inside) override
    {
        if (inside)
        {
            owner_->choose(ui, index_);
        }
    }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2, const Style &style) override
    {
        return {ui.font().measure(text_, style.fontSize, style.font.get()).x, lineHeight(ui, style)};
    }
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override
    {
        const bool current = owner_->selected() == index_;
        drawText(ui, list, text_, inset(rect_, style.padding), style, Align::kStart, current ? &style.accent : nullptr);
    }

  private:
    Dropdown *owner_;
    int index_;
    std::string text_;
};
} // namespace

Dropdown::Dropdown(std::function<std::vector<std::string>()> options, Binding<int> selected)
    : Widget("dropdown"), options_(std::move(options)), selected_(std::move(selected))
{
}

Dropdown::Dropdown(std::vector<std::string> options, Binding<int> selected)
    : Dropdown([o = std::move(options)] { return o; }, std::move(selected))
{
}

void Dropdown::open(Ui &ui)
{
    auto list = std::make_unique<ScrollView>("popup");
    auto &items = list->emplace<VBox>();
    const std::vector<std::string> all = options();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        items.emplace<DropdownItem>(*this, static_cast<int>(i), all[i]);
    }
    ui.openPopup(*this, std::move(list), rect_);
}

void Dropdown::choose(Ui &ui, int index)
{
    ui.closePopup();
    const int count = static_cast<int>(options().size());
    if (index < 0 || index >= count || index == selected() || !selected_.writable())
    {
        return;
    }
    ui.defer([set = selected_.set, index] { set(index); });
    emit(ui, EventKind::kChange);
}

void Dropdown::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (!inside)
    {
        return;
    }
    if (ui.popupOpenFor(*this))
        ui.closePopup();
    else
        open(ui);
}

void Dropdown::onKey(Ui &ui, int key)
{
    if (key == kKeyUp)
        choose(ui, selected() - 1);
    else if (key == kKeyDown)
        choose(ui, selected() + 1);
    else if (key == kKeyEnter || key == kKeySpace)
        ui.popupOpenFor(*this) ? ui.closePopup() : open(ui);
    else if (key == kKeyEscape)
        ui.closePopup();
}

glm::vec2 Dropdown::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    float widest = 0.0f;
    for (const std::string &o : options())
    {
        widest = std::max(widest, ui.font().measure(o, style.fontSize, style.font.get()).x);
    }
    return {widest + style.spacing + ui.font().measure("v", style.fontSize, style.font.get()).x, lineHeight(ui, style)};
}

void Dropdown::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const std::vector<std::string> all = options();
    const int i = selected();
    if (i >= 0 && i < static_cast<int>(all.size()))
    {
        drawText(ui, list, all[static_cast<std::size_t>(i)], content, style, Align::kStart);
    }
    drawText(ui, list, "v", content, style, Align::kEnd, &style.accent);
}

// ── TextField ────────────────────────────────────────────────────────────────────────────────────────────────────────

TextField::TextField(Binding<std::string> value, std::string styleName) : Widget(std::move(styleName)), value_(std::move(value)) {}

std::string TextField::current() const { return value_.value(); }
std::string TextField::text() const { return editing_ ? edit_.text : current(); }

bool TextField::commit(Ui &ui, const std::string &text)
{
    if (!value_.writable())
    {
        return false;
    }
    if (text == current())
    {
        return true;
    }
    ui.defer([set = value_.set, text] { set(text); });
    emit(ui, EventKind::kChange);
    return true;
}

void TextField::onFocusChanged(Ui &ui, bool focused)
{
    Widget::onFocusChanged(ui, focused);
    if (focused)
    {
        edit_.text = current();
        edit_.selectAll(); // prendre le focus sélectionne tout : la frappe remplace la valeur
        edit_.resetHistory();
        editing_ = true;
        return;
    }
    if (editing_)
    {
        editing_ = false;
        (void)commit(ui, edit_.text);
    }
}

std::size_t TextField::positionAt(const Ui &ui, glm::vec2 mouse) const
{
    const Style s = style(ui.theme());
    const Rect content = inset(rect_, s.padding);
    const float caretX = ui.font().measure(std::string_view(edit_.text).substr(0, edit_.caret), s.fontSize, s.font.get()).x;
    const float offset = std::max(0.0f, caretX - content.size.x + 2.0f);
    return ui.font().caretIndex(edit_.text, s.fontSize, mouse.x - content.position.x + offset, s.font.get());
}

void TextField::onPress(Ui &ui, glm::vec2 mouse)
{
    if (!editing_)
    {
        return;
    }
    edit_.moveTo(positionAt(ui, mouse), ui.shiftHeld());
}

void TextField::onDrag(Ui &ui, glm::vec2 mouse)
{
    if (editing_)
    {
        edit_.moveTo(positionAt(ui, mouse), true); // glisser sélectionne
    }
}

void TextField::onText(Ui &, std::string_view text)
{
    if (!editing_)
    {
        return;
    }
    std::string clean;
    for (char c : text)
    {
        if (static_cast<unsigned char>(c) >= 0x20U && c != 0x7F)
        {
            clean.push_back(c);
        }
    }
    if (clean.empty())
    {
        return;
    }
    edit_.beginEdit(TextEditState::EditKind::kTyping, clean);
    edit_.insert(clean);
}

void TextField::onKey(Ui &ui, int key)
{
    if (!editing_)
    {
        return;
    }
    const bool extend = ui.shiftHeld();
    switch (key)
    {
    case kKeyBackspace:
        if (edit_.hasSelection() || edit_.caret > 0)
        {
            edit_.beginEdit(TextEditState::EditKind::kDeleting);
            if (!edit_.hasSelection())
                edit_.anchor = utf8Previous(edit_.text, edit_.caret);
            edit_.eraseSelection();
        }
        break;
    case kKeyDelete:
        if (edit_.hasSelection() || edit_.caret < edit_.text.size())
        {
            edit_.beginEdit(TextEditState::EditKind::kDeleting);
            if (!edit_.hasSelection())
                edit_.anchor = edit_.caret + utf8Length(edit_.text, edit_.caret);
            edit_.eraseSelection();
        }
        break;
    case kKeyUndo: (void)edit_.undo(); break;
    case kKeyRedo: (void)edit_.redo(); break;
    case kKeyLeft:
        edit_.moveTo(edit_.hasSelection() && !extend ? edit_.selectionStart() : utf8Previous(edit_.text, edit_.caret), extend);
        break;
    case kKeyRight:
        edit_.moveTo(edit_.hasSelection() && !extend ? edit_.selectionEnd()
                                                     : (edit_.caret < edit_.text.size() ? edit_.caret + utf8Length(edit_.text, edit_.caret) : edit_.caret),
                     extend);
        break;
    case kKeyHome: edit_.moveTo(0, extend); break;
    case kKeyEnd: edit_.moveTo(edit_.text.size(), extend); break;
    case kKeySelectAll: edit_.selectAll(); break;
    case kKeyCopy:
        if (edit_.hasSelection())
            ui.setClipboardText(edit_.selected());
        break;
    case kKeyCut:
        if (edit_.hasSelection())
        {
            ui.setClipboardText(edit_.selected());
            edit_.beginEdit(TextEditState::EditKind::kOther);
            edit_.eraseSelection();
        }
        break;
    case kKeyPaste:
    {
        std::string pasted = ui.clipboardText();
        std::erase_if(pasted, [](char c) { return c == '\n' || c == '\r'; }); // une seule ligne
        if (!pasted.empty() || edit_.hasSelection())
        {
            edit_.beginEdit(TextEditState::EditKind::kOther);
            edit_.insert(pasted);
        }
        break;
    }
    case kKeyEnter:
        if (!commit(ui, edit_.text))
        {
            edit_.text = current();
        }
        edit_.moveTo(edit_.text.size(), false);
        break;
    case kKeyEscape:
        edit_.text = current(); // annule : la perte du focus ne change rien
        edit_.moveTo(edit_.text.size(), false);
        ui.focus(nullptr);
        break;
    default: break;
    }
}

glm::vec2 TextField::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    // Assez large pour une dizaine de caractères ; `width` du langage ou du parent pour plus.
    return {ui.font().measure("0000000000", style.fontSize, style.font.get()).x, lineHeight(ui, style)};
}

void TextField::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const std::string shown = text();
    float offset = 0.0f;
    float caretX = 0.0f;
    const float h = lineHeight(ui, style);
    const float top = content.position.y + (content.size.y - h) * 0.5f;
    if (editing_)
    {
        caretX = ui.font().measure(std::string_view(edit_.text).substr(0, edit_.caret), style.fontSize, style.font.get()).x;
        offset = std::max(0.0f, caretX - content.size.x + 2.0f);
    }
    list.pushClip(content.position, content.size);
    if (editing_ && edit_.hasSelection())
    {
        auto xAt = [&](std::size_t pos) { return ui.font().measure(std::string_view(edit_.text).substr(0, pos), style.fontSize, style.font.get()).x; };
        Color selection = style.accent;
        selection.a *= 0.35f;
        const float a = xAt(edit_.selectionStart());
        const float b = xAt(edit_.selectionEnd());
        list.rect({content.position.x + a - offset, top}, {b - a, h}, selection);
    }
    drawText(ui, list, shown, {{content.position.x - offset, content.position.y}, {content.size.x + offset, content.size.y}}, style, Align::kStart);
    if (editing_ && std::fmod(ui.time(), 1.0f) < 0.6f)
    {
        list.rect(renderer::DrawRect{.position = {std::round(content.position.x + caretX - offset), top},
                                     .size = {std::max(1.0f, style.borderWidth), h},
                                     .color = style.foreground});
    }
    list.popClip();
}

// ── NumberField ──────────────────────────────────────────────────────────────────────────────────────────────────────

NumberField::NumberField(Binding<double> value, bool integer, double min, double max)
    : TextField(Binding<std::string>{}), number_(std::move(value)), integer_(integer), min_(min), max_(max)
{
}

std::string NumberField::current() const
{
    const double v = number_.value();
    if (integer_)
    {
        return formatNumber(v, 0);
    }
    char buffer[64];
    // Écriture la plus courte qui relit la même valeur en float (les paramètres sont des float) : 0.35 et non 0.3499999940.
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, static_cast<float>(v));
    return ec == std::errc() ? std::string(buffer, end) : formatNumber(v, 6);
}

bool NumberField::commit(Ui &ui, const std::string &text)
{
    if (!number_.writable())
    {
        return false;
    }
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if (first == std::string::npos)
    {
        return false;
    }
    const std::string trimmed = text.substr(first, last - first + 1);
    double v = 0.0;
    const char *begin = trimmed.data();
    const char *end = begin + trimmed.size();
    if (*begin == '+')
    {
        ++begin;
    }
    auto [ptr, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc() || ptr != end || !std::isfinite(v) || v < min_ || v > max_)
    {
        return false;
    }
    if (integer_)
    {
        v = std::round(v);
    }
    if (v == number_.value())
    {
        return true;
    }
    ui.defer([set = number_.set, v] { set(v); });
    emit(ui, EventKind::kChange);
    return true;
}

// ── ColorPicker ──────────────────────────────────────────────────────────────────────────────────────────────────────

ColorPicker::ColorPicker(Binding<Color> value, bool alpha) : Widget("colorPicker"), value_(std::move(value))
{
    auto &sliders = emplace<VBox>();
    const char *names[] = {"R ", "G ", "B ", "A "};
    for (int i = 0; i < (alpha ? 4 : 3); ++i)
    {
        Binding<double> channel;
        channel.get = [get = value_.get, i] { return get ? static_cast<double>(get()[i]) : 0.0; };
        if (value_.writable())
        {
            channel.set = [b = value_, i](const double &v)
            {
                Color c = b.get();
                c[i] = static_cast<float>(v);
                b.set(c);
            };
        }
        auto &s = sliders.emplace<Slider>(std::move(channel), 0.0, 1.0, 0.01);
        s.setPrefix(names[i]);
        s.onChange([this](const Event &) { fire(EventKind::kChange); }); // un canal qui change = la couleur qui change
    }
}

glm::vec2 ColorPicker::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    Widget &sliders = *children()[0];
    const float swatch = style.width;
    const glm::vec2 s = sliders.measure(ui, {available.x - swatch - style.spacing, available.y});
    return {swatch + style.spacing + s.x, std::max(swatch, s.y)};
}

void ColorPicker::arrange(Ui &ui, const Rect &content, const Style &style)
{
    const float swatch = style.width;
    children()[0]->layout(ui, {{content.position.x + swatch + style.spacing, content.position.y},
                               {std::max(0.0f, content.size.x - swatch - style.spacing), content.size.y}});
}

void ColorPicker::drawBackground(const Ui &, renderer::DrawList &, const Style &) const {}

void ColorPicker::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    Style swatch = style;
    swatch.background = color();
    swatch.background.a = 1.0f;
    drawBox(list, {content.position, {style.width, content.size.y}}, swatch);
    drawChildren(ui, list);
}

// ── Plot ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

Plot::Plot(std::function<std::vector<float>()> values) : Widget("plot"), values_(std::move(values)) {}

glm::vec2 Plot::measureContent(Ui &, glm::vec2, const Style &style) { return {style.width * 6.0f, style.width * 3.0f}; }

void Plot::drawContent(const Ui &, renderer::DrawList &list, const Style &style) const
{
    const std::vector<float> v = values_ ? values_() : std::vector<float>{};
    const Rect content = inset(rect_, style.padding);
    if (v.empty() || content.size.x <= 0.0f || content.size.y <= 0.0f)
    {
        return;
    }
    float lo = min_;
    float hi = max_;
    if (!fixedRange_)
    {
        lo = std::min(0.0f, *std::min_element(v.begin(), v.end()));
        hi = std::max(0.0f, *std::max_element(v.begin(), v.end()));
    }
    if (hi <= lo)
    {
        hi = lo + 1.0f;
    }
    const float barWidth = content.size.x / static_cast<float>(v.size());
    const float gap = barWidth > 3.0f ? 1.0f : 0.0f;
    const float zero = content.end().y - (std::clamp(0.0f, lo, hi) - lo) / (hi - lo) * content.size.y;
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        const float y = content.end().y - (std::clamp(v[i], lo, hi) - lo) / (hi - lo) * content.size.y;
        const float top = std::min(y, zero);
        const float h = std::abs(zero - y);
        if (h > 0.0f)
        {
            list.rect({content.position.x + barWidth * static_cast<float>(i), top}, {std::max(barWidth - gap, 1.0f), h}, style.accent);
        }
    }
}

// ── Tabs ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
class TabButton final : public Widget
{
  public:
    TabButton(Tabs &owner, int index, std::string title) : Widget("tab"), owner_(&owner), index_(index), title_(std::move(title)) {}
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &ui, glm::vec2, bool inside) override
    {
        if (inside)
            owner_->select(ui, index_);
    }
    void onKey(Ui &ui, int key) override
    {
        if (key == kKeyEnter || key == kKeySpace)
            owner_->select(ui, index_);
    }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2, const Style &style) override
    {
        return {ui.font().measure(title_, style.fontSize, style.font.get()).x, lineHeight(ui, style)};
    }
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override
    {
        drawText(ui, list, title_, inset(rect_, style.padding), style, Align::kCenter);
        if (owner_->selected() == index_)
        {
            const float h = std::max(2.0f, 2.0f * style.borderWidth);
            list.rect({rect_.position.x, rect_.end().y - h}, {rect_.size.x, h}, style.accent);
        }
    }

  private:
    Tabs *owner_;
    int index_;
    std::string title_;
};
} // namespace

Tabs::Tabs() : Tabs(Binding<int>{}) {}

Tabs::Tabs(Binding<int> selected) : VBox("tabs"), selected_(std::move(selected)) { header_ = &emplace<HBox>(); }

Widget &Tabs::addPage(std::string title, std::unique_ptr<Widget> page)
{
    const int index = static_cast<int>(pages_.size());
    header_->emplace<TabButton>(*this, index, std::move(title));
    page->bindVisible([this, index] { return selected() == index; });
    Widget &added = add(std::move(page));
    pages_.push_back(&added);
    return added;
}

int Tabs::selected() const
{
    const int n = static_cast<int>(pages_.size());
    const int i = selected_.get ? selected_.get() : local_;
    return n == 0 ? 0 : std::clamp(i, 0, n - 1);
}

void Tabs::select(Ui &ui, int index)
{
    if (index == selected() || index < 0 || index >= static_cast<int>(pages_.size()))
    {
        return;
    }
    if (selected_.writable())
        ui.defer([set = selected_.set, index] { set(index); });
    else if (!selected_.get)
        local_ = index;
    else
        return;
    emit(ui, EventKind::kChange);
}

// ── Section ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
class SectionHeader final : public Widget
{
  public:
    SectionHeader(Section &owner, std::string title) : Widget("sectionHeader"), owner_(&owner), title_(std::move(title)) {}
    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onRelease(Ui &, glm::vec2, bool inside) override
    {
        if (inside)
            owner_->setExpanded(!owner_->expanded());
    }
    void onKey(Ui &, int key) override
    {
        if (key == kKeyEnter || key == kKeySpace)
            owner_->setExpanded(!owner_->expanded());
    }

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2, const Style &style) override
    {
        return {ui.font().measure("- " + title_, style.fontSize, style.font.get()).x, lineHeight(ui, style)};
    }
    void drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const override
    {
        drawText(ui, list, (owner_->expanded() ? "- " : "+ ") + title_, inset(rect_, style.padding), style, Align::kStart);
    }

  private:
    Section *owner_;
    std::string title_;
};
} // namespace

Section::Section(std::string title, bool expanded) : VBox("section"), expanded_(expanded)
{
    emplace<SectionHeader>(*this, std::move(title));
    body_ = &emplace<VBox>();
    body_->bindVisible([this] { return expanded_; });
}

glm::vec2 Section::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    body_->margin.left = style.width; // retrait du contenu : jeton du thème
    return VBox::measureContent(ui, available, style);
}

// ── Separator ────────────────────────────────────────────────────────────────────────────────────────────────────────

glm::vec2 Separator::measureContent(Ui &, glm::vec2, const Style &style) { return {0.0f, style.width}; }
void Separator::drawContent(const Ui &, renderer::DrawList &, const Style &) const {}

} // namespace engine::ui
