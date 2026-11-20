#include "orosmo/ui/color.hpp"

#include "orosmo/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace orosmo::ui
{

// ── Conversions ──────────────────────────────────────────────────────────────────────────────────────────────────────

glm::vec3 rgbToHsv(glm::vec3 c) noexcept
{
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 0.0f)
    {
        if (mx == c.r)
            h = std::fmod((c.g - c.b) / d, 6.0f);
        else if (mx == c.g)
            h = (c.b - c.r) / d + 2.0f;
        else
            h = (c.r - c.g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f)
            h += 1.0f;
    }
    return {h, mx > 0.0f ? d / mx : 0.0f, mx};
}

glm::vec3 hsvToRgb(glm::vec3 hsv) noexcept
{
    const float h = (hsv.x - std::floor(hsv.x)) * 6.0f;
    const float s = std::clamp(hsv.y, 0.0f, 1.0f);
    const float v = std::clamp(hsv.z, 0.0f, 1.0f);
    const float c = v * s;
    const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    glm::vec3 rgb;
    switch (static_cast<int>(h) % 6)
    {
    case 0: rgb = {c, x, 0}; break;
    case 1: rgb = {x, c, 0}; break;
    case 2: rgb = {0, c, x}; break;
    case 3: rgb = {0, x, c}; break;
    case 4: rgb = {x, 0, c}; break;
    default: rgb = {c, 0, x}; break;
    }
    return rgb + glm::vec3(v - c);
}

glm::vec3 rgbToHsl(glm::vec3 c) noexcept
{
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float l = (mx + mn) * 0.5f;
    const float d = mx - mn;
    const float s = d <= 0.0f ? 0.0f : d / (1.0f - std::abs(2.0f * l - 1.0f));
    return {rgbToHsv(c).x, std::clamp(s, 0.0f, 1.0f), l};
}

glm::vec3 hslToRgb(glm::vec3 hsl) noexcept
{
    const float l = std::clamp(hsl.z, 0.0f, 1.0f);
    const float s = std::clamp(hsl.y, 0.0f, 1.0f);
    const float v = l + s * std::min(l, 1.0f - l);
    const float sv = v <= 0.0f ? 0.0f : 2.0f * (1.0f - l / v);
    return hsvToRgb({hsl.x, sv, v});
}

std::string toHex(const Color &c, bool alpha)
{
    auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char buffer[16];
    if (alpha)
        std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b), byte(c.a));
    else
        std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b));
    return buffer;
}

std::optional<Color> fromHex(std::string_view text) noexcept
{
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return std::nullopt;
    std::string withHash = "#" + std::string(text);
    return parseColor(withHash);
}

// ── Dégradé, courbe ──────────────────────────────────────────────────────────────────────────────────────────────────

void Gradient::sort()
{
    std::stable_sort(stops.begin(), stops.end(), [](const GradientStop &a, const GradientStop &b) { return a.position < b.position; });
}

Color Gradient::evaluate(float t) const
{
    if (stops.empty())
        return Color(0.0f);
    std::vector<GradientStop> s = stops;
    std::stable_sort(s.begin(), s.end(), [](const GradientStop &a, const GradientStop &b) { return a.position < b.position; });
    if (t <= s.front().position)
        return s.front().color;
    for (std::size_t i = 1; i < s.size(); ++i)
    {
        if (t <= s[i].position)
        {
            const float span = s[i].position - s[i - 1].position;
            const float f = span > 0.0f ? (t - s[i - 1].position) / span : 1.0f;
            return glm::mix(s[i - 1].color, s[i].color, f);
        }
    }
    return s.back().color;
}

float Curve::evaluate(float x) const
{
    if (points.empty())
        return x;
    std::vector<glm::vec2> p = points;
    std::stable_sort(p.begin(), p.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; });
    if (p.size() == 1 || x <= p.front().x)
        return p.front().y;
    if (x >= p.back().x)
        return p.back().y;
    std::size_t i = 1;
    while (i < p.size() && x > p[i].x)
        ++i;
    const glm::vec2 a = p[i - 1];
    const glm::vec2 b = p[i];
    const float h = b.x - a.x;
    if (h <= 0.0f)
        return b.y;
    const float t = (x - a.x) / h;
    if (!smooth)
        return a.y + (b.y - a.y) * t;
    // Cubique d'Hermite à pentes de Fritsch-Carlson : monotone entre deux points, pas de dépassement.
    const std::size_t n = p.size();
    std::vector<float> delta(n - 1), m(n);
    for (std::size_t k = 0; k + 1 < n; ++k)
    {
        const float dx = p[k + 1].x - p[k].x;
        delta[k] = dx > 0.0f ? (p[k + 1].y - p[k].y) / dx : 0.0f;
    }
    m[0] = delta[0];
    m[n - 1] = delta[n - 2];
    for (std::size_t k = 1; k + 1 < n; ++k)
        m[k] = delta[k - 1] * delta[k] <= 0.0f ? 0.0f : (delta[k - 1] + delta[k]) * 0.5f;
    for (std::size_t k = 0; k + 1 < n; ++k)
    {
        if (delta[k] == 0.0f)
        {
            m[k] = m[k + 1] = 0.0f;
            continue;
        }
        const float alpha = m[k] / delta[k];
        const float beta = m[k + 1] / delta[k];
        const float r = alpha * alpha + beta * beta;
        if (r > 9.0f)
        {
            const float tau = 3.0f / std::sqrt(r);
            m[k] = tau * alpha * delta[k];
            m[k + 1] = tau * beta * delta[k];
        }
    }
    const float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.y + (t3 - 2 * t2 + t) * h * m[i - 1] + (-2 * t3 + 3 * t2) * b.y + (t3 - t2) * h * m[i];
}

namespace
{

void quad(std::vector<float> &v, glm::vec2 p0, glm::vec2 p1, glm::vec2 uv0 = {0, 0}, glm::vec2 uv1 = {1, 1})
{
    const float q[] = {p0.x, p0.y, uv0.x, uv0.y, p0.x, p1.y, uv0.x, uv1.y, p1.x, p1.y, uv1.x, uv1.y,
                       p0.x, p0.y, uv0.x, uv0.y, p1.x, p1.y, uv1.x, uv1.y, p1.x, p0.y, uv1.x, uv0.y};
    v.insert(v.end(), std::begin(q), std::end(q));
}

// Pastille ronde avec un contour (repère sur une roue, point d'une courbe, arrêt d'un dégradé).
void marker(renderer::DrawList &list, glm::vec2 center, float size, const Color &fill, const Color &ring)
{
    list.rect(renderer::DrawRect{.position = center - glm::vec2(size * 0.5f), .size = glm::vec2(size), .color = fill, .radius = size * 0.5f,
                                 .borderWidth = std::max(1.5f, size * 0.18f), .borderColor = ring});
}

// Petit carré de couleur (aperçu) : sans entrée, juste dessiné.
class Swatch final : public Widget
{
  public:
    explicit Swatch(std::function<Color()> color) : Widget("colorPicker"), color_(std::move(color)) {}

  protected:
    glm::vec2 measureContent(Ui &, glm::vec2, const Style &style) override { return glm::vec2(style.width); }
    void drawBackground(const Ui &, renderer::DrawList &list, const Style &style) const override
    {
        Style s = style;
        s.background = color_();
        drawBox(list, rect_, s);
    }
    void drawContent(const Ui &, renderer::DrawList &, const Style &) const override {}

  private:
    std::function<Color()> color_;
};
} // namespace

// ── ColorWheel ───────────────────────────────────────────────────────────────────────────────────────────────────────

ColorWheel::ColorWheel(Binding<Color> value) : Widget("colorWheel"), value_(std::move(value))
{
    hue_ = rgbToHsv(glm::vec3(value_.value())).x;
}

Color ColorWheel::color() const { return pending_ ? *pending_ : value_.value(); }

ColorWheel::Geometry ColorWheel::geometry() const
{
    Geometry g;
    g.center = rect_.position + rect_.size * 0.5f;
    g.outer = std::min(rect_.size.x, rect_.size.y) * 0.5f;
    g.inner = g.outer * 0.8f;
    const float side = g.inner * 1.3f; // carré inscrit dans l'anneau, avec une marge
    g.square = {g.center - glm::vec2(side * 0.5f), glm::vec2(side)};
    return g;
}

glm::vec2 ColorWheel::measureContent(Ui &, glm::vec2, const Style &style)
{
    // Couleur réglée ailleurs (curseurs, champ hexadécimal) : la roue suit ; pour un gris, la teinte est gardée (pas de saut).
    const glm::vec3 hsv = rgbToHsv(glm::vec3(color()));
    if (hsv.y > 0.001f && hsv.z > 0.001f)
        hue_ = hsv.x;
    return glm::vec2(style.width * 8.0f);
}

void ColorWheel::refreshImages(int size) const
{
    if (!ring_)
    {
        // Anneau des teintes : une fois (bord adouci sur un texel, fond transparent).
        ring_ = std::make_shared<renderer::AtlasImage>();
        ring_->id = renderer::newAtlasId();
        ring_->width = ring_->height = static_cast<std::uint32_t>(size);
        ring_->rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
        const float c = static_cast<float>(size) * 0.5f;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - c, dy = static_cast<float>(y) + 0.5f - c;
                const float r = std::sqrt(dx * dx + dy * dy) / c;
                const float a = std::clamp((1.0f - r) * c, 0.0f, 1.0f) * std::clamp((r - 0.8f) * c, 0.0f, 1.0f);
                if (a <= 0.0f)
                    continue;
                float h = std::atan2(dy, dx) / (2.0f * std::numbers::pi_v<float>);
                if (h < 0.0f)
                    h += 1.0f;
                const glm::vec3 rgb = hsvToRgb({h, 1.0f, 1.0f});
                std::uint8_t *p = &ring_->rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
                p[0] = static_cast<std::uint8_t>(rgb.r * 255.0f);
                p[1] = static_cast<std::uint8_t>(rgb.g * 255.0f);
                p[2] = static_cast<std::uint8_t>(rgb.b * 255.0f);
                p[3] = static_cast<std::uint8_t>(a * 255.0f);
            }
    }
    if (squareHue_ != hue_)
    {
        // Carré saturation (x) × valeur (y vers le haut) de la teinte courante : refait quand la teinte change (même texture).
        constexpr int kSquare = 64;
        if (!square_)
        {
            square_ = std::make_shared<renderer::AtlasImage>();
            square_->id = renderer::newAtlasId();
            square_->width = square_->height = kSquare;
            square_->rgba.assign(kSquare * kSquare * 4, 255);
        }
        for (int y = 0; y < kSquare; ++y)
            for (int x = 0; x < kSquare; ++x)
            {
                const glm::vec3 rgb = hsvToRgb({hue_, static_cast<float>(x) / (kSquare - 1), 1.0f - static_cast<float>(y) / (kSquare - 1)});
                std::uint8_t *p = &square_->rgba[(static_cast<std::size_t>(y) * kSquare + static_cast<std::size_t>(x)) * 4];
                p[0] = static_cast<std::uint8_t>(std::lround(rgb.r * 255.0f));
                p[1] = static_cast<std::uint8_t>(std::lround(rgb.g * 255.0f));
                p[2] = static_cast<std::uint8_t>(std::lround(rgb.b * 255.0f));
            }
        ++square_->version;
        squareHue_ = hue_;
    }
}

void ColorWheel::drawContent(const Ui &, renderer::DrawList &list, const Style &style) const
{
    const Geometry g = geometry();
    if (g.outer <= 2.0f)
        return;
    const Color current = color();
    const glm::vec3 hsv = rgbToHsv(glm::vec3(current));
    refreshImages(256);
    std::vector<float> ring, square;
    quad(ring, g.center - glm::vec2(g.outer), g.center + glm::vec2(g.outer));
    const float half = 0.5f / 64.0f;
    quad(square, g.square.position, g.square.end(), {half, half}, {1.0f - half, 1.0f - half});
    list.image(renderer::DrawImage{.image = ring_, .vertices = std::move(ring), .tint = Color(1.0f)});
    list.image(renderer::DrawImage{.image = square_, .vertices = std::move(square), .tint = Color(1.0f)});
    const float angle = hue_ * 2.0f * std::numbers::pi_v<float>;
    const float mid = (g.outer + g.inner) * 0.5f;
    const float size = std::max(8.0f, (g.outer - g.inner) * 0.9f);
    marker(list, g.center + glm::vec2(std::cos(angle), std::sin(angle)) * mid, size, Color(hsvToRgb({hue_, 1, 1}), 1.0f), style.foreground);
    marker(list, g.square.position + glm::vec2(hsv.y, 1.0f - hsv.z) * g.square.size, size, Color(glm::vec3(current), 1.0f), style.foreground);
}

void ColorWheel::pick(Ui &ui, glm::vec2 mouse)
{
    const Geometry g = geometry();
    const Color current = color();
    glm::vec3 hsv = rgbToHsv(glm::vec3(current));
    hsv.x = hue_;
    if (part_ == 1)
    {
        float h = std::atan2(mouse.y - g.center.y, mouse.x - g.center.x) / (2.0f * std::numbers::pi_v<float>);
        hue_ = h < 0.0f ? h + 1.0f : h;
        hsv.x = hue_;
    }
    else if (part_ == 2)
    {
        const glm::vec2 t = glm::clamp((mouse - g.square.position) / g.square.size, glm::vec2(0.0f), glm::vec2(1.0f));
        hsv.y = t.x;
        hsv.z = 1.0f - t.y;
    }
    else
    {
        return;
    }
    const Color next(hsvToRgb(hsv), current.a);
    if (!value_.writable() || next == current)
        return;
    pending_ = next;
    ui.defer([this, next] {
        value_.set(next);
        pending_.reset();
    });
    emit(ui, EventKind::kChange);
}

void ColorWheel::onPress(Ui &ui, glm::vec2 mouse)
{
    const Geometry g = geometry();
    const float r = glm::length(mouse - g.center);
    part_ = r >= g.inner * 0.95f && r <= g.outer ? 1 : (g.square.contains(mouse) ? 2 : 0);
    pick(ui, mouse);
}

void ColorWheel::onDrag(Ui &ui, glm::vec2 mouse) { pick(ui, mouse); }

// ── ColorEditor ──────────────────────────────────────────────────────────────────────────────────────────────────────

namespace
{
class FormatChannels final : public VBox
{
  public:
    using VBox::VBox;
    std::function<void()> beforeMeasure;

  protected:
    glm::vec2 measureContent(Ui &ui, glm::vec2 available, const Style &style) override
    {
        if (beforeMeasure)
            beforeMeasure();
        return VBox::measureContent(ui, available, style);
    }
};
} // namespace

ColorEditor::ColorEditor(Binding<Color> value, bool alpha) : VBox("box"), value_(std::move(value)), alpha_(alpha)
{
    original_ = value_.value();
    auto &top = emplace<HBox>();
    top.emplace<ColorWheel>(value_).onChange([this](const Event &) { fire(EventKind::kChange); });
    auto &side = top.emplace<VBox>();
    side.setWidth(SizeSpec::fill());
    side.emplace<Label>("Format", "caption");
    auto &fmt = side.emplace<Dropdown>(std::vector<std::string>{"RGB", "HSV", "HSL"}, Binding<int>::ref(formatIndex_));
    fmt.setId("format");
    fmt.setWidth(SizeSpec::fill());
    side.emplace<Label>("Hex", "caption");
    Binding<std::string> hex{[this] { return toHex(value_.value(), alpha_); },
                             value_.writable() ? std::function<void(const std::string &)>([this](const std::string &t) {
                                 if (auto c = fromHex(t))
                                 {
                                     Color next = *c;
                                     if (!alpha_)
                                         next.a = value_.value().a;
                                     if (next != value_.value()) // saisie invalide ou couleur identique : aucun événement
                                     {
                                         value_.set(next);
                                         fire(EventKind::kChange);
                                     }
                                 }
                             })
                                               : std::function<void(const std::string &)>()};
    side.emplace<TextField>(std::move(hex)).setId("hex").setWidth(SizeSpec::fill());
    auto &previews = side.emplace<HBox>();
    previews.emplace<Swatch>([this] { return original_; }).setTooltip("original colour");
    previews.emplace<Swatch>([this] { return value_.value(); }).setTooltip("new colour");
    auto channels = std::make_unique<FormatChannels>();
    channels->beforeMeasure = [this] {
        if (static_cast<int>(format_) != formatIndex_)
        {
            format_ = static_cast<ColorFormat>(formatIndex_);
            rebuildChannels();
        }
    };
    channels_ = &add(std::move(channels));
    rebuildChannels();
}

void ColorEditor::rebuildChannels()
{
    channels_->clearChildren();
    struct Spec
    {
        const char *prefix;
        double max;
    };
    const Spec rgb[] = {{"R ", 255}, {"G ", 255}, {"B ", 255}};
    const Spec hsv[] = {{"H ", 360}, {"S ", 100}, {"V ", 100}};
    const Spec hsl[] = {{"H ", 360}, {"S ", 100}, {"L ", 100}};
    const Spec *specs = format_ == ColorFormat::kRgb ? rgb : (format_ == ColorFormat::kHsv ? hsv : hsl);
    const ColorFormat format = format_;
    auto toSpace = [format](const Color &c) {
        const glm::vec3 v(c);
        return format == ColorFormat::kRgb ? v : (format == ColorFormat::kHsv ? rgbToHsv(v) : rgbToHsl(v));
    };
    auto fromSpace = [format](glm::vec3 v) {
        return format == ColorFormat::kRgb ? v : (format == ColorFormat::kHsv ? hsvToRgb(v) : hslToRgb(v));
    };
    for (int i = 0; i < 3; ++i)
    {
        const double max = specs[i].max;
        Binding<double> b;
        // Teinte d'un gris (saturation nulle) : le RGB ne la contient pas ; celle choisie est gardée dans hue_ pour que la
        // saturation, ajoutée ensuite, parte de cette teinte et non du rouge.
        const bool hueSpace = format != ColorFormat::kRgb;
        b.get = [this, toSpace, i, max, hueSpace] {
            const glm::vec3 s = toSpace(value_.value());
            if (hueSpace && i == 0)
            {
                if (s[1] <= 0.0f)
                    return static_cast<double>(hue_) * max;
                hue_ = s[0];
            }
            return static_cast<double>(s[i]) * max;
        };
        if (value_.writable())
            b.set = [this, toSpace, fromSpace, i, max, hueSpace](const double &v) {
                const Color c = value_.value();
                glm::vec3 s = toSpace(c);
                if (hueSpace && s[1] <= 0.0f)
                    s[0] = hue_;
                s[i] = static_cast<float>(v / max);
                if (hueSpace && i == 0)
                    hue_ = s[0];
                value_.set(Color(fromSpace(s), c.a));
            };
        channels_->emplace<Slider>(std::move(b), 0.0, max, 1.0).setPrefix(specs[i].prefix).setWidth(SizeSpec::fill());
    }
    if (alpha_)
    {
        Binding<double> a{[this] { return static_cast<double>(value_.value().a) * 255.0; },
                          value_.writable() ? std::function<void(const double &)>([this](const double &v) {
                              Color c = value_.value();
                              c.a = static_cast<float>(v / 255.0);
                              value_.set(c);
                          })
                                            : std::function<void(const double &)>()};
        channels_->emplace<Slider>(std::move(a), 0.0, 255.0, 1.0).setPrefix("A ").setWidth(SizeSpec::fill());
    }
    for (const auto &child : channels_->children())
        child->onChange([this](const Event &) { fire(EventKind::kChange); });
}

// ── GradientEditor ───────────────────────────────────────────────────────────────────────────────────────────────────

class GradientBar final : public Widget
{
  public:
    explicit GradientBar(GradientEditor &owner) : Widget("gradient"), owner_(&owner) {}
    bool interactive() const override { return true; }

    void onPress(Ui &ui, glm::vec2 mouse) override
    {
        Gradient g = current();
        const int i = stopAt(ui, g, mouse);
        if (i >= 0)
        {
            owner_->selected_ = i;
            dragging_ = i;
            return;
        }
        if (!owner_->value_.writable())
            return;
        const float t = positionAt(ui, mouse.x);
        g.stops.push_back({t, g.evaluate(t)});
        owner_->selected_ = dragging_ = static_cast<int>(g.stops.size()) - 1;
        write(ui, g);
    }

    void onDrag(Ui &ui, glm::vec2 mouse) override
    {
        Gradient g = current();
        if (dragging_ < 0 || dragging_ >= static_cast<int>(g.stops.size()))
            return;
        g.stops[static_cast<std::size_t>(dragging_)].position = positionAt(ui, mouse.x);
        write(ui, g);
    }

    void onRelease(Ui &, glm::vec2, bool) override { dragging_ = -1; }

    bool onSecondaryClick(Ui &ui, glm::vec2 mouse) override
    {
        Gradient g = current();
        const int i = stopAt(ui, g, mouse);
        if (i < 0 || g.stops.size() <= 1 || !owner_->value_.writable())
            return i >= 0;
        g.stops.erase(g.stops.begin() + i);
        owner_->selected_ = std::clamp(owner_->selected_ - (owner_->selected_ >= i ? 1 : 0), 0, static_cast<int>(g.stops.size()) - 1);
        write(ui, g);
        return true;
    }

  protected:
    glm::vec2 measureContent(Ui &, glm::vec2, const Style &style) override { return {style.width * 10.0f, style.width * 2.0f}; }
    void drawBackground(const Ui &, renderer::DrawList &, const Style &) const override {}

    void drawContent(const Ui &, renderer::DrawList &list, const Style &style) const override
    {
        const Gradient g = current();
        const Rect bar = barRect(style);
        const int columns = std::max(1, static_cast<int>(bar.size.x));
        for (int x = 0; x < columns; ++x)
        {
            const float t = (static_cast<float>(x) + 0.5f) / static_cast<float>(columns);
            list.rect({bar.position.x + static_cast<float>(x), bar.position.y}, {1.0f, bar.size.y}, g.evaluate(t));
        }
        Style frame = style;
        frame.background = Color(0.0f);
        drawBox(list, bar, frame); // bordure du thème autour de la barre
        const float size = style.width * 0.7f;
        for (std::size_t i = 0; i < g.stops.size(); ++i)
        {
            const float x = bar.position.x + g.stops[i].position * bar.size.x;
            const bool chosen = static_cast<int>(i) == owner_->selected_;
            marker(list, {x, bar.end().y + size * 0.6f}, chosen ? size * 1.25f : size, Color(glm::vec3(g.stops[i].color), 1.0f),
                   chosen ? style.accent : style.foreground);
        }
    }

  private:
    Gradient current() const { return pending_ ? *pending_ : owner_->value_.value(); }
    Rect barRect(const Style &style) const
    {
        const Rect c = inset(rect_, style.padding);
        const float markers = style.width * 0.9f;
        return {c.position, {c.size.x, std::max(1.0f, c.size.y - markers)}};
    }
    float positionAt(const Ui &ui, float x) const
    {
        const Rect bar = barRect(style(ui.theme()));
        return bar.size.x > 0.0f ? std::clamp((x - bar.position.x) / bar.size.x, 0.0f, 1.0f) : 0.0f;
    }
    int stopAt(const Ui &ui, const Gradient &g, glm::vec2 mouse) const
    {
        const Style s = style(ui.theme());
        const Rect bar = barRect(s);
        int best = -1;
        float bestDistance = s.width * 0.6f;
        for (std::size_t i = 0; i < g.stops.size(); ++i)
        {
            const float d = std::abs(bar.position.x + g.stops[i].position * bar.size.x - mouse.x);
            if (d <= bestDistance && mouse.y >= bar.end().y - 2.0f)
            {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }
    void write(Ui &ui, const Gradient &g)
    {
        if (!owner_->value_.writable()) // liaison en lecture seule : on peut choisir un arrêt, pas le déplacer
            return;
        pending_ = g;
        ui.defer([this, g] {
            owner_->value_.set(g);
            pending_.reset();
        });
        owner_->emit(ui, EventKind::kChange);
    }

    GradientEditor *owner_;
    int dragging_ = -1;
    std::optional<Gradient> pending_;
};

GradientEditor::GradientEditor(Binding<Gradient> value) : VBox("box"), value_(std::move(value))
{
    emplace<GradientBar>(*this).setWidth(SizeSpec::fill());
    // Couleur de l'arrêt choisi : même éditeur que les couleurs (roue, formats, alpha).
    Binding<Color> stop{[this] {
                            const Gradient g = value_.value();
                            return g.stops.empty() ? Color(0.0f) : g.stops[static_cast<std::size_t>(std::clamp(selected_, 0, static_cast<int>(g.stops.size()) - 1))].color;
                        },
                        value_.writable() ? std::function<void(const Color &)>([this](const Color &c) {
                            Gradient g = value_.value();
                            if (g.stops.empty())
                                return;
                            g.stops[static_cast<std::size_t>(std::clamp(selected_, 0, static_cast<int>(g.stops.size()) - 1))].color = c;
                            value_.set(g);
                        })
                                          : std::function<void(const Color &)>()};
    auto &editor = emplace<ColorEditor>(std::move(stop), true);
    editor.onChange([this](const Event &) { fire(EventKind::kChange); });
}

// ── CurveEditor ──────────────────────────────────────────────────────────────────────────────────────────────────────

CurveEditor::CurveEditor(Binding<Curve> value) : Widget("curve"), value_(std::move(value)) {}

glm::vec2 CurveEditor::measureContent(Ui &, glm::vec2, const Style &style) { return glm::vec2(style.width * 10.0f); }

Rect CurveEditor::area(const Ui &ui) const
{
    const Style s = style(ui.theme());
    return inset(rect_, s.padding + s.width * 0.4f); // marge : les points des bords restent entiers
}

glm::vec2 CurveEditor::toScreen(const Rect &a, glm::vec2 p) const { return {a.position.x + p.x * a.size.x, a.end().y - p.y * a.size.y}; }

glm::vec2 CurveEditor::toCurve(const Rect &a, glm::vec2 s) const
{
    return glm::clamp(glm::vec2((s.x - a.position.x) / std::max(a.size.x, 1.0f), (a.end().y - s.y) / std::max(a.size.y, 1.0f)), glm::vec2(0.0f),
                      glm::vec2(1.0f));
}

int CurveEditor::pointAt(const Ui &ui, glm::vec2 mouse) const
{
    const Rect a = area(ui);
    const Curve c = pending_ ? *pending_ : value_.value();
    const float radius = std::max(6.0f, style(ui.theme()).width * 0.5f);
    for (std::size_t i = 0; i < c.points.size(); ++i)
        if (glm::length(toScreen(a, c.points[i]) - mouse) <= radius)
            return static_cast<int>(i);
    return -1;
}

void CurveEditor::write(Ui &ui, Curve c)
{
    if (!value_.writable())
        return;
    pending_ = c;
    ui.defer([this, c] {
        value_.set(c);
        pending_.reset();
    });
    emit(ui, EventKind::kChange);
}

void CurveEditor::onPress(Ui &ui, glm::vec2 mouse)
{
    dragging_ = pointAt(ui, mouse);
    if (dragging_ >= 0)
        return;
    Curve c = pending_ ? *pending_ : value_.value();
    const glm::vec2 p = toCurve(area(ui), mouse);
    auto it = std::upper_bound(c.points.begin(), c.points.end(), p.x, [](float x, glm::vec2 q) { return x < q.x; });
    if (it == c.points.begin() || it == c.points.end())
        return; // pas avant le premier ni après le dernier point
    dragging_ = static_cast<int>(it - c.points.begin());
    c.points.insert(it, p);
    write(ui, c);
}

void CurveEditor::onDrag(Ui &ui, glm::vec2 mouse)
{
    Curve c = pending_ ? *pending_ : value_.value();
    if (dragging_ < 0 || dragging_ >= static_cast<int>(c.points.size()))
        return;
    const auto i = static_cast<std::size_t>(dragging_);
    glm::vec2 p = toCurve(area(ui), mouse);
    if (i == 0)
        p.x = c.points.front().x; // les extrémités gardent leur abscisse
    else if (i + 1 == c.points.size())
        p.x = c.points.back().x;
    else
    {
        // L'ordre des points est gardé, avec une marge de 0,001 si l'intervalle la contient ; sinon, bornes exactes des voisins.
        const float lo = c.points[i - 1].x + 0.001f, hi = c.points[i + 1].x - 0.001f;
        p.x = lo <= hi ? std::clamp(p.x, lo, hi) : std::clamp(p.x, c.points[i - 1].x, c.points[i + 1].x);
    }
    c.points[i] = p;
    write(ui, c);
}

bool CurveEditor::onSecondaryClick(Ui &ui, glm::vec2 mouse)
{
    Curve c = pending_ ? *pending_ : value_.value();
    const int i = pointAt(ui, mouse);
    if (i <= 0 || i + 1 >= static_cast<int>(c.points.size()))
        return i >= 0; // les extrémités ne se suppriment pas
    c.points.erase(c.points.begin() + i);
    write(ui, c);
    return true;
}

void CurveEditor::drawContent(const Ui &ui, renderer::DrawList &list, const Style &style) const
{
    const Rect a = area(ui);
    const Curve c = pending_ ? *pending_ : value_.value();
    Color grid = style.foreground;
    grid.a *= 0.12f;
    for (int k = 0; k <= 4; ++k)
    {
        const float f = static_cast<float>(k) / 4.0f;
        list.rect({a.position.x + f * a.size.x, a.position.y}, {1.0f, a.size.y}, grid);
        list.rect({a.position.x, a.position.y + f * a.size.y}, {a.size.x, 1.0f}, grid);
    }
    // La courbe, colonne par colonne (2 px d'épaisseur) : pas de primitive « ligne » dans la liste de dessin.
    const int columns = std::max(1, static_cast<int>(a.size.x));
    float previous = toScreen(a, {0.0f, c.evaluate(0.0f)}).y;
    for (int x = 0; x < columns; ++x)
    {
        const float fx = (static_cast<float>(x) + 1.0f) / static_cast<float>(columns);
        const float y = toScreen(a, {fx, c.evaluate(fx)}).y;
        const float top = std::min(previous, y) - 1.0f;
        list.rect({a.position.x + static_cast<float>(x), top}, {1.0f, std::abs(y - previous) + 2.0f}, style.accent);
        previous = y;
    }
    const float size = std::max(8.0f, style.width * 0.6f);
    for (std::size_t i = 0; i < c.points.size(); ++i)
        marker(list, toScreen(a, c.points[i]), static_cast<int>(i) == dragging_ ? size * 1.3f : size, Color(glm::vec3(style.background), 1.0f),
               style.accent); // fond du thème, rendu opaque (le marqueur doit cacher la courbe)
    (void)ui;
}

} // namespace orosmo::ui
