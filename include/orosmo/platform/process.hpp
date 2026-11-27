// Commande lancée par l'interpréteur du système (A79 lot L8, UI-08 : le programme en développement reconstruit ses vues `.cui`
// avec la bibliothèque de jeu). Bloquant : l'appelant la lance sur un autre thread s'il ne veut pas attendre.
#pragma once

#include <orosmo/core/error.hpp>

#include <filesystem>
#include <string>

namespace orosmo::platform
{

struct CommandResult
{
    int exitCode = 0;   // code de sortie (-1 : arrêtée par un signal)
    std::string output; // sorties standard et d'erreur réunies, dans l'ordre d'écriture
};

// `command` est passée à `sh -c` (Windows : `cmd.exe /c`), lancée dans `directory` (vide : dossier courant), sorties réunies.
// Erreurs : kInvalidArgument (commande vide), kIo (dossier absent, processus impossible à créer). Une commande qui échoue n'est
// pas une erreur : voir `exitCode`.
core::Result<CommandResult> runCommand(const std::string &command, const std::filesystem::path &directory = {});

} // namespace orosmo::platform
