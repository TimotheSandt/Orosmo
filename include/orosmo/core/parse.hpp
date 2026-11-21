// Lecture de nombres indépendante de la locale et du système : les mêmes octets donnent la même valeur partout (thèmes JSON,
// champs de l'interface). Syntaxe de std::from_chars (format général) : ni espace ni `+` en tête, ni préfixe hexadécimal, texte
// entièrement consommé ; un nombre hors de la plage du type est refusé.
#pragma once

#include <optional>
#include <string_view>

namespace orosmo::core
{

std::optional<float> parseFloat(std::string_view text) noexcept;
std::optional<double> parseDouble(std::string_view text) noexcept;

} // namespace orosmo::core
