#include "orosmo/ui/controls.hpp"

#include "orosmo/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orosmo::ui
{

namespace
{
// Au moins un pixel : un thème avec `fontSize: 0` ne doit pas mener à des divisions par zéro (rangées, pages, souris).
float lineHeight(const Ui &ui, const Style &style) { return std::max(1.0f, ui.font().measure("Hg", style.fontSize, style.font.get()).y); }

// Décimales qui représentent exactement le pas ET l'origine de la plage (pas de 0.25 : deux décimales ; 0.1 depuis 0.05 : deux).
std::string formatValue(double v, double step, double origin = 0.0)
{
    char buffer[64];
    if (step > 0.0)
    {
        auto exact = [](double x, int d) { const double s = x * std::pow(10.0, d); return std::abs(s - std::round(s)) < 1e-6 * std::max(1.0, std::abs(s)); };
        int decimals = 0;
        while (decimals < 6 && !(exact(step, decimals) && exact(origin, decimals)))
            ++decimals;
        std::snprintf(buffer, sizeof buffer, "%.*f", decimals, v);
    }
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
    groupOpen_ = false; // un déplacement termine le pas d'annulation en cours
}

void TextEditState::selectAll()
{
    anchor = 0;
    caret = text.size();
    groupOpen_ = false;
}

void TextEditState::beginEdit(EditKind kind, std::string_view inserted)
{
    const bool coalesce = groupOpen_ && kind != EditKind::kOther && kind == lastKind_ && !hasSelection();
    if (!coalesce)
    {
        undo_.push_back({text, caret, anchor});
        if (undo_.size() > kMaxHistory)
            undo_.erase(undo_.begin());
    }
    redo_.clear();
    lastKind_ = kind;
    groupOpen_ = kind != EditKind::kOther && inserted.find_first_of(" \n\t") == std::string_view::npos;
}

bool TextEditState::undo()
{
    if (undo_.empty())
        return false;
    redo_.push_back({text, caret, anchor});
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    text = std::move(s.text);
    caret = s.caret;
    anchor = s.anchor;
    groupOpen_ = false;
    return true;
}

bool TextEditState::redo()
{
    if (redo_.empty())
        return false;
    undo_.push_back({text, caret, anchor});
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    text = std::move(s.text);
    caret = s.caret;
    anchor = s.anchor;
    groupOpen_ = false;
    return true;
}

void TextEditState::resetHistory() noexcept
{
    undo_.clear();
    redo_.clear();
    groupOpen_ = false;
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

std::string RangeSlider::formatted() const { return formatValue(low(), step_, min_) + " - " + formatValue(high(), step_, min_); }

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
    // Jeton de vie vérifié avant et après le setter (un gestionnaire ou le setter lui-même peut détruire le curseur).
    ui.defer([this, alive = lifetimeToken(), handle, v] {
        if (alive.expired())
            return;
        const auto set = (handle == 0 ? low_ : high_).set; // copie : le setter peut détruire le curseur
        set(v);
        if (!alive.expired())
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
    const float w = ui.font().measure(formatValue(max_, step_, min_) + " - " + formatValue(max_, step_, min_), style.fontSize, style.font.get()).x;
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
    // Saisie validée dans le champ : même événement que les boutons (NumberField n'émet que pour une valeur acceptée et changée).
    field_->onChange([this](const Event &) { fire(EventKind::kChange); });
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

TextArea::TextArea(Binding<std::string> value) : Widget("textArea"), value_(std::move(value)) {}

std::string TextArea::text() const { return syncing_ ? value_.value() : edit_.text; }

std::vector<TextArea::Line> TextArea::lines(const Ui &ui, const Style &style) const
{
    std::vector<Line> out;
    const std::string &t = edit_.text;
    const float width = inset(rect_, style.padding).size.x - gutter(ui, style);
    std::size_t begin = 0;
    for (std::size_t number = 0;; ++number)
    {
        const std::size_t nl = t.find('\n', begin);
        const std::size_t end = nl == std::string::npos ? t.size() : nl;
        std::size_t start = begin;
        // Retour automatique : la plus longue partie qui tient, coupée après la dernière espace s'il y en a une.
        while (wrap_ && width > 0.0f && start < end)
        {
            const std::string_view rest = std::string_view(t).substr(start, end - start);
            if (ui.font().measure(rest, style.fontSize, style.font.get()).x <= width)
                break;
            std::size_t fit = ui.font().caretIndex(rest, style.fontSize, width, style.font.get());
            while (fit > 0 && ui.font().measure(rest.substr(0, fit), style.fontSize, style.font.get()).x > width)
                fit = utf8Previous(rest, fit);
            if (fit == 0)
                fit = utf8Length(rest, 0); // au moins un caractère par rangée
            const std::size_t space = rest.substr(0, fit).find_last_of(' ');
            const std::size_t cut = space != std::string_view::npos && space > 0 ? space + 1 : fit;
            if (start + cut >= end)
                break; // le reste (un seul caractère trop large) forme la dernière rangée : pas de rangée vide après lui
            out.push_back({start, start + cut, start == begin, false, number});
            start += cut;
        }
        out.push_back({start, end, start == begin, true, number});
        if (nl == std::string::npos)
            break;
        begin = nl + 1;
    }
    return out;
}

std::size_t TextArea::lineOf(const std::vector<Line> &ls, std::size_t position)
{
    // À la coupure d'un retour automatique, le curseur est au début de la rangée suivante.
    for (std::size_t i = 0; i < ls.size(); ++i)
        if (position < ls[i].end || (position == ls[i].end && ls[i].last))
            return i;
    return ls.size() - 1;
}

std::size_t TextArea::rowCount(const Ui &ui) const { return lines(ui, style(ui.theme())).size(); }

float TextArea::gutter(const Ui &ui, const Style &style) const
{
    if (!lineNumbers_)
        return 0.0f;
    const auto count = static_cast<std::size_t>(std::count(edit_.text.begin(), edit_.text.end(), '\n')) + 1;
    const std::string widest(std::to_string(count).size() + 1, '0');
    return ui.font().measure(widest, style.fontSize, style.font.get()).x + style.spacing;
}

std::size_t TextArea::positionAt(const Ui &ui, glm::vec2 mouse) const
{
    const Style s = style(ui.theme());
    const Rect content = inset(rect_, s.padding);
    const float lh = lineHeight(ui, s);
    const auto ls = lines(ui, s);
    const auto row = static_cast<std::size_t>(std::clamp((mouse.y - content.position.y + scroll_) / lh, 0.0f, static_cast<float>(ls.size() - 1)));
    const Line &l = ls[row];
    const std::string_view text = std::string_view(edit_.text).substr(l.begin, l.end - l.begin);
    return l.begin + ui.font().caretIndex(text, s.fontSize, mouse.x - content.position.x - gutter(ui, s), s.font.get());
}

void TextArea::syncFromBinding()
{
    std::string v = value_.value();
    if (v == edit_.text)
        return;
    edit_.text = std::move(v); // changée hors de l'éditeur : l'historique ne s'applique plus
    edit_.resetHistory();
    // Le curseur reste dans le texte, sur le début d'un caractère UTF-8 (jamais au milieu de « é »).
    std::size_t caret = std::min(edit_.caret, edit_.text.size());
    while (caret > 0 && caret < edit_.text.size() && (static_cast<unsigned char>(edit_.text[caret]) & 0xC0U) == 0x80U)
        --caret;
    edit_.caret = edit_.anchor = caret;
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
    const float y = static_cast<float>(lineOf(lines(ui, s), edit_.caret)) * lh;
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
        syncFromBinding();
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
        syncFromBinding();
    edit_.moveTo(positionAt(ui, mouse), ui.shiftHeld());
    desiredX_ = -1.0f;
}

void TextArea::onDrag(Ui &ui, glm::vec2 mouse) { edit_.moveTo(positionAt(ui, mouse), true); }

void TextArea::onText(Ui &ui, std::string_view text)
{
    if (!editable())
        return;
    std::string clean;
    for (char c : text)
        if (static_cast<unsigned char>(c) >= 0x20U && c != 0x7F)
            clean.push_back(c);
    if (clean.empty())
        return;
    edit_.beginEdit(TextEditState::EditKind::kTyping, clean);
    edit_.insert(clean);
    desiredX_ = -1.0f;
    changed(ui);
}

void TextArea::onKey(Ui &ui, int key)
{
    const bool extend = ui.shiftHeld();
    const Style s = style(ui.theme());
    const auto ls = lines(ui, s);
    const std::size_t row = lineOf(ls, edit_.caret);
    auto xOf = [&](std::size_t pos)
    {
        const Line &l = ls[lineOf(ls, pos)];
        return ui.font().measure(std::string_view(edit_.text).substr(l.begin, pos - l.begin), s.fontSize, s.font.get()).x;
    };
    auto toRow = [&](std::size_t r)
    {
        if (desiredX_ < 0.0f)
            desiredX_ = xOf(edit_.caret);
        const Line &l = ls[r];
        const std::string_view text = std::string_view(edit_.text).substr(l.begin, l.end - l.begin);
        std::size_t target = l.begin + ui.font().caretIndex(text, s.fontSize, desiredX_, s.font.get());
        if (target == l.end && !l.last && target > l.begin) // la fin d'une rangée coupée appartient à la suivante : rester sur celle-ci
            target = utf8Previous(edit_.text, target);
        edit_.moveTo(target, extend);
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
    case kKeyEnd: // fin de la rangée (avant l'espace de coupure d'un retour automatique)
        edit_.moveTo(ls[row].last || ls[row].end == ls[row].begin ? ls[row].end : utf8Previous(edit_.text, ls[row].end), extend);
        desiredX_ = -1.0f;
        break;
    case kKeySelectAll: edit_.selectAll(); break;
    case kKeyCopy:
        if (edit_.hasSelection())
            ui.setClipboardText(edit_.selected());
        break;
    case kKeyCut:
        if (edit_.hasSelection() && editable())
        {
            ui.setClipboardText(edit_.selected());
            edit_.beginEdit(TextEditState::EditKind::kOther);
            edit_.eraseSelection();
            edited = true;
        }
        break;
    case kKeyPaste:
        if (editable())
        {
            edit_.beginEdit(TextEditState::EditKind::kOther);
            edit_.insert(ui.clipboardText());
            edited = true;
        }
        break;
    case kKeyEnter:
        if (editable())
        {
            edit_.beginEdit(TextEditState::EditKind::kTyping, "\n");
            edit_.insert("\n");
            edited = true;
        }
        break;
    case kKeyTab:
        if (editable())
        {
            edit_.beginEdit(TextEditState::EditKind::kTyping, "    ");
            edit_.insert("    ");
            edited = true;
        }
        break;
    case kKeyUndo: edited = editable() && edit_.undo(); break;
    case kKeyRedo: edited = editable() && edit_.redo(); break;
    case kKeyBackspace:
        if (editable() && (edit_.hasSelection() || edit_.caret > 0))
        {
            edit_.beginEdit(TextEditState::EditKind::kDeleting);
            if (!edit_.hasSelection())
                edit_.anchor = utf8Previous(edit_.text, edit_.caret);
            edit_.eraseSelection();
            edited = true;
        }
        break;
    case kKeyDelete:
        if (editable() && (edit_.hasSelection() || edit_.caret < edit_.text.size()))
        {
            edit_.beginEdit(TextEditState::EditKind::kDeleting);
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
    const float content = static_cast<float>(lines(ui, s).size()) * lh;
    const float maxScroll = std::max(0.0f, content - inset(rect_, s.padding).size.y);
    if (maxScroll <= 0.0f || wheel.y == 0.0f)
        return false;
    scroll_ = std::clamp(scroll_ - wheel.y * 3.0f * lh, 0.0f, maxScroll);
    return true;
}

glm::vec2 TextArea::measureContent(Ui &ui, glm::vec2, const Style &style)
{
    if (syncing_)
        syncFromBinding();
    // Six lignes et une quarantaine de caractères par défaut ; .width / .height pour plus.
    return {ui.font().measure(std::string(40, '0'), style.fontSize, style.font.get()).x, 6.0f * lineHeight(ui, style)};
}

void TextArea::arrange(Ui &ui, const Rect &content, const Style &style)
{
    Widget::arrange(ui, content, style);
    // Contenu raccourci (valeur liée remplacée, largeur changée) : le défilement revient dans la nouvelle plage.
    const float lh = lineHeight(ui, style);
    const float maxScroll = std::max(0.0f, static_cast<float>(lines(ui, style).size()) * lh - content.size.y);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
}

void TextArea::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect content = inset(rect_, style.padding);
    const float lh = lineHeight(ui, style);
    const float g = gutter(ui, style);
    const auto ls = lines(ui, style);
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
        const float x0 = std::round(content.position.x + g);
        auto xAt = [&](std::size_t pos) { return x0 + ui.font().measure(line.substr(0, pos - l.begin), style.fontSize, style.font.get()).x; };
        if (editing && s1 > s0 && s0 <= l.end && s1 >= l.begin)
        {
            const std::size_t a = std::max(s0, l.begin);
            const std::size_t b = std::min(s1, l.end);
            const float xa = xAt(a);
            const float xb = s1 > l.end ? xAt(l.end) + 4.0f : xAt(b); // la fin de ligne sélectionnée se voit
            list.rect({xa, y}, {std::max(xb - xa, 0.0f), lh}, selection);
        }
        if (lineNumbers_ && l.first)
        {
            drawText(ui, list, std::to_string(l.number + 1), {{content.position.x, y}, {g - style.spacing, lh}}, style, Align::kEnd, &style.muted);
        }
        if (!line.empty())
            ui.font().draw(list, line, {x0, std::round(y)}, style.fontSize, style.foreground, style.font.get());
        if (editing && edit_.caret >= l.begin && edit_.caret <= l.end && lineOf(ls, edit_.caret) == i && std::fmod(ui.time(), 1.0f) < 0.6f)
            list.rect({std::round(xAt(edit_.caret)), y}, {std::max(1.0f, style.borderWidth), lh}, style.foreground);
    }
    if (lineNumbers_) // séparation entre les numéros et le texte
        list.rect({content.position.x + g - style.spacing * 0.5f, content.position.y}, {std::max(1.0f, style.borderWidth), content.size.y}, style.border);
    list.popClip();
}

} // namespace orosmo::ui
