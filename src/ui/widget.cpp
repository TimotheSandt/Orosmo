#include "engine/ui/widget.hpp"

#include "engine/ui/nine_slice.hpp"
#include "engine/ui/ui.hpp"

#include "Logger.h"

#include <algorithm>
#include <cstdio>

namespace engine::ui
{

std::string detail::textPiece(double value)
{
    if (!std::isfinite(value))
    {
        return value != value ? "nan" : (value > 0 ? "inf" : "-inf");
    }
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
    std::string out = buffer;
    while (!out.empty() && out.back() == '0')
        out.pop_back();
    if (!out.empty() && out.back() == '.')
        out.pop_back();
    if (out == "-0")
        out = "0";
    return out;
}

Widget::Widget(std::string styleName) : styleName_(std::move(styleName)) {}
Widget::~Widget() = default;

void Widget::adopt(std::unique_ptr<Widget> child)
{
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void Widget::clearChildren() { children_.clear(); }

Widget *Widget::find(std::string_view id) noexcept
{
    if (id_ == id)
    {
        return this;
    }
    for (const auto &child : children_)
    {
        if (Widget *found = child->find(id))
        {
            return found;
        }
    }
    if (Widget *d = detached())
    {
        return d->find(id);
    }
    return nullptr;
}

Widget &Widget::setId(std::string id)
{
    id_ = std::move(id);
    return *this;
}

Widget &Widget::setStyleName(std::string name)
{
    styleName_ = std::move(name);
    return *this;
}

Widget &Widget::overrideStyle(StyleProperty property, std::string value)
{
    for (auto &[p, v] : overrides_)
    {
        if (p == property)
        {
            v = std::move(value);
            return *this;
        }
    }
    overrides_.emplace_back(property, std::move(value));
    return *this;
}

Style Widget::style(const Theme &theme) const
{
    Style s = theme.style(styleName_, state());
    for (const auto &[property, value] : overrides_)
    {
        auto resolved = theme.resolve(value);
        if (!resolved)
        {
            if (!overrideWarned_)
            {
                overrideWarned_ = true;
                LOG_WARNING("UI: style override ", std::string(propertyName(property)), " = '", value, "' on '", styleName_,
                            "' ignored: ", resolved.error().message);
            }
            continue;
        }
        s.set(property, *resolved);
    }
    return s;
}

bool Widget::visible() const { return visible_ && (!visibleIf_ || visibleIf_()); }

bool Widget::enabled() const
{
    for (const Widget *w = this; w != nullptr; w = w->parent_)
    {
        if (!w->enabled_ || (w->enabledIf_ && !w->enabledIf_()))
        {
            return false;
        }
    }
    return true;
}

WidgetState Widget::state() const
{
    if (!enabled())
        return WidgetState::kDisabled;
    if (pressed_ && hovered_)
        return WidgetState::kPressed;
    if (focused_)
        return WidgetState::kFocus;
    if (hovered_)
        return WidgetState::kHover;
    return WidgetState::kNormal;
}

glm::vec2 Widget::measure(Ui &ui, glm::vec2 available)
{
    if (!visible())
    {
        measured_ = {0.0f, 0.0f};
        return measured_;
    }
    const Style s = style(ui.theme());
    const float pad = s.padding;
    auto inner = [&](float avail, const SizeSpec &spec)
    {
        if (spec.mode == SizeMode::kFixed)
            return std::max(0.0f, spec.value - 2.0f * pad);
        return std::max(0.0f, std::min(avail, spec.max) - 2.0f * pad);
    };
    const glm::vec2 content = measureContent(ui, {inner(available.x, width), inner(available.y, height)}, s);
    auto axis = [&](float c, const SizeSpec &spec) { return spec.clamp(spec.mode == SizeMode::kFixed ? spec.value : c + 2.0f * pad); };
    measured_ = {axis(content.x, width), axis(content.y, height)};
    return measured_;
}

void Widget::layout(Ui &ui, const Rect &rect)
{
    rect_ = rect;
    if (!visible())
    {
        return;
    }
    const Style s = style(ui.theme());
    arrange(ui, inset(rect, s.padding), s);
}

void Widget::draw(const Ui &ui, renderer::DrawList &list) const
{
    if (!visible())
    {
        return;
    }
    const Style s = style(ui.theme());
    drawBackground(ui, list, s);
    if (overflow == Overflow::kHidden)
    {
        list.pushClip(rect_.position, rect_.size); // ce qui dépasse n'est pas dessiné
        drawContent(ui, list, s);
        list.popClip();
        return;
    }
    drawContent(ui, list, s);
}

void Widget::drawBackground(const Ui &, renderer::DrawList &list, const Style &style) const { drawBox(list, rect_, style); }

glm::vec2 Widget::measureContent(Ui &ui, glm::vec2 available, const Style &)
{
    glm::vec2 size{0.0f};
    for (const auto &child : children_)
    {
        const glm::vec2 m = child->measure(ui, available - child->margin.total()) + child->margin.total();
        size = glm::max(size, m);
    }
    return size;
}

void Widget::arrange(Ui &ui, const Rect &content, const Style &)
{
    for (const auto &child : children_)
    {
        child->layout(ui, {content.position + glm::vec2(child->margin.left, child->margin.top), content.size - child->margin.total()});
    }
}

void Widget::drawContent(const Ui &ui, renderer::DrawList &list, const Style &) const { drawChildren(ui, list); }

void Widget::drawChildren(const Ui &ui, renderer::DrawList &list) const
{
    for (const auto &child : children_)
    {
        child->draw(ui, list);
    }
}

void Widget::drawBox(renderer::DrawList &list, const Rect &r, const Style &style)
{
    if (style.shadowSize > 0.0f && style.shadowColor.a > 0.0f && (style.background.a > 0.0f || style.image))
    {
        const float s = style.shadowSize;
        list.rect(renderer::DrawRect{.position = r.position + glm::vec2(-s, -s + style.shadowOffset),
                                     .size = r.size + glm::vec2(2.0f * s),
                                     .color = style.shadowColor,
                                     .radius = style.radius + s,
                                     .softness = s});
    }
    if (style.background.a <= 0.0f && (style.borderWidth <= 0.0f || style.border.a <= 0.0f))
    {
        if (style.image)
        {
            appendNineSlice(list, *style.image, r, style.imageTint);
        }
        return;
    }
    renderer::DrawRect box{.position = r.position, .size = r.size, .color = style.background, .radius = style.radius};
    if (style.borderWidth > 0.0f && style.border.a > 0.0f)
    {
        box.borderWidth = style.borderWidth;
        box.borderColor = style.border;
    }
    list.rect(box);
    if (style.image)
    {
        appendNineSlice(list, *style.image, r, style.imageTint); // l'image par-dessus le fond (qui peut être transparent)
    }
}

void Widget::drawText(const Ui &ui, renderer::DrawList &list, std::string_view text, const Rect &r, const Style &style, Align align,
                      const Color *color)
{
    if (text.empty())
    {
        return;
    }
    const glm::vec2 size = ui.font().measure(text, style.fontSize, style.font.get());
    float x = r.position.x;
    if (align == Align::kCenter)
        x += (r.size.x - size.x) * 0.5f;
    else if (align == Align::kEnd)
        x += r.size.x - size.x;
    const float y = r.position.y + (r.size.y - size.y) * 0.5f;
    ui.font().draw(list, text, {std::round(x), std::round(y)}, style.fontSize, color != nullptr ? *color : style.foreground, style.font.get());
}

Rect Widget::inset(const Rect &r, float padding) noexcept
{
    return {r.position + glm::vec2(padding), glm::max(r.size - glm::vec2(2.0f * padding), glm::vec2(0.0f))};
}

void Widget::onPress(Ui &, glm::vec2) {}
void Widget::onDrag(Ui &, glm::vec2) {}
void Widget::onRelease(Ui &ui, glm::vec2, bool inside)
{
    if (inside && interactive())
    {
        emit(ui, EventKind::kClick);
    }
}
bool Widget::onSecondaryClick(Ui &, glm::vec2) { return false; }
bool Widget::openContextMenu(Ui &, glm::vec2) { return false; }
void Widget::onOutsideClick(Ui &) {}
void Widget::onHoverChanged(Ui &, bool) {}
bool Widget::onWheel(Ui &, glm::vec2) { return false; }
void Widget::onKey(Ui &, int) {}
void Widget::onText(Ui &, std::string_view) {}
bool Widget::onShortcut(Ui &, std::string_view) { return false; }
void Widget::onFocusChanged(Ui &ui, bool focused) { emit(ui, EventKind::kFocus, focused); }

void Widget::fire(EventKind kind, bool active)
{
    if (const Handler &h = handlers_[static_cast<std::size_t>(kind)])
    {
        h(Event{kind, this, active});
    }
}

void Widget::emit(Ui &ui, EventKind kind, bool active)
{
    const Handler &h = handlers_[static_cast<std::size_t>(kind)];
    if (!h)
    {
        return;
    }
    ui.defer(
        [this, alive = std::weak_ptr<char>(lifetime_), kind, active, h]
        {
            if (!alive.expired()) // la source a pu être détruite par un gestionnaire précédent de la même image
            {
                h(Event{kind, this, active});
            }
        });
}

} // namespace engine::ui
