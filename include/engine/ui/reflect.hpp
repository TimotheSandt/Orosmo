// UI-12 : paramètres réfléchis → widgets. Une structure décrite par `core::Reflect<T>` (CORE-12) s'affiche automatiquement : une
// ligne par champ (libellé, widget choisi d'après le type et la plage), infobulle = description du champ, structures imbriquées en
// sections repliables. Aucun code d'interface à écrire pour une nouvelle structure de paramètres.
//
//   bool                       → Checkbox
//   énumération avec EnumNames → Dropdown (sinon NumberField entier)
//   entier borné / non borné   → Slider (pas 1) / NumberField entier
//   flottant borné / non borné → Slider / NumberField
//   std::string                → TextField
//   Vec3, Vec4 dont le nom contient « color » → ColorPicker ; sinon un NumberField par composante (Vec2 aussi)
//   structure réfléchie        → Section (récursif)
//   std::vector                → libellé « [n] » en lecture seule (pas d'édition de liste ; docs/GAPS.md)
#pragma once

#include "engine/core/reflection.hpp"
#include "engine/ui/widgets.hpp"

#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

namespace engine::ui
{

// Libellé lisible d'un nom de champ : "heightScale" → "Height scale", "tileResolution" → "Tile resolution".
std::string fieldLabel(std::string_view name);

namespace detail
{
bool namesColor(std::string_view name);

template <typename M> std::unique_ptr<Widget> reflectedControl(M &member, const core::Range &range, std::string_view name, Widget &panel);

template <int N, typename V> std::unique_ptr<Widget> vectorFields(V &v)
{
    auto row = std::make_unique<HBox>();
    Widget *rowPtr = row.get();
    for (int i = 0; i < N; ++i)
    {
        Binding<double> b{[p = &v, i] { return static_cast<double>((*p)[i]); }, [p = &v, i](const double &x) { (*p)[i] = static_cast<float>(x); }};
        auto &field = row->emplace<NumberField>(std::move(b), false);
        field.setWidth(SizeSpec::fill());
        field.onChange([rowPtr](const Event &) { rowPtr->fire(EventKind::kChange); });
    }
    return row;
}
} // namespace detail

template <core::Reflected T> std::unique_ptr<Widget> reflectedPanel(T &object, std::string_view idPrefix = {});

namespace detail
{
template <typename M> std::unique_ptr<Widget> reflectedControl(M &member, const core::Range &range, std::string_view name, Widget &panel)
{
    (void)panel;
    if constexpr (std::is_same_v<M, bool>)
    {
        return std::make_unique<Checkbox>(std::string(), Binding<bool>::ref(member));
    }
    else if constexpr (core::NamedEnum<M>)
    {
        std::vector<std::string> names;
        for (const char *n : core::EnumNames<M>::names)
            names.emplace_back(n);
        Binding<int> b{[p = &member] { return static_cast<int>(*p); }, [p = &member](const int &i) { *p = static_cast<M>(i); }};
        return std::make_unique<Dropdown>(std::move(names), std::move(b));
    }
    else if constexpr (std::is_enum_v<M>)
    {
        using U = std::underlying_type_t<M>;
        Binding<double> b{[p = &member] { return static_cast<double>(static_cast<U>(*p)); },
                          [p = &member](const double &v) { *p = static_cast<M>(static_cast<U>(v)); }};
        return std::make_unique<NumberField>(std::move(b), true);
    }
    else if constexpr (std::is_integral_v<M>)
    {
        if (range.bounded)
            return std::make_unique<Slider>(numberBinding(member), range.min, range.max, 1.0);
        return std::make_unique<NumberField>(numberBinding(member), true, static_cast<double>(std::numeric_limits<M>::lowest()),
                                             static_cast<double>(std::numeric_limits<M>::max()));
    }
    else if constexpr (std::is_floating_point_v<M>)
    {
        if (range.bounded)
            return std::make_unique<Slider>(numberBinding(member), range.min, range.max);
        return std::make_unique<NumberField>(numberBinding(member), false);
    }
    else if constexpr (std::is_same_v<M, std::string>)
    {
        return std::make_unique<TextField>(Binding<std::string>::ref(member));
    }
    else if constexpr (std::is_same_v<M, core::Vec4> || std::is_same_v<M, core::Vec3>)
    {
        if (namesColor(name))
        {
            constexpr bool kAlpha = std::is_same_v<M, core::Vec4>;
            Binding<Color> b{[p = &member]
                             {
                                 if constexpr (kAlpha)
                                     return Color(*p);
                                 else
                                     return Color(*p, 1.0f);
                             },
                             [p = &member](const Color &c) { *p = M(c); }};
            return std::make_unique<ColorPicker>(std::move(b), kAlpha);
        }
        return vectorFields<M::length()>(member);
    }
    else if constexpr (std::is_same_v<M, core::Vec2>)
    {
        return vectorFields<2>(member);
    }
    else if constexpr (core::IsVector<M>::value)
    {
        auto label = std::make_unique<Label>();
        label->bindText([p = &member] { return "[" + std::to_string(p->size()) + "]"; });
        return label;
    }
    else
    {
        static_assert(core::Reflected<M>, "reflectedPanel: type de champ non pris en charge");
        return nullptr;
    }
}
} // namespace detail

// Panneau de réglage de `object` (qui doit vivre plus longtemps que le panneau). Tout changement d'un champ, à n'importe quelle
// profondeur, émet `onChange` sur le panneau renvoyé. Identifiant de chaque contrôle : `idPrefix` + chemin du champ
// ("relief.octaves").
template <core::Reflected T> std::unique_ptr<Widget> reflectedPanel(T &object, std::string_view idPrefix)
{
    auto grid = std::make_unique<Grid>(2, "box");
    Widget &panel = *grid;
    std::apply(
        [&](const auto &...field)
        {
            auto addOne = [&](const auto &f)
            {
                using M = typename std::remove_cvref_t<decltype(f)>::Member;
                M &member = object.*(f.member);
                const std::string label = fieldLabel(f.name);
                const std::string help = f.description != nullptr ? f.description : "";
                if constexpr (core::Reflected<M>)
                {
                    auto &section = grid->template emplace<Section>(label);
                    section.columnSpan = 2;
                    section.setTooltip(help);
                    section.setId(std::string(idPrefix) + f.name);
                    auto inner = reflectedPanel(member, std::string(idPrefix) + f.name + ".");
                    inner->onChange([&panel](const Event &) { panel.fire(EventKind::kChange); });
                    section.body().add(std::move(inner));
                }
                else
                {
                    grid->template emplace<Label>(label).setTooltip(help);
                    auto control = detail::reflectedControl(member, f.range, f.name, panel);
                    control->setId(std::string(idPrefix) + f.name);
                    control->setTooltip(help);
                    control->setWidth(SizeSpec::fill());
                    control->onChange([&panel](const Event &) { panel.fire(EventKind::kChange); });
                    grid->add(std::move(control));
                }
            };
            (addOne(field), ...);
        },
        core::Reflect<T>::fields());
    return grid;
}

} // namespace engine::ui
