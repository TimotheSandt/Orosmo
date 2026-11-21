#include <orosmo/core/parse.hpp>

#include <cerrno>
#include <charconv>
#include <string>
#include <system_error>

// OROSMO_PARSE_STRTOD : chemin de macOS, forçable ailleurs pour le tester (glibc a aussi newlocale et strtod_l).
#if defined(__APPLE__) || defined(OROSMO_PARSE_STRTOD)
#define OROSMO_PARSE_USE_STRTOD 1
#include <locale.h>
#include <stdlib.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace orosmo::core
{

namespace
{

#if defined(OROSMO_PARSE_USE_STRTOD)
// libc++ d'Apple : std::from_chars des flottants est supprimé quand la cible macOS est ancienne (runner macOS Intel de la release).
// strtod_l / strtof_l dans la locale "C" (arrondi correct), avec les mêmes refus que from_chars.
locale_t cLocale() noexcept
{
    static const locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

bool fromCharsSyntax(std::string_view text) noexcept
{
    if (text.empty())
        return false;
    const std::size_t i = text.front() == '-' ? 1 : 0;
    if (i >= text.size() || text[i] == '+' || text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ||
        text[i] == '\f' || text[i] == '\v')
        return false;
    if (text.size() >= i + 2 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X'))
        return false; // from_chars (format général) ne lit pas l'hexadécimal
    return text.front() != ' ';
}

template <typename T, typename F> std::optional<T> parseWith(std::string_view text, F convert) noexcept
{
    if (!fromCharsSyntax(text) || cLocale() == nullptr)
        return std::nullopt;
    try
    {
        const std::string copy(text); // strtod lit jusqu'au zéro final
        char *end = nullptr;
        errno = 0;
        const T value = convert(copy.c_str(), &end, cLocale());
        if (errno == ERANGE || end != copy.c_str() + copy.size())
            return std::nullopt;
        return value;
    }
    catch (...) // allocation de la copie
    {
        return std::nullopt;
    }
}
#else
template <typename T> std::optional<T> parseWith(std::string_view text) noexcept
{
    T value{};
    const char *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc() || ptr != end)
        return std::nullopt;
    return value;
}
#endif

} // namespace

std::optional<float> parseFloat(std::string_view text) noexcept
{
#if defined(OROSMO_PARSE_USE_STRTOD)
    return parseWith<float>(text, [](const char *s, char **e, locale_t l) { return strtof_l(s, e, l); });
#else
    return parseWith<float>(text);
#endif
}

std::optional<double> parseDouble(std::string_view text) noexcept
{
#if defined(OROSMO_PARSE_USE_STRTOD)
    return parseWith<double>(text, [](const char *s, char **e, locale_t l) { return strtod_l(s, e, l); });
#else
    return parseWith<double>(text);
#endif
}

} // namespace orosmo::core
