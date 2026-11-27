#include "orosmo/ui/layout.hpp"

#include "orosmo/ui/ui.hpp"

#include <algorithm>
#include <numeric>

namespace orosmo::ui
{

namespace
{
// Position et taille d'un enfant sur un axe, dans `[start, start + avail)`, selon son alignement et sa taille demandée.
std::pair<float, float> place(Align align, const SizeSpec &spec, float start, float avail, float measured)
{
    float size = measured;
    if (spec.mode == SizeMode::kFill || (align == Align::kStretch && spec.mode == SizeMode::kFit))
    {
        size = spec.clamp(avail);
    }
    size = std::max(0.0f, size);
    switch (align)
    {
    case Align::kCenter: return {start + (avail - size) * 0.5f, size};
    case Align::kEnd: return {start + avail - size, size};
    default: return {start, size};
    }
}

int mainIndex(Axis axis) { return axis == Axis::kVertical ? 1 : 0; }
} // namespace

// ── Box ──────────────────────────────────────────────────────────────────────────────────────────────────────────────

Box::Box(Axis axis, std::string styleName) : Widget(std::move(styleName)), axis_(axis) {}

glm::vec2 Box::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    const int m = mainIndex(axis_);
    const int c = 1 - m;
    float mainSum = 0.0f;
    float crossMax = 0.0f;
    int count = 0;
    for (const auto &child : children())
    {
        if (!child->visible())
        {
            continue;
        }
        const glm::vec2 margins = child->margin.total();
        glm::vec2 avail = available - margins;
        const glm::vec2 size = child->measure(ui, avail) + margins;
        mainSum += size[m];
        crossMax = std::max(crossMax, size[c]);
        ++count;
    }
    if (count > 1)
    {
        mainSum += style.spacing * static_cast<float>(count - 1);
    }
    glm::vec2 out;
    out[m] = mainSum;
    out[c] = crossMax;
    return out;
}

void Box::arrange(Ui &ui, const Rect &content, const Style &style)
{
    const int m = mainIndex(axis_);
    const int c = 1 - m;
    std::vector<Widget *> visible;
    for (const auto &child : children())
    {
        if (child->visible())
        {
            visible.push_back(child.get());
        }
    }
    if (visible.empty())
    {
        return;
    }
    auto specOf = [&](const Widget &w, int axis) -> const SizeSpec & { return axis == 0 ? w.width : w.height; };
    float fixed = style.spacing * static_cast<float>(visible.size() - 1);
    float weights = 0.0f;
    for (Widget *w : visible)
    {
        const glm::vec2 margins = w->margin.total();
        const SizeSpec &spec = specOf(*w, m);
        fixed += margins[m];
        if (spec.mode == SizeMode::kFill)
        {
            weights += std::max(spec.value, 0.0f);
        }
        else
        {
            fixed += w->measured()[m];
        }
    }
    const float remaining = std::max(0.0f, content.size[m] - fixed);

    float cursor = content.position[m];
    for (Widget *w : visible)
    {
        const SizeSpec &mainSpec = specOf(*w, m);
        const SizeSpec &crossSpec = specOf(*w, c);
        const float lead = m == 1 ? w->margin.top : w->margin.left;
        const float trail = m == 1 ? w->margin.bottom : w->margin.right;
        const float crossLead = c == 1 ? w->margin.top : w->margin.left;
        float mainSize = w->measured()[m];
        if (mainSpec.mode == SizeMode::kFill)
        {
            mainSize = weights > 0.0f ? mainSpec.clamp(remaining * std::max(mainSpec.value, 0.0f) / weights) : mainSpec.min;
        }
        const Align crossAlign = c == 0 ? w->alignX : w->alignY;
        const float crossAvail = content.size[c] - w->margin.total()[c];
        auto [crossPos, crossSize] = place(crossAlign, crossSpec, content.position[c] + crossLead, crossAvail, w->measured()[c]);
        Rect r;
        r.position[m] = cursor + lead;
        r.size[m] = mainSize;
        r.position[c] = crossPos;
        r.size[c] = crossSize;
        w->layout(ui, r);
        cursor += lead + mainSize + trail + style.spacing;
    }
}

// ── Grid ─────────────────────────────────────────────────────────────────────────────────────────────────────────────

Grid::Grid(int columns, std::string styleName) : Widget(std::move(styleName)), columns_(std::max(columns, 1)) {}

std::vector<Grid::Cell> Grid::cells() const
{
    std::vector<Cell> out;
    int row = 0;
    int column = 0;
    for (const auto &child : children())
    {
        if (!child->visible())
        {
            continue;
        }
        const int span = std::clamp(child->columnSpan, 1, columns_);
        if (column + span > columns_)
        {
            ++row;
            column = 0;
        }
        out.push_back({child.get(), row, column, span});
        column += span;
        if (column >= columns_)
        {
            ++row;
            column = 0;
        }
    }
    return out;
}

void Grid::computeTracks(Ui &ui, glm::vec2 available, const Style &style)
{
    const std::vector<Cell> all = cells();
    const int rows = all.empty() ? 0 : all.back().row + 1;
    columnWidths_.assign(static_cast<std::size_t>(columns_), 0.0f);
    rowHeights_.assign(static_cast<std::size_t>(rows), 0.0f);
    fillColumns_.assign(static_cast<std::size_t>(columns_), false);
    for (const Cell &cell : all)
    {
        const glm::vec2 margins = cell.widget->margin.total();
        const glm::vec2 size = cell.widget->measure(ui, glm::vec2(available.x, kUnbounded) - margins) + margins;
        rowHeights_[static_cast<std::size_t>(cell.row)] = std::max(rowHeights_[static_cast<std::size_t>(cell.row)], size.y);
        if (cell.span == 1)
        {
            auto &w = columnWidths_[static_cast<std::size_t>(cell.column)];
            w = std::max(w, size.x);
            if (cell.widget->width.mode == SizeMode::kFill)
            {
                fillColumns_[static_cast<std::size_t>(cell.column)] = true;
            }
        }
    }
    // Un enfant sur plusieurs colonnes plus large qu'elles : l'excédent va à la dernière.
    for (const Cell &cell : all)
    {
        if (cell.span == 1)
        {
            continue;
        }
        // Enfant `fill` sur plusieurs colonnes dont aucune n'est extensible : ses colonnes le deviennent (l'excédent s'y répartit).
        if (cell.widget->width.mode == SizeMode::kFill)
        {
            bool any = false;
            for (int k = 0; k < cell.span; ++k)
                any = any || fillColumns_[static_cast<std::size_t>(cell.column + k)];
            for (int k = 0; !any && k < cell.span; ++k)
                fillColumns_[static_cast<std::size_t>(cell.column + k)] = true;
        }
        float spanned = style.spacing * static_cast<float>(cell.span - 1);
        for (int k = 0; k < cell.span; ++k)
        {
            spanned += columnWidths_[static_cast<std::size_t>(cell.column + k)];
        }
        const float wanted = cell.widget->measured().x + cell.widget->margin.horizontal();
        if (wanted > spanned)
        {
            columnWidths_[static_cast<std::size_t>(cell.column + cell.span - 1)] += wanted - spanned;
        }
    }
}

glm::vec2 Grid::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    computeTracks(ui, available, style);
    const float gap = style.spacing;
    const float w = std::accumulate(columnWidths_.begin(), columnWidths_.end(), 0.0f) + gap * static_cast<float>(columns_ - 1);
    const float h = rowHeights_.empty() ? 0.0f
                                        : std::accumulate(rowHeights_.begin(), rowHeights_.end(), 0.0f) +
                                              gap * static_cast<float>(rowHeights_.size() - 1);
    return {w, h};
}

void Grid::arrange(Ui &ui, const Rect &content, const Style &style)
{
    const float gap = style.spacing;
    std::vector<float> widths = columnWidths_;
    const float used = std::accumulate(widths.begin(), widths.end(), 0.0f) + gap * static_cast<float>(columns_ - 1);
    const auto fills = static_cast<float>(std::count(fillColumns_.begin(), fillColumns_.end(), true));
    if (fills > 0.0f && content.size.x > used)
    {
        const float extra = (content.size.x - used) / fills;
        for (std::size_t i = 0; i < widths.size(); ++i)
        {
            widths[i] += fillColumns_[i] ? extra : 0.0f;
        }
    }
    std::vector<float> xs(widths.size(), content.position.x);
    for (std::size_t i = 1; i < widths.size(); ++i)
    {
        xs[i] = xs[i - 1] + widths[i - 1] + gap;
    }
    std::vector<float> ys(rowHeights_.size(), content.position.y);
    for (std::size_t i = 1; i < rowHeights_.size(); ++i)
    {
        ys[i] = ys[i - 1] + rowHeights_[i - 1] + gap;
    }
    for (const Cell &cell : cells())
    {
        if (static_cast<std::size_t>(cell.row) >= rowHeights_.size())
        {
            continue; // l'arbre a changé depuis la mesure : placé à la prochaine image
        }
        Widget &w = *cell.widget;
        float cellWidth = gap * static_cast<float>(cell.span - 1);
        for (int k = 0; k < cell.span; ++k)
        {
            cellWidth += widths[static_cast<std::size_t>(cell.column + k)];
        }
        const float cellHeight = rowHeights_[static_cast<std::size_t>(cell.row)];
        auto [x, wd] = place(w.alignX, w.width, xs[static_cast<std::size_t>(cell.column)] + w.margin.left, cellWidth - w.margin.horizontal(),
                             w.measured().x);
        auto [y, ht] = place(w.alignY, w.height, ys[static_cast<std::size_t>(cell.row)] + w.margin.top, cellHeight - w.margin.vertical(),
                             w.measured().y);
        w.layout(ui, {{x, y}, {wd, ht}});
    }
}

// ── Overlay ──────────────────────────────────────────────────────────────────────────────────────────────────────────

glm::vec2 Overlay::measureContent(Ui &ui, glm::vec2 available, const Style &)
{
    glm::vec2 size{0.0f};
    for (const auto &child : children())
    {
        if (child->visible())
        {
            size = glm::max(size, child->measure(ui, available - child->margin.total()) + child->margin.total());
        }
    }
    return size;
}

void Overlay::arrange(Ui &ui, const Rect &content, const Style &)
{
    for (const auto &child : children())
    {
        if (!child->visible())
        {
            continue;
        }
        Widget &w = *child;
        auto [x, wd] = place(w.alignX, w.width, content.position.x + w.margin.left, content.size.x - w.margin.horizontal(), w.measured().x);
        auto [y, ht] = place(w.alignY, w.height, content.position.y + w.margin.top, content.size.y - w.margin.vertical(), w.measured().y);
        w.layout(ui, {{x, y}, {wd, ht}});
    }
}

// ── ScrollView ───────────────────────────────────────────────────────────────────────────────────────────────────────

void ScrollView::setScroll(float y) noexcept { scroll_ = std::clamp(y, 0.0f, maxScroll_); }

void ScrollView::saveRetained(RetainedState &state) const { state.scroll = scroll_; }

// Avant la première mise en page, la borne n'est pas connue : `arrange` ramène la valeur dans [0, maxScroll].
void ScrollView::restoreRetained(Ui &, const RetainedState &state)
{
    if (state.scroll)
        scroll_ = std::max(0.0f, *state.scroll);
}

glm::vec2 ScrollView::measureContent(Ui &ui, glm::vec2 available, const Style &style)
{
    glm::vec2 size{0.0f};
    for (const auto &child : children())
    {
        if (child->visible())
        {
            const glm::vec2 margins = child->margin.total();
            size = glm::max(size, child->measure(ui, glm::vec2(available.x - style.width, kUnbounded) - margins) + margins);
        }
    }
    contentHeight_ = size.y;
    return {size.x + style.width, std::min(size.y, available.y)};
}

void ScrollView::arrange(Ui &ui, const Rect &content, const Style &style)
{
    viewport_ = content;
    maxScroll_ = std::max(0.0f, contentHeight_ - content.size.y);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll_);
    barWidth_ = maxScroll_ > 0.0f ? style.width : 0.0f;
    for (const auto &child : children())
    {
        if (child->visible())
        {
            child->layout(ui, {{content.position.x + child->margin.left, content.position.y - scroll_ + child->margin.top},
                               {content.size.x - barWidth_ - child->margin.horizontal(), contentHeight_ - child->margin.vertical()}});
        }
    }
}

Rect ScrollView::thumb() const
{
    if (maxScroll_ <= 0.0f || contentHeight_ <= 0.0f)
    {
        return {};
    }
    // Taille minimale réduite dans une vue courte : le curseur garde toujours une course (il ne dépasse pas de la vue).
    const float minimum = std::min(barWidth_ * 2.0f, viewport_.size.y * 0.5f);
    const float h = std::max(minimum, viewport_.size.y * viewport_.size.y / contentHeight_);
    const float y = viewport_.position.y + (viewport_.size.y - h) * (scroll_ / maxScroll_);
    return {{viewport_.end().x - barWidth_, y}, {barWidth_, h}};
}

void ScrollView::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    list.pushClip(viewport_.position, viewport_.size);
    drawChildren(ui, list);
    list.popClip();
    if (maxScroll_ > 0.0f)
    {
        const Rect t = thumb();
        list.rect(renderer::DrawRect{.position = t.position, .size = t.size, .color = style.accent, .radius = barWidth_ * 0.5f});
    }
}

bool ScrollView::onWheel(Ui &ui, glm::vec2 wheel)
{
    if (maxScroll_ <= 0.0f || wheel.y == 0.0f)
    {
        return false;
    }
    // Un cran = trois lignes de texte du thème.
    setScroll(scroll_ - wheel.y * 3.0f * style(ui.theme()).fontSize);
    return true;
}

void ScrollView::onPress(Ui &ui, glm::vec2 mouse)
{
    draggingBar_ = barWidth_ > 0.0f && mouse.x >= viewport_.end().x - barWidth_;
    if (!draggingBar_)
    {
        return;
    }
    const Rect t = thumb();
    dragOffset_ = t.contains(mouse) ? mouse.y - t.position.y : t.size.y * 0.5f;
    onDrag(ui, mouse);
}

void ScrollView::onDrag(Ui &, glm::vec2 mouse)
{
    if (!draggingBar_)
    {
        return;
    }
    const Rect t = thumb();
    const float travel = viewport_.size.y - t.size.y;
    if (travel > 0.0f)
    {
        setScroll((mouse.y - dragOffset_ - viewport_.position.y) / travel * maxScroll_);
    }
}

} // namespace orosmo::ui
