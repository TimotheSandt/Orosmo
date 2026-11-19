#include "engine/ui/controls.hpp"

#include "engine/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::ui
{

namespace
{
float lineHeight(const Ui &ui, const Style &style) { return ui.font().measure("Hg", style.fontSize, style.font.get()).y; }

std::string formatValue(double v, double step)
{
    char buffer[64];
    if (step > 0.0 && std::abs(step - std::round(step)) < 1e-9)
        std::snprintf(buffer, sizeof buffer, "%.0f", v);
    else if (step > 0.0)
        std::snprintf(buffer, sizeof buffer, "%.*f", std::clamp(static_cast<int>(std::ceil(-std::log10(step) - 1e-9)), 0, 6), v);
    else
        std::snprintf(buffer, sizeof buffer, "%.4g", v);
    return buffer;
}
} // namespace

// ── TextEditState ────────────────────────────────────────────────────────────────────────────────────────────────────

void TextEditState::eraseSelection()
{
    if (!hasSelection())
        return;
    const std::size_t a = selectionStart();
    text.erase(a, selectionEnd() - a);
    caret = anchor = a;
}

void TextEditState::insert(std::string_view s)
{
    eraseSelection();
    text.insert(caret, s);
    caret += s.size();
    anchor = caret;
}

void TextEditState::moveTo(std::size_t position, bool extend)
{
    caret = std::min(position, text.size());
    if (!extend)
        anchor = caret;
}

void TextEditState::selectAll()
{
    anchor = 0;
    caret = text.size();
}

// ── RadioButton ──────────────────────────────────────────────────────────────────────────────────────────────────────

RadioButton::RadioButton(std::string text, int value, Binding<int> selected)
    : Widget("radio"), text_(std::move(text)), value_(value), selected_(std::move(selected))
{
}

void RadioButton::select(Ui &ui)
{
    if (checked() || !selected_.writable())
        return;
    ui.defer([set = selected_.set, v = value_] { set(v); });
    emit(ui, EventKind::kChange);
}

void RadioButton::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (inside)
    {
        select(ui);
        emit(ui, EventKind::kClick);
    }
}

void RadioButton::onKey(Ui &ui, int key)
{
    if (key == kKeyEnter || key == kKeySpace)
        select(ui);
}

glm::vec2 RadioButton::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const float text = text_.empty() ? 0.0f : style.spacing + ui.font().measure(text_, style.fontSize, style.font.get()).x;
    return {style.width + text, std::max(style.width, lineHeight(ui, style))};
}

void RadioButton::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const float d = style.width;
    const Rect circle{{content.position.x, content.position.y + (content.size.y - d) * 0.5f}, {d, d}};
    Style round = style;
    round.radius = d * 0.5f; // un cercle, quel que soit le rayon du thème
    drawBox(list, circle, round);
    if (checked())
    {
        const float m = std::round(d * 0.28f);
        list.rect(renderer::DrawRect{.position = circle.position + glm::vec2(m), .size = circle.size - glm::vec2(2.0f * m), .color = style.accent,
                                     .radius = (d - 2.0f * m) * 0.5f});
    }
    if (!text_.empty())
    {
        drawText(ui, list, text_, {{circle.end().x + style.spacing, content.position.y}, {content.end().x - circle.end().x - style.spacing, content.size.y}},
                 style, Align::kStart);
    }
}

// ── RangeSlider ──────────────────────────────────────────────────────────────────────────────────────────────────────

RangeSlider::RangeSlider(Binding<double> low, Binding<double> high, double min, double max, double step)
    : Widget("slider"), low_(std::move(low)), high_(std::move(high)), min_(std::min(min, max)), max_(std::max(min, max)), step_(std::max(step, 0.0))
{
    defaultLow_ = low_.get ? quantize(low_.value()) : min_;
    defaultHigh_ = high_.get ? quantize(high_.value()) : max_;
}

double RangeSlider::low() const { return hasPending_[0] ? pending_[0] : low_.value(); }
double RangeSlider::high() const { return hasPending_[1] ? pending_[1] : high_.value(); }

double RangeSlider::quantize(double v) const
{
    v = std::clamp(v, min_, max_);
    if (step_ > 0.0)
        v = std::clamp(min_ + std::round((v - min_) / step_) * step_, min_, max_);
    return v;
}

std::string RangeSlider::formatted() const { return formatValue(low(), step_) + " - " + formatValue(high(), step_); }

float RangeSlider::xOf(const Ui &ui, double v) const
{
    const Rect track = inset(rect_, style(ui.theme()).padding);
    const double t = max_ > min_ ? (v - min_) / (max_ - min_) : 0.0;
    return track.position.x + static_cast<float>(t) * track.size.x;
}

double RangeSlider::valueAt(const Ui &ui, float x) const
{
    const Rect track = inset(rect_, style(ui.theme()).padding);
    if (track.size.x <= 0.0f)
        return min_;
    return min_ + std::clamp(static_cast<double>((x - track.position.x) / track.size.x), 0.0, 1.0) * (max_ - min_);
}

void RangeSlider::write(Ui &ui, int handle, double v)
{
    // Les poignées ne se croisent pas : la basse reste sous la haute.
    v = handle == 0 ? std::min(quantize(v), high()) : std::max(quantize(v), low());
    const Binding<double> &b = handle == 0 ? low_ : high_;
    if (!b.writable() || v == (handle == 0 ? low() : high()))
        return;
    pending_[static_cast<std::size_t>(handle)] = v;
    hasPending_[static_cast<std::size_t>(handle)] = true;
    ui.defer([this, handle, v] {
        (handle == 0 ? low_ : high_).set(v);
        hasPending_[static_cast<std::size_t>(handle)] = false;
    });
    emit(ui, EventKind::kChange);
}

void RangeSlider::onPress(Ui &ui, glm::vec2 mouse)
{
    // Poignée la plus proche du pointeur ; à égalité (poignées confondues), celle du côté où l'on clique.
    const float dl = std::abs(mouse.x - xOf(ui, low()));
    const float dh = std::abs(mouse.x - xOf(ui, high()));
    handle_ = dl < dh ? 0 : (dh < dl ? 1 : (mouse.x < xOf(ui, low()) ? 0 : 1));
    dragging_ = false;
    pressX_ = lastX_ = mouse.x;
    dragValue_ = handle_ == 0 ? low() : high();
}

void RangeSlider::onDrag(Ui &ui, glm::vec2 mouse)
{
    if (!dragging_)
    {
        if (std::abs(mouse.x - pressX_) <= Slider::kDragThreshold)
            return;
        dragging_ = true;
    }
    const float width = std::max(inset(rect_, style(ui.theme()).padding).size.x, 1.0f);
    const double perPixel = (max_ - min_) / static_cast<double>(width) * (ui.shiftHeld() ? 0.1 : 1.0);
    dragValue_ = std::clamp(dragValue_ + static_cast<double>(mouse.x - lastX_) * perPixel, min_, max_);
    lastX_ = mouse.x;
    write(ui, handle_, dragValue_);
}

void RangeSlider::onRelease(Ui &ui, glm::vec2 mouse, bool inside)
{
    if (!dragging_ && inside)
        write(ui, handle_, valueAt(ui, mouse.x)); // simple clic : la poignée la plus proche vient sous le pointeur
    dragging_ = false;
}

bool RangeSlider::onSecondaryClick(Ui &ui, glm::vec2)
{
    // Haut d'abord si on remonte (la basse ne peut pas dépasser la haute courante), sinon bas d'abord.
    if (defaultLow_ > high())
    {
        write(ui, 1, defaultHigh_);
        write(ui, 0, defaultLow_);
    }
    else
    {
        write(ui, 0, defaultLow_);
        write(ui, 1, defaultHigh_);
    }
    return true;
}

void RangeSlider::onKey(Ui &ui, int key)
{
    const double step = step_ > 0.0 ? step_ : (max_ - min_) / 100.0;
    const double v = handle_ == 0 ? low() : high();
    if (key == kKeyLeft || key == kKeyDown)
        write(ui, handle_, v - step);
    else if (key == kKeyRight || key == kKeyUp)
        write(ui, handle_, v + step);
    else if (key == kKeyHome)
        handle_ = 0;
    else if (key == kKeyEnd)
        handle_ = 1;
}

glm::vec2 RangeSlider::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    const float w = ui.font().measure(formatValue(max_, step_) + " - " + formatValue(max_, step_), style.fontSize, style.font.get()).x;
    return {w + 2.0f * style.width, std::max(style.width, lineHeight(ui, style))};
}

void RangeSlider::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const float a = xOf(ui, low());
    const float b = xOf(ui, high());
    list.rect(renderer::DrawRect{.position = {a, rect_.position.y}, .size = {std::max(b - a, 0.0f), rect_.size.y}, .color = style.accent,
                                 .radius = style.radius});
    // Poignées : traits pleins à chaque bout de la plage (la plus récemment prise un peu plus large).
    for (int h = 0; h < 2; ++h)
    {
        const float x = h == 0 ? a : b;
        const float w = std::max(3.0f, style.width * (h == handle_ ? 0.3f : 0.2f));
        list.rect(renderer::DrawRect{.position = {std::round(x - w * 0.5f), rect_.position.y}, .size = {w, rect_.size.y}, .color = style.foreground,
                                     .radius = w * 0.5f});
    }
    drawText(ui, list, formatted(), inset(rect_, style.padding), style, Align::kCenter);
}

// ── SpinBox ──────────────────────────────────────────────────────────────────────────────────────────────────────────

SpinBox::SpinBox(Binding<double> value, double min, double max, double stepSize, bool integer)
    : Widget("box"), value_(std::move(value)), min_(std::min(min, max)), max_(std::max(min, max)), step_(stepSize > 0.0 ? stepSize : 1.0)
{
    field_ = &emplace<NumberField>(value_, integer, min_, max_);
    minus_ = &emplace<Button>("-");
    plus_ = &emplace<Button>("+");
    minus_->onClick([this](const Event &) { if (ui_ != nullptr) this->step(*ui_, -1.0); });
    plus_->onClick([this](const Event &) { if (ui_ != nullptr) this->step(*ui_, 1.0); });
}

void SpinBox::step(Ui &ui, double direction)
{
    if (!value_.writable())
        return;
    const double v = std::clamp(value_.value() + direction * step_, min_, max_);
    if (v == value_.value())
        return;
    ui.defer([set = value_.set, v] { set(v); });
    emit(ui, EventKind::kChange);
}

bool SpinBox::onWheel(Ui &ui, glm::vec2 wheel)
{
    if (wheel.y == 0.0f || !enabled())
        return false;
    step(ui, wheel.y > 0.0f ? 1.0 : -1.0);
    return true;
}

glm::vec2 SpinBox::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    const glm::vec2 f = field_->measure(ui, available);
    const glm::vec2 m = minus_->measure(ui, available);
    const glm::vec2 p = plus_->measure(ui, available);
    const float side = std::max({m.x, p.x, f.y});
    return {f.x + 2.0f * (side + style.spacing), std::max({f.y, m.y, p.y})};
}

void SpinBox::arrange(Ui &ui, const Rect &content, const Style &style)
{
    ui_ = &ui;
    const float side = content.size.y;
    const float fieldW = std::max(0.0f, content.size.x - 2.0f * (side + style.spacing));
    field_->layout(ui, {content.position, {fieldW, content.size.y}});
    minus_->layout(ui, {{content.position.x + fieldW + style.spacing, content.position.y}, {side, side}});
    plus_->layout(ui, {{content.end().x - side, content.position.y}, {side, side}});
}

// ── TextArea ─────────────────────────────────────────────────────────────────────────────────────────────────────────

TextArea::TextArea(Binding<std::string> value) : Widget("textField"), value_(std::move(value)) {}

std::string TextArea::text() const { return syncing_ ? value_.value() : edit_.text; }

std::vector<TextArea::Line> TextArea::lines() const
{
    std::vector<Line> out;
    const std::string &t = syncing_ ? edit_.text : edit_.text;
    std::size_t begin = 0;
    for (;;)
    {
        const std::size_t nl = t.find('\n', begin);
        if (nl == std::string::npos)
        {
            out.push_back({begin, t.size()});
            break;
        }
        out.push_back({begin, nl});
        begin = nl + 1;
    }
    return out;
}

std::size_t TextArea::lineOf(std::size_t position) const
{
    const auto ls = lines();
    for (std::size_t i = 0; i < ls.size(); ++i)
        if (position <= ls[i].end)
            return i;
    return ls.size() - 1;
}

float TextArea::gutter(const Ui &ui, const Style &style) const
{
    if (!lineNumbers_)
        return 0.0f;
    const std::string widest(std::to_string(lines().size()).size() + 1, '0');
    return ui.font().measure(widest, style.fontSize, style.font.get()).x + style.spacing;
}

std::size_t TextArea::positionAt(const Ui &ui, glm::vec2 mouse) const
{
    const Style s = style(ui.theme());
    const Rect content = inset(rect_, s.padding);
    const float lh = lineHeight(ui, s);
    const auto ls = lines();
    const auto row = static_cast<std::size_t>(std::clamp((mouse.y - content.position.y + scroll_) / lh, 0.0f, static_cast<float>(ls.size() - 1)));
    const Line &l = ls[row];
    const std::string_view text = std::string_view(edit_.text).substr(l.begin, l.end - l.begin);
    return l.begin + ui.font().caretIndex(text, s.fontSize, mouse.x - content.position.x - gutter(ui, s), s.font.get());
}

void TextArea::changed(Ui &ui)
{
    // Un éditeur écrit à chaque modification (pas seulement à la validation) : la valeur liée suit la frappe.
    if (value_.writable())
    {
        ui.defer([set = value_.set, t = edit_.text] { set(t); });
        emit(ui, EventKind::kChange);
    }
    revealCaret(ui);
}

void TextArea::revealCaret(const Ui &ui)
{
    const Style s = style(ui.theme());
    const float lh = lineHeight(ui, s);
    const float view = inset(rect_, s.padding).size.y;
    const float y = static_cast<float>(lineOf(edit_.caret)) * lh;
    if (y < scroll_)
        scroll_ = y;
    else if (y + lh > scroll_ + view)
        scroll_ = y + lh - view;
    scroll_ = std::max(scroll_, 0.0f);
}

void TextArea::onFocusChanged(Ui &ui, bool focused)
{
    Widget::onFocusChanged(ui, focused);
    if (focused)
    {
        edit_.text = value_.value();
        edit_.caret = std::min(edit_.caret, edit_.text.size());
        edit_.anchor = edit_.caret;
        syncing_ = false;
    }
    else
    {
        syncing_ = true;
        edit_.anchor = edit_.caret;
    }
}

void TextArea::onPress(Ui &ui, glm::vec2 mouse)
{
    if (syncing_)
        edit_.text = value_.value();
    edit_.moveTo(positionAt(ui, mouse), ui.shiftHeld());
    desiredX_ = -1.0f;
}

void TextArea::onDrag(Ui &ui, glm::vec2 mouse) { edit_.moveTo(positionAt(ui, mouse), true); }

void TextArea::onText(Ui &ui, std::string_view text)
{
    if (readOnly_)
        return;
    std::string clean;
    for (char c : text)
        if (static_cast<unsigned char>(c) >= 0x20U && c != 0x7F)
            clean.push_back(c);
    if (clean.empty())
        return;
    edit_.insert(clean);
    desiredX_ = -1.0f;
    changed(ui);
}

void TextArea::onKey(Ui &ui, int key)
{
    const bool extend = ui.shiftHeld();
    const auto ls = lines();
    const std::size_t row = lineOf(edit_.caret);
    const Style s = style(ui.theme());
    auto xOf = [&](std::size_t pos)
    {
        const Line &l = ls[lineOf(pos)];
        return ui.font().measure(std::string_view(edit_.text).substr(l.begin, pos - l.begin), s.fontSize, s.font.get()).x;
    };
    auto toRow = [&](std::size_t r)
    {
        if (desiredX_ < 0.0f)
            desiredX_ = xOf(edit_.caret);
        const Line &l = ls[r];
        const std::string_view text = std::string_view(edit_.text).substr(l.begin, l.end - l.begin);
        edit_.moveTo(l.begin + ui.font().caretIndex(text, s.fontSize, desiredX_, s.font.get()), extend);
    };
    const float lh = lineHeight(ui, s);
    const auto page = static_cast<std::size_t>(std::max(1.0f, inset(rect_, s.padding).size.y / lh));
    bool edited = false;
    switch (key)
    {
    case kKeyLeft:
        edit_.moveTo(edit_.hasSelection() && !extend ? edit_.selectionStart() : utf8Previous(edit_.text, edit_.caret), extend);
        desiredX_ = -1.0f;
        break;
    case kKeyRight:
        edit_.moveTo(edit_.hasSelection() && !extend ? edit_.selectionEnd()
                                                     : (edit_.caret < edit_.text.size() ? edit_.caret + utf8Length(edit_.text, edit_.caret) : edit_.caret),
                     extend);
        desiredX_ = -1.0f;
        break;
    case kKeyUp:
        if (row > 0)
            toRow(row - 1);
        break;
    case kKeyDown:
        if (row + 1 < ls.size())
            toRow(row + 1);
        break;
    case kKeyPageUp: toRow(row > page ? row - page : 0); break;
    case kKeyPageDown: toRow(std::min(row + page, ls.size() - 1)); break;
    case kKeyHome:
        edit_.moveTo(ls[row].begin, extend);
        desiredX_ = -1.0f;
        break;
    case kKeyEnd:
        edit_.moveTo(ls[row].end, extend);
        desiredX_ = -1.0f;
        break;
    case kKeySelectAll: edit_.selectAll(); break;
    case kKeyCopy:
        if (edit_.hasSelection())
            ui.setClipboardText(edit_.selected());
        break;
    case kKeyCut:
        if (edit_.hasSelection() && !readOnly_)
        {
            ui.setClipboardText(edit_.selected());
            edit_.eraseSelection();
            edited = true;
        }
        break;
    case kKeyPaste:
        if (!readOnly_)
        {
            edit_.insert(ui.clipboardText());
            edited = true;
        }
        break;
    case kKeyEnter:
        if (!readOnly_)
        {
            edit_.insert("\n");
            edited = true;
        }
        break;
    case kKeyTab:
        if (!readOnly_)
        {
            edit_.insert("    ");
            edited = true;
        }
        break;
    case kKeyBackspace:
        if (!readOnly_ && (edit_.hasSelection() || edit_.caret > 0))
        {
            if (!edit_.hasSelection())
                edit_.anchor = utf8Previous(edit_.text, edit_.caret);
            edit_.eraseSelection();
            edited = true;
        }
        break;
    case kKeyDelete:
        if (!readOnly_ && (edit_.hasSelection() || edit_.caret < edit_.text.size()))
        {
            if (!edit_.hasSelection())
                edit_.anchor = edit_.caret + utf8Length(edit_.text, edit_.caret);
            edit_.eraseSelection();
            edited = true;
        }
        break;
    case kKeyEscape: ui.focus(nullptr); return;
    default: return;
    }
    if (edited)
    {
        desiredX_ = -1.0f;
        changed(ui);
    }
    else
    {
        revealCaret(ui);
    }
}

bool TextArea::onWheel(Ui &ui, glm::vec2 wheel)
{
    const Style s = style(ui.theme());
    const float lh = lineHeight(ui, s);
    const float content = static_cast<float>(lines().size()) * lh;
    const float maxScroll = std::max(0.0f, content - inset(rect_, s.padding).size.y);
    if (maxScroll <= 0.0f || wheel.y == 0.0f)
        return false;
    scroll_ = std::clamp(scroll_ - wheel.y * 3.0f * lh, 0.0f, maxScroll);
    return true;
}

glm::vec2 TextArea::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    if (syncing_)
        edit_.text = value_.value();
    // Six lignes et une quarantaine de caractères par défaut ; .width / .height pour plus.
    return {ui.font().measure(std::string(40, '0'), style.fontSize, style.font.get()).x, 6.0f * lineHeight(ui, style)};
}

void TextArea::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const float lh = lineHeight(ui, style);
    const float g = gutter(ui, style);
    const auto ls = lines();
    list.pushClip(content.position, content.size);
    const std::size_t first = static_cast<std::size_t>(scroll_ / lh);
    const std::size_t s0 = edit_.selectionStart();
    const std::size_t s1 = edit_.selectionEnd();
    const bool editing = !syncing_;
    Color selection = style.accent;
    selection.a *= 0.35f;
    for (std::size_t i = first; i < ls.size(); ++i)
    {
        const float y = content.position.y + static_cast<float>(i) * lh - scroll_;
        if (y > content.end().y)
            break;
        const Line &l = ls[i];
        const std::string_view line = std::string_view(edit_.text).substr(l.begin, l.end - l.begin);
        const float x0 = content.position.x + g;
        auto xAt = [&](std::size_t pos) { return x0 + ui.font().measure(line.substr(0, pos - l.begin), style.fontSize, style.font.get()).x; };
        if (editing && s1 > s0 && s0 <= l.end && s1 >= l.begin)
        {
            const std::size_t a = std::max(s0, l.begin);
            const std::size_t b = std::min(s1, l.end);
            const float xa = xAt(a);
            const float xb = s1 > l.end ? xAt(l.end) + 4.0f : xAt(b); // la fin de ligne sélectionnée se voit
            list.rect({xa, y}, {std::max(xb - xa, 0.0f), lh}, selection);
        }
        if (lineNumbers_)
        {
            Color muted = style.foreground;
            muted.a *= 0.45f;
            drawText(ui, list, std::to_string(i + 1), {{content.position.x, y}, {g - style.spacing, lh}}, style, Align::kEnd, &muted);
        }
        if (!line.empty())
            ui.font().draw(list, line, {x0, y}, style.fontSize, style.foreground, style.font.get());
        if (editing && edit_.caret >= l.begin && edit_.caret <= l.end && lineOf(edit_.caret) == i && std::fmod(ui.time(), 1.0f) < 0.6f)
            list.rect({std::round(xAt(edit_.caret)), y}, {std::max(1.0f, style.borderWidth), lh}, style.foreground);
    }
    list.popClip();
}

} // namespace engine::ui
