#include "engine/ui/theme.hpp"

#include "engine/core/file_watcher.hpp"

#include "engine/renderer/image.hpp"

#include "Logger.h"
#include <nlohmann/json.hpp>

#include <charconv>
#include <fstream>
#include <set>
#include <sstream>

namespace engine::ui
{

using core::ErrorCode;
using core::makeError;
using Json = nlohmann::json;

namespace
{
constexpr std::array<std::string_view, kWidgetStateCount> kStateNames = {"normal", "hover", "pressed", "disabled", "focus"};
constexpr std::array<std::string_view, kStylePropertyCount> kPropertyNames = {
    "background", "foreground", "border",  "accent",   "muted",      "shadowColor",  "borderWidth",
    "radius",     "padding",    "spacing", "fontSize", "shadowSize", "shadowOffset", "width",       "image",       "imageTint", "font",
};

std::optional<WidgetState> stateFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kStateNames.size(); ++i)
    {
        if (kStateNames[i] == name)
        {
            return static_cast<WidgetState>(i);
        }
    }
    return std::nullopt;
}

std::string knownList(auto const &names)
{
    std::string out;
    for (std::string_view n : names)
    {
        out += out.empty() ? "" : ", ";
        out += n;
    }
    return out;
}

std::optional<float> parseNumber(std::string_view text)
{
    float value = 0.0f;
    const auto *end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc() || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Données brutes d'un fichier de thème fusionnées avec celles de ses parents (avant résolution des jetons).
struct RawWidget
{
    std::map<std::string, Json> base;
    std::map<std::string, std::map<std::string, Json>> states;
};
struct RawTheme
{
    std::string name;
    std::map<std::string, Json, std::less<>> tokens;
    std::map<std::string, RawWidget> widgets;
    std::map<std::string, std::pair<Json, std::filesystem::path>> images; // définition, dossier du fichier qui la donne
    std::map<std::string, std::filesystem::path> fonts;                   // nom -> fichier (chemin résolu)
    std::vector<std::filesystem::path> sources;
};

core::Result<std::string> readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, path.generic_string() + ": cannot read the theme file");
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

core::Result<> mergeDocument(RawTheme &raw, std::string_view text, const std::filesystem::path &baseDirectory, const std::string &source,
                             std::set<std::filesystem::path> &visiting);

core::Result<> mergeFile(RawTheme &raw, const std::filesystem::path &path, std::set<std::filesystem::path> &visiting)
{
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec)
    {
        canonical = path;
    }
    if (visiting.contains(canonical))
    {
        return makeError(ErrorCode::kParse, path.generic_string() + ": cycle in \"extends\"");
    }
    raw.sources.push_back(path); // avant la lecture : un parent qui manque encore est observé lui aussi
    auto text = readText(path);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    visiting.insert(canonical);
    auto merged = mergeDocument(raw, *text, path.parent_path(), path.generic_string(), visiting);
    visiting.erase(canonical);
    return merged;
}

core::Result<> mergeDocument(RawTheme &raw, std::string_view text, const std::filesystem::path &baseDirectory, const std::string &source,
                             std::set<std::filesystem::path> &visiting)
{
    Json doc = Json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (doc.is_discarded() || !doc.is_object())
    {
        return makeError(ErrorCode::kParse, source + ": not a JSON object");
    }
    auto fail = [&](const std::string &where, const std::string &what) { return makeError(ErrorCode::kParse, source + ": " + where + ": " + what); };

    // Le parent d'abord : ce fichier en surcharge les jetons et les propriétés.
    if (auto it = doc.find("extends"); it != doc.end())
    {
        if (!it->is_string())
        {
            return fail("extends", "expected a file name");
        }
        if (auto parent = mergeFile(raw, baseDirectory / it->get<std::string>(), visiting); !parent)
        {
            return parent;
        }
    }
    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        if (it.key() != "name" && it.key() != "extends" && it.key() != "tokens" && it.key() != "widgets" && it.key() != "$schema" &&
            it.key() != "description" && it.key() != "images" && it.key() != "fonts")
        {
            return fail(it.key(), "unknown key (expected name, extends, tokens, images, fonts, widgets)");
        }
    }
    if (auto it = doc.find("name"); it != doc.end())
    {
        if (!it->is_string())
        {
            return fail("name", "expected a string");
        }
        raw.name = it->get<std::string>();
    }
    if (auto it = doc.find("tokens"); it != doc.end())
    {
        if (!it->is_object())
        {
            return fail("tokens", "expected an object");
        }
        for (auto t = it->begin(); t != it->end(); ++t)
        {
            if (!t->is_string() && !t->is_number())
            {
                return fail("tokens." + t.key(), "expected a color \"#rrggbb[aa]\", a number or another token's name");
            }
            raw.tokens[t.key()] = *t;
        }
    }
    if (auto it = doc.find("fonts"); it != doc.end())
    {
        if (!it->is_object())
        {
            return fail("fonts", "expected an object {\"name\": \"file.ttf\"}");
        }
        for (auto f = it->begin(); f != it->end(); ++f)
        {
            if (!f->is_string())
            {
                return fail("fonts." + f.key(), "expected the font file name (relative to the theme)");
            }
            raw.fonts[f.key()] = baseDirectory / f->get<std::string>();
        }
    }
    if (auto it = doc.find("images"); it != doc.end())
    {
        if (!it->is_object())
        {
            return fail("images", "expected an object");
        }
        for (auto i = it->begin(); i != it->end(); ++i)
        {
            if (!i->is_object())
            {
                return fail("images." + i.key(), "expected an object {\"file\", \"slice\", \"edges\", \"center\", \"scale\"}");
            }
            raw.images[i.key()] = {*i, baseDirectory};
        }
    }
    if (auto it = doc.find("widgets"); it != doc.end())
    {
        if (!it->is_object())
        {
            return fail("widgets", "expected an object");
        }
        for (auto w = it->begin(); w != it->end(); ++w)
        {
            const std::string path = "widgets." + w.key();
            if (!w->is_object())
            {
                return fail(path, "expected an object");
            }
            RawWidget &target = raw.widgets[w.key()];
            for (auto p = w->begin(); p != w->end(); ++p)
            {
                if (p.key() == "states")
                {
                    if (!p->is_object())
                    {
                        return fail(path + ".states", "expected an object");
                    }
                    for (auto s = p->begin(); s != p->end(); ++s)
                    {
                        if (!stateFromName(s.key()))
                        {
                            return fail(path + ".states." + s.key(), "unknown state (known: " + knownList(kStateNames) + ")");
                        }
                        if (!s->is_object())
                        {
                            return fail(path + ".states." + s.key(), "expected an object");
                        }
                        for (auto sp = s->begin(); sp != s->end(); ++sp)
                        {
                            target.states[s.key()][sp.key()] = *sp;
                        }
                    }
                    continue;
                }
                target.base[p.key()] = *p;
            }
        }
    }
    return {};
}

} // namespace

std::string_view stateName(WidgetState state) noexcept { return kStateNames[static_cast<std::size_t>(state)]; }
std::string_view propertyName(StyleProperty property) noexcept { return kPropertyNames[static_cast<std::size_t>(property)]; }

std::optional<StyleProperty> propertyFromName(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kPropertyNames.size(); ++i)
    {
        if (kPropertyNames[i] == name)
        {
            return static_cast<StyleProperty>(i);
        }
    }
    return std::nullopt;
}

bool isColorProperty(StyleProperty property) noexcept
{
    return property <= StyleProperty::kShadowColor || property == StyleProperty::kImageTint;
}

void Style::set(StyleProperty property, const TokenValue &value) noexcept
{
    if (property == StyleProperty::kImage)
    {
        if (const ImageRef *i = std::get_if<ImageRef>(&value))
            image = *i;
        return;
    }
    if (property == StyleProperty::kFont)
    {
        if (const FontRef *f = std::get_if<FontRef>(&value))
            font = *f;
        return;
    }
    if (property == StyleProperty::kImageTint)
    {
        if (const Color *c = std::get_if<Color>(&value))
            imageTint = *c;
        return;
    }
    if (isColorProperty(property))
    {
        const Color *c = std::get_if<Color>(&value);
        if (c == nullptr)
        {
            return;
        }
        switch (property)
        {
        case StyleProperty::kBackground: background = *c; break;
        case StyleProperty::kForeground: foreground = *c; break;
        case StyleProperty::kBorder: border = *c; break;
        case StyleProperty::kAccent: accent = *c; break;
        case StyleProperty::kMuted: muted = *c; break;
        default: shadowColor = *c; break;
        }
        return;
    }
    const float *f = std::get_if<float>(&value);
    if (f == nullptr)
    {
        return;
    }
    switch (property)
    {
    case StyleProperty::kBorderWidth: borderWidth = *f; break;
    case StyleProperty::kRadius: radius = *f; break;
    case StyleProperty::kPadding: padding = *f; break;
    case StyleProperty::kSpacing: spacing = *f; break;
    case StyleProperty::kFontSize: fontSize = *f; break;
    case StyleProperty::kShadowSize: shadowSize = *f; break;
    case StyleProperty::kShadowOffset: shadowOffset = *f; break;
    default: width = *f; break;
    }
}

std::optional<Color> parseColor(std::string_view text) noexcept
{
    if (text.size() != 7 && text.size() != 9)
    {
        return std::nullopt;
    }
    if (text[0] != '#')
    {
        return std::nullopt;
    }
    Color c{0.0f, 0.0f, 0.0f, 1.0f};
    for (std::size_t i = 0; i < (text.size() - 1) / 2; ++i)
    {
        const int hi = hexDigit(text[1 + 2 * i]);
        const int lo = hexDigit(text[2 + 2 * i]);
        if (hi < 0 || lo < 0)
        {
            return std::nullopt;
        }
        c[static_cast<glm::length_t>(i)] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return c;
}

// Construit un Theme à partir des données brutes : résout les jetons (alias compris), vérifie chaque propriété, précalcule les
// styles de chaque widget dans chaque état.
struct ThemeBuilder
{
    static core::Result<Theme> build(RawTheme raw, const std::string &source)
    {
        Theme theme;
        theme.name_ = raw.name;
        theme.sources_ = std::move(raw.sources);
        auto fail = [&](const std::string &where, const std::string &what) { return makeError(ErrorCode::kParse, source + ": " + where + ": " + what); };

        // Jetons : nombre, couleur, ou nom d'un autre jeton (profondeur bornée : un cycle est une erreur).
        for (const auto &[name, value] : raw.tokens)
        {
            Json current = value;
            std::string chain = name;
            for (int depth = 0;; ++depth)
            {
                if (depth > 16)
                {
                    return fail("tokens." + name, "alias cycle (" + chain + ")");
                }
                if (current.is_number())
                {
                    theme.tokens_[name] = current.get<float>();
                    break;
                }
                const std::string text = current.get<std::string>();
                if (!text.empty() && text[0] == '#')
                {
                    auto color = parseColor(text);
                    if (!color)
                    {
                        return fail("tokens." + name, "invalid color '" + text + "' (expected #rrggbb or #rrggbbaa)");
                    }
                    theme.tokens_[name] = *color;
                    break;
                }
                auto next = raw.tokens.find(text);
                if (next == raw.tokens.end())
                {
                    return fail("tokens." + name, "unknown token '" + text + "'");
                }
                chain += " -> " + text;
                current = next->second;
            }
        }

        // Images en neuf parties : fichier PNG (relatif au thème qui le nomme), coins, remplissage des bords et du centre, échelle.
        for (const auto &[name, entry] : raw.images)
        {
            const auto &[def, baseDir] = entry;
            const std::string path = "images." + name;
            auto style = std::make_shared<ImageStyle>();
            style->name = name;
            auto file = def.find("file");
            if (file == def.end() || !file->is_string())
            {
                return fail(path + ".file", "expected the PNG file name");
            }
            const std::filesystem::path png = baseDir / file->get<std::string>();
            auto loaded = renderer::loadPng(png);
            if (!loaded)
            {
                return makeError(ErrorCode::kIo, source + ": " + path + ".file: cannot read " + png.generic_string());
            }
            auto atlas = std::make_shared<renderer::AtlasImage>();
            atlas->id = renderer::newAtlasId();
            atlas->width = loaded->width;
            atlas->height = loaded->height;
            atlas->rgba = std::move(loaded->rgba);
            style->image = atlas;
            theme.sources_.push_back(png);
            if (auto slice = def.find("slice"); slice != def.end())
            {
                std::array<float, 4> v{};
                if (slice->is_number())
                {
                    v.fill(slice->get<float>());
                }
                else if (slice->is_array() && slice->size() == 4 && std::all_of(slice->begin(), slice->end(), [](const Json &j) { return j.is_number(); }))
                {
                    for (std::size_t k = 0; k < 4; ++k)
                        v[k] = (*slice)[k].get<float>();
                }
                else
                {
                    return fail(path + ".slice", "expected a number or [left, top, right, bottom] (texels)");
                }
                style->left = v[0];
                style->top = v[1];
                style->right = v[2];
                style->bottom = v[3];
            }
            if (style->left < 0 || style->top < 0 || style->right < 0 || style->bottom < 0 ||
                style->left + style->right > static_cast<float>(atlas->width) || style->top + style->bottom > static_cast<float>(atlas->height))
            {
                return fail(path + ".slice", "the corners do not fit in the " + std::to_string(atlas->width) + "x" + std::to_string(atlas->height) + " image");
            }
            auto fillOf = [&](const char *key, SliceFill &out) -> core::Result<>
            {
                auto it = def.find(key);
                if (it == def.end())
                    return {};
                const std::string mode = it->is_string() ? it->get<std::string>() : "";
                if (mode == "stretch")
                    out = SliceFill::kStretch;
                else if (mode == "repeat")
                    out = SliceFill::kRepeat;
                else if (mode == "none")
                    out = SliceFill::kNone;
                else
                    return fail(path + "." + key, "expected \"stretch\", \"repeat\" or \"none\"");
                return {};
            };
            if (auto r = fillOf("edges", style->edges); !r)
                return std::unexpected(r.error());
            if (auto r = fillOf("center", style->center); !r)
                return std::unexpected(r.error());
            if (auto s = def.find("scale"); s != def.end())
            {
                if (!s->is_number() || !(s->get<float>() >= 0.01f && s->get<float>() <= 100.0f))
                    return fail(path + ".scale", "expected a number from 0.01 to 100");
                style->scale = s->get<float>();
            }
            for (auto k = def.begin(); k != def.end(); ++k)
            {
                if (k.key() != "file" && k.key() != "slice" && k.key() != "edges" && k.key() != "center" && k.key() != "scale")
                    return fail(path + "." + k.key(), "unknown key (expected file, slice, edges, center, scale)");
            }
            theme.images_[name] = std::move(style);
        }

        // Polices : le fichier doit exister (il est lu par le rendu du texte au premier usage).
        for (const auto &[name, file] : raw.fonts)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec))
            {
                return makeError(ErrorCode::kIo, source + ": fonts." + name + ": cannot read " + file.generic_string());
            }
            const auto written = std::filesystem::last_write_time(file, ec);
            theme.fonts_[name] = std::make_shared<FontFace>(FontFace{name, file, ec ? 0 : static_cast<std::int64_t>(written.time_since_epoch().count())});
            theme.sources_.push_back(file);
        }

        using Props = std::vector<std::pair<StyleProperty, TokenValue>>;
        auto resolveProps = [&](const std::map<std::string, Json> &props, const std::string &where) -> core::Result<Props>
        {
            Props out;
            for (const auto &[key, value] : props)
            {
                const std::string path = where + "." + key;
                auto property = propertyFromName(key);
                if (!property)
                {
                    return fail(path, "unknown property (known: " + knownList(kPropertyNames) + ")");
                }
                if (*property == StyleProperty::kFont)
                {
                    const std::string name = value.is_string() ? value.get<std::string>() : std::string();
                    FontRef f = theme.font(name);
                    if (!f && name != "default")
                        return fail(path, "unknown font '" + name + "' (section \"fonts\", or \"default\")");
                    out.emplace_back(*property, TokenValue(f));
                    continue;
                }
                if (*property == StyleProperty::kImage && value.is_string())
                {
                    // Une image se nomme directement (pas de jeton) : "none" retire l'image héritée.
                    const std::string name = value.get<std::string>();
                    ImageRef i = theme.image(name);
                    if (!i && name != "none")
                        return fail(path, "unknown image '" + name + "' (section \"images\")");
                    out.emplace_back(*property, TokenValue(i));
                    continue;
                }
                core::Result<TokenValue> resolved = value.is_number()   ? core::Result<TokenValue>(value.get<float>())
                                                    : value.is_string() ? theme.resolve(value.get<std::string>())
                                                                        : makeError(ErrorCode::kParse, "expected a token name, a color or a number");
                if (!resolved)
                {
                    return fail(path, resolved.error().message);
                }
                if (*property == StyleProperty::kImage)
                {
                    if (!std::holds_alternative<ImageRef>(*resolved))
                        return fail(path, "expects an image name (section \"images\") or \"none\"");
                }
                else
                {
                    const bool isColor = std::holds_alternative<Color>(*resolved);
                    if (std::holds_alternative<ImageRef>(*resolved) || std::holds_alternative<FontRef>(*resolved) || isColor != isColorProperty(*property))
                    {
                        return fail(path, isColorProperty(*property) ? "expects a color" : "expects a number");
                    }
                }
                out.emplace_back(*property, *resolved);
            }
            return out;
        };

        struct Resolved
        {
            Props base;
            std::array<Props, kWidgetStateCount> states;
        };
        std::map<std::string, Resolved, std::less<>> resolved;
        raw.widgets.try_emplace("default");
        for (const auto &[widget, data] : raw.widgets)
        {
            Resolved r;
            auto base = resolveProps(data.base, "widgets." + widget);
            if (!base)
            {
                return std::unexpected(base.error());
            }
            r.base = std::move(*base);
            for (const auto &[state, props] : data.states)
            {
                auto p = resolveProps(props, "widgets." + widget + ".states." + state);
                if (!p)
                {
                    return std::unexpected(p.error());
                }
                r.states[static_cast<std::size_t>(*stateFromName(state))] = std::move(*p);
            }
            resolved.emplace(widget, std::move(r));
        }

        const Resolved &fallback = resolved.find("default")->second;
        auto apply = [](Style &style, const Props &props)
        {
            for (const auto &[property, value] : props)
            {
                style.set(property, value);
            }
        };
        for (const auto &[widget, r] : resolved)
        {
            Theme::StyleSet set;
            for (std::size_t s = 0; s < kWidgetStateCount; ++s)
            {
                Style style;
                // default, puis le widget, puis l'état de default, puis l'état du widget : un état générique (désactivé) s'applique à
                // tous les widgets, sauf si le widget définit le même état.
                apply(style, fallback.base);
                apply(style, r.base);
                if (s != 0)
                {
                    apply(style, fallback.states[0]);
                    apply(style, r.states[0]);
                }
                apply(style, fallback.states[s]);
                apply(style, r.states[s]);
                set[s] = style;
            }
            theme.styles_.emplace(widget, set);
        }
        return theme;
    }
};

namespace
{
// Theme::load, en rendant aussi les fichiers lus ou tentés (même en cas d'échec) : ThemeHost les observe.
core::Result<Theme> loadTracked(const std::filesystem::path &path, std::vector<std::filesystem::path> &attempted)
{
    RawTheme raw;
    std::set<std::filesystem::path> visiting;
    auto merged = mergeFile(raw, path, visiting);
    attempted = raw.sources;
    if (!merged)
    {
        return std::unexpected(merged.error());
    }
    return ThemeBuilder::build(std::move(raw), path.generic_string());
}
} // namespace

core::Result<Theme> Theme::load(const std::filesystem::path &path)
{
    std::vector<std::filesystem::path> attempted;
    return loadTracked(path, attempted);
}

core::Result<Theme> Theme::parse(std::string_view json, const std::filesystem::path &baseDirectory, std::string_view sourceName)
{
    RawTheme raw;
    std::set<std::filesystem::path> visiting;
    const std::string source(sourceName);
    if (auto merged = mergeDocument(raw, json, baseDirectory, source, visiting); !merged)
    {
        return std::unexpected(merged.error());
    }
    return ThemeBuilder::build(std::move(raw), source);
}

Theme Theme::fallback()
{
    static constexpr std::string_view kJson = R"({
        "name": "fallback",
        "tokens": {"bg": "#202020f0", "fg": "#e6e6e6", "accent": "#4c8dff", "muted": "#5a5a5a", "control": "#353535"},
        "widgets": {
            "default": {"foreground": "fg", "accent": "accent", "muted": "muted", "padding": 4, "spacing": 4, "fontSize": 16,
                        "width": 16, "states": {"disabled": {"foreground": "muted"}, "focus": {"border": "accent", "borderWidth": 1}}},
            "panel": {"background": "bg", "padding": 8},
            "button": {"background": "control", "radius": 3, "states": {"hover": {"background": "muted"}, "pressed": {"background": "accent"}}},
            "textField": {"background": "control"},
            "dropdown": {"background": "control"},
            "popup": {"background": "control"},
            "tooltip": {"background": "control", "padding": 4}
        }
    })";
    auto theme = parse(kJson, {}, "fallback");
    ENGINE_ASSERT_ALWAYS(theme.has_value(), "the built-in fallback theme must parse");
    return std::move(*theme);
}

std::optional<TokenValue> Theme::token(std::string_view name) const
{
    auto it = tokens_.find(name);
    return it == tokens_.end() ? std::nullopt : std::optional<TokenValue>(it->second);
}

core::Result<TokenValue> Theme::resolve(std::string_view value) const
{
    if (!value.empty() && value[0] == '#')
    {
        if (auto color = parseColor(value))
        {
            return *color;
        }
        return makeError(ErrorCode::kParse, "invalid color '" + std::string(value) + "' (expected #rrggbb or #rrggbbaa)");
    }
    if (auto number = parseNumber(value))
    {
        return *number;
    }
    if (auto t = token(value))
    {
        return *t;
    }
    if (value == "none")
    {
        return ImageRef{};
    }
    if (value == "default")
    {
        return FontRef{};
    }
    if (FontRef f = font(value))
    {
        return f;
    }
    if (ImageRef i = image(value))
    {
        return i;
    }
    return makeError(ErrorCode::kNotFound, "unknown token '" + std::string(value) + "'");
}

FontRef Theme::font(std::string_view name) const
{
    auto it = fonts_.find(name);
    return it == fonts_.end() ? FontRef{} : it->second;
}

ImageRef Theme::image(std::string_view name) const
{
    auto it = images_.find(name);
    return it == images_.end() ? ImageRef{} : it->second;
}

const Style &Theme::style(std::string_view widget, WidgetState state) const
{
    auto it = styles_.find(widget);
    if (it == styles_.end())
    {
        it = styles_.find("default");
    }
    return it->second[static_cast<std::size_t>(state)];
}

bool Theme::hasWidget(std::string_view widget) const { return styles_.contains(widget); }

// ── ThemeHost ───────────────────────────────────────────────────────────────────────────────────────────────────────

ThemeHost::ThemeHost() : theme_(Theme::fallback()) {}
ThemeHost::~ThemeHost() { unwatch(); }

core::Result<> ThemeHost::load(const std::filesystem::path &path)
{
    std::vector<std::filesystem::path> attempted;
    auto theme = loadTracked(path, attempted);
    path_ = path;
    pending_ = false;
    if (!theme)
    {
        lastError_ = theme.error();
        attempted_ = std::move(attempted);
        if (watcher_ != nullptr)
        {
            rewatch();
        }
        return std::unexpected(theme.error());
    }
    install(std::move(*theme));
    return {};
}

void ThemeHost::set(Theme theme)
{
    path_.clear();
    pending_ = false;
    install(std::move(theme));
}

void ThemeHost::install(Theme theme)
{
    attempted_.clear();
    theme_ = std::move(theme);
    lastError_.reset();
    ++revision_;
    if (watcher_ != nullptr)
    {
        rewatch();
    }
}

void ThemeHost::watch(core::FileWatcher &watcher)
{
    unwatch();
    watcher_ = &watcher;
    rewatch();
}

void ThemeHost::unwatch()
{
    if (watcher_ != nullptr)
    {
        for (std::uint64_t id : watchIds_)
        {
            watcher_->unwatch(id);
        }
    }
    watchIds_.clear();
    watcher_ = nullptr;
}

bool ThemeHost::reloadIfChanged()
{
    if (!pending_)
    {
        return false;
    }
    pending_ = false;
    if (path_.empty())
    {
        return false;
    }
    std::vector<std::filesystem::path> attempted;
    auto theme = loadTracked(path_, attempted);
    if (!theme)
    {
        lastError_ = theme.error();
        LOG_ERROR(1, "Theme reload failed, the previous theme is kept: ", theme.error().message);
        // Observe aussi ce que la tentative a lu ou cherché (un nouveau parent qui n'existe pas encore, par exemple).
        attempted_ = std::move(attempted);
        if (watcher_ != nullptr)
        {
            rewatch();
        }
        return false;
    }
    LOG_INFO("Theme reloaded: ", path_.generic_string());
    install(std::move(*theme));
    return true;
}

void ThemeHost::rewatch()
{
    core::FileWatcher *watcher = watcher_;
    for (std::uint64_t id : watchIds_)
    {
        watcher->unwatch(id);
    }
    watchIds_.clear();
    std::vector<std::filesystem::path> files = theme_.sources();
    files.insert(files.end(), attempted_.begin(), attempted_.end());
    std::ranges::sort(files);
    const auto [first, last] = std::ranges::unique(files);
    files.erase(first, last);
    for (const std::filesystem::path &source : files)
    {
        watchIds_.push_back(watcher->watch(source, [this](const core::FileEvent &) { pending_ = true; }));
    }
}

} // namespace engine::ui
