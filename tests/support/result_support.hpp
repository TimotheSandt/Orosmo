// Lecture sûre de l'erreur d'un `Result` dans un test : `errorOf(r)` vaut le code d'erreur, ou rien si `r` est un succès. Appeler
// `r.error()` sur un succès viole la précondition de std::expected (comportement indéfini) au lieu de faire échouer le test ;
// `CHECK((errorOf(r) == ErrorCode::kX))` échoue proprement si une régression renvoie un succès.
#pragma once

#include <doctest/doctest.h>
#include <orosmo/core/error.hpp>

#include <optional>
#include <string>
#include <utility>

namespace tests
{

template <typename T> std::optional<orosmo::core::ErrorCode> errorOf(const orosmo::core::Result<T> &result)
{
    if (result)
    {
        return std::nullopt;
    }
    return result.error().code;
}

// Valeur d'un `Result` attendu en succès : le test s'arrête (REQUIRE) avec le message d'erreur au lieu de déréférencer une erreur.
template <typename T> T valueOf(orosmo::core::Result<T> result)
{
    INFO((result ? std::string() : result.error().message));
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace tests
