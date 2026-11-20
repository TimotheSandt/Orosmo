#include "orosmo/ui/reflect.hpp"

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
