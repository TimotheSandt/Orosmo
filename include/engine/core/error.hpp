// CORE-02 : gestion d'erreurs. Les erreurs attendues se retournent par std::expected ; aucune exception dans le flux normal.
#pragma once

#include <cstdint>
#include <expected>
#include <source_location>
#include <string>
#include <utility>

namespace engine::core
{

enum class ErrorCode : std::uint16_t
{
    kUnknown = 0,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kIo,
    kParse,
    kUnsupported,
    kOutOfRange,
    kCancelled,
    kBusy,
    kVersionMismatch,
};

const char *toString(ErrorCode code) noexcept;

struct Error
{
    ErrorCode code = ErrorCode::kUnknown;
    std::string message;
    std::source_location where;

    // "kNotFound: res://a.json (src/x.cpp:12)"
    std::string describe() const;
};

// Result<T> : valeur ou Error. Result<> : succès sans valeur.
template <typename T = void> using Result = std::expected<T, Error>;

inline std::unexpected<Error> makeError(ErrorCode code, std::string message,
                                        std::source_location where = std::source_location::current())
{
    return std::unexpected<Error>(Error{code, std::move(message), where});
}

// Écrit l'erreur dans le logger existant (niveau ERROR) et la renvoie telle quelle, pour `return logged(err)`.
const Error &logError(const Error &error);

// Assertion avec message : toujours active en debug, retirée en release (ENGINE_ASSERT_ALWAYS reste active).
// Un échec écrit dans le logger, puis appelle le gestionnaire (par défaut : std::abort). Les tests le remplacent.
using AssertHandler = void (*)(const char *expression, const char *message, std::source_location where);
AssertHandler setAssertHandler(AssertHandler handler) noexcept;
[[noreturn]] void assertFailed(const char *expression, const char *message, std::source_location where);

} // namespace engine::core

#define ENGINE_ASSERT_ALWAYS(cond, msg)                                                                              \
    do                                                                                                               \
    {                                                                                                                \
        if (!(cond)) [[unlikely]]                                                                                    \
            ::engine::core::assertFailed(#cond, (msg), std::source_location::current());                            \
    } while (0)

#ifdef NDEBUG
#define ENGINE_ASSERT(cond, msg) ((void)0)
#else
#define ENGINE_ASSERT(cond, msg) ENGINE_ASSERT_ALWAYS(cond, msg)
#endif
