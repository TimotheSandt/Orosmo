#include "orosmo/ui/reflect.hpp"

#include "orosmo/ui/layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>

namespace orosmo::ui
{

std::string fieldLabel(std::string_view name)
{
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (c == '_')
        {
            out.push_back(' ');
            continue;
        }
        const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        if (i == 0)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            continue;
        }
        // Une majuscule après une minuscule commence un mot ("heightScale") ; un sigle reste groupé ("LOD").
        const bool afterLower = std::islower(static_cast<unsigned char>(name[i - 1])) != 0;
        if (upper && afterLower)
        {
            out.push_back(' ');
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            continue;
        }
        out.push_back(c);
    }
    return out;
}

namespace detail
{
bool namesColor(std::string_view name)
{
    std::string lower;
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos;
}
} // namespace detail

} // namespace orosmo::ui

// ── Variante à l'exécution (A79 lot L10) ────────────────────────────────────────────────────────────────────────────

namespace orosmo::ui
{

namespace
{
struct FieldAccess
{
    std::shared_ptr<const ObjectLocator> locate;
    std::size_t offset = 0;

    unsigned char *at() const
    {
        auto *base = static_cast<unsigned char *>((*locate)());
        return base != nullptr ? base + offset : nullptr;
    }
    template <typename T> T read() const
    {
        T value{};
        if (const unsigned char *p = at())
            std::memcpy(&value, p, sizeof value);
        return value;
    }
    template <typename T> void write(const T &value) const
    {
        if (unsigned char *p = at())
            std::memcpy(p, &value, sizeof value);
    }
    FieldAccess shifted(std::size_t by) const { return {locate, offset + by}; }
};

template <typename T> Binding<double> numberAt(FieldAccess a)
{
    return {[a] { return static_cast<double>(a.read<T>()); },
            [a](const double &v)
            {
                if (std::isnan(v))
                    return;
                if constexpr (std::is_integral_v<T>)
                {
                    const double r = std::round(v);
                    const double lo = static_cast<double>(std::numeric_limits<T>::lowest());
                    const double above = std::ldexp(1.0, std::numeric_limits<T>::digits);
                    a.write<T>(r < lo ? std::numeric_limits<T>::lowest() : (r >= above ? std::numeric_limits<T>::max() : static_cast<T>(r)));
                }
                else
                {
                    const double limit = static_cast<double>(std::numeric_limits<T>::max());
                    a.write<T>(static_cast<T>(std::clamp(v, -limit, limit)));
                }
            }};
}

template <typename T> std::unique_ptr<Widget> integerControl(FieldAccess a, const core::FieldDesc &f)
{
    const double lowest = static_cast<double>(std::numeric_limits<T>::lowest());
    const double below = std::floor(std::nextafter(std::ldexp(1.0, std::numeric_limits<T>::digits), 0.0));
    if (f.range.bounded)
    {
        const bool integral = f.range.min == std::floor(f.range.min) && f.range.max == std::floor(f.range.max);
        if (integral && f.range.min >= lowest && f.range.max <= below && f.range.min <= f.range.max)
            return std::make_unique<Slider>(numberAt<T>(a), f.range.min, f.range.max, 1.0);
        LOG_ERROR(1, "reflectedPanel: invalid range [", f.range.min, ", ", f.range.max, "] for the integer field '", f.name, "': field refused");
        return std::make_unique<Label>("plage invalide");
    }
    return std::make_unique<NumberField>(numberAt<T>(a), true, lowest, below);
}

template <typename T> std::unique_ptr<Widget> floatControl(FieldAccess a, const core::FieldDesc &f)
{
    if (f.range.bounded)
        return std::make_unique<Slider>(numberAt<T>(a), f.range.min, f.range.max);
    const double limit = static_cast<double>(std::numeric_limits<T>::max());
    auto field = std::make_unique<NumberField>(numberAt<T>(a), false, -limit, limit);
    field->setDoublePrecision(std::is_same_v<T, double>);
    return field;
}

template <typename T> std::unique_ptr<Widget> componentsControl(FieldAccess a, int count)
{
    auto row = std::make_unique<HBox>();
    Widget *rowPtr = row.get();
    for (int i = 0; i < count; ++i)
    {
        auto field = floatControl<T>(a.shifted(sizeof(T) * static_cast<std::size_t>(i)), core::FieldDesc{});
        field->setWidth(SizeSpec::fill());
        field->onChange([rowPtr](const Event &) { rowPtr->fire(EventKind::kChange); });
        row->add(std::move(field));
    }
    return row;
}

std::unique_ptr<Widget> readOnly(std::string text) { return std::make_unique<Label>(std::move(text)); }

std::unique_ptr<Widget> runtimeControl(FieldAccess a, const core::FieldDesc &f)
{
    using core::FieldType;
    const auto mismatch = [&] { return readOnly("(" + std::to_string(f.size) + " bytes)"); };
    switch (f.type)
    {
    case FieldType::kBool:
        if (f.size != 1)
            return mismatch();
        return std::make_unique<Checkbox>(std::string(), Binding<bool>{[a] { return a.read<unsigned char>() != 0; },
                                                                       [a](const bool &v) { a.write<unsigned char>(v ? 1 : 0); }});
    case FieldType::kInt:
    case FieldType::kEnum:
        switch (f.size)
        {
        case 1: return integerControl<std::int8_t>(a, f);
        case 2: return integerControl<std::int16_t>(a, f);
        case 4: return integerControl<std::int32_t>(a, f);
        case 8: return integerControl<std::int64_t>(a, f);
        default: return mismatch();
        }
    case FieldType::kUInt:
        switch (f.size)
        {
        case 1: return integerControl<std::uint8_t>(a, f);
        case 2: return integerControl<std::uint16_t>(a, f);
        case 4: return integerControl<std::uint32_t>(a, f);
        case 8: return integerControl<std::uint64_t>(a, f);
        default: return mismatch();
        }
    case FieldType::kFloat: return f.size == sizeof(float) ? floatControl<float>(a, f) : mismatch();
    case FieldType::kDouble: return f.size == sizeof(double) ? floatControl<double>(a, f) : mismatch();
    case FieldType::kVec2:
    case FieldType::kVec3:
    case FieldType::kVec4:
    case FieldType::kQuat:
    {
        const int count = f.type == FieldType::kVec2 ? 2 : (f.type == FieldType::kVec3 ? 3 : 4);
        if (f.size != sizeof(float) * static_cast<std::size_t>(count))
            return mismatch();
        if ((f.type == FieldType::kVec3 || f.type == FieldType::kVec4) && detail::namesColor(f.name))
        {
            const bool alpha = f.type == FieldType::kVec4;
            Binding<Color> b{[a, alpha]
                             {
                                 if (alpha)
                                     return a.read<glm::vec4>();
                                 return Color(a.read<glm::vec3>(), 1.0f);
                             },
                             [a, alpha](const Color &c)
                             {
                                 if (alpha)
                                     a.write<glm::vec4>(c);
                                 else
                                     a.write<glm::vec3>(glm::vec3(c));
                             }};
            return std::make_unique<ColorPicker>(std::move(b), alpha);
        }
        return componentsControl<float>(a, count);
    }
    case FieldType::kDVec3: return f.size == sizeof(double) * 3 ? componentsControl<double>(a, 3) : mismatch();
    case FieldType::kDQuat: return f.size == sizeof(double) * 4 ? componentsControl<double>(a, 4) : mismatch();
    case FieldType::kString:
    case FieldType::kArray: return readOnly("(not editable)");
    case FieldType::kStruct: break;
    }
    return mismatch();
}

std::unique_ptr<Widget> runtimePanel(const FieldAccess &object, std::span<const core::FieldDesc> fields, const std::string &idPrefix)
{
    auto grid = std::make_unique<Grid>(2, "box");
    Widget &panel = *grid;
    for (const core::FieldDesc &f : fields)
    {
        const std::string label = fieldLabel(f.name);
        const FieldAccess access = object.shifted(f.offset);
        if (f.type == core::FieldType::kStruct && !f.fields.empty())
        {
            auto &section = grid->emplace<Section>(label);
            section.columnSpan = 2;
            section.setTooltip(f.description);
            section.setId(idPrefix + f.name);
            auto inner = runtimePanel(access, f.fields, idPrefix + f.name + ".");
            inner->onChange([&panel](const Event &) { panel.fire(EventKind::kChange); });
            section.body().add(std::move(inner));
            continue;
        }
        grid->emplace<Label>(label).setTooltip(f.description);
        auto control = f.type == core::FieldType::kStruct ? readOnly(std::to_string(f.size) + " bytes") : runtimeControl(access, f);
        control->setId(idPrefix + f.name);
        control->setTooltip(f.description);
        control->setWidth(SizeSpec::fill());
        control->onChange([&panel](const Event &) { panel.fire(EventKind::kChange); });
        grid->add(std::move(control));
    }
    return grid;
}
} // namespace

std::unique_ptr<Widget> reflectedPanel(ObjectLocator locate, std::span<const core::FieldDesc> fields, std::string_view idPrefix)
{
    if (!locate)
        locate = [] { return static_cast<void *>(nullptr); };
    const FieldAccess object{std::make_shared<const ObjectLocator>(std::move(locate)), 0};
    return runtimePanel(object, fields, std::string(idPrefix));
}

} // namespace orosmo::ui
