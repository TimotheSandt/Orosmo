// Services du système d'exploitation dont le noyau a besoin (largeur de la console, chemin de l'exécutable, dossier de
// données utilisateur, attente précise). Ce fichier ne fait que DÉCLARER : les définitions sont dans src/platform/system.cpp,
// seul endroit qui inclut <windows.h> et les en-têtes POSIX (docs/03 §6 et §8.2). core ne dépend donc pas de platform à la
// compilation (aucun #include vers le haut) ; il appelle un port que platform implémente. Si une plateforme n'a pas
// d'implémentation, l'édition de liens échoue au lieu de masquer l'absence.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace engine::core::system
{

// Nombre de colonnes de la console attachée à la sortie standard ; 80 s'il n'y en a pas (redirection, pas de terminal).
std::size_t terminalWidth();

// Chemin absolu de l'exécutable en cours ; vide si le système ne le donne pas.
std::filesystem::path executablePath();

// Dossier de données utilisateur de la plateforme, sans le nom de l'application :
//   Windows %APPDATA%, macOS ~/Library/Application Support, Linux $XDG_DATA_HOME ou ~/.local/share.
// Vide si le système n'en donne pas (pas de HOME par exemple).
std::filesystem::path userDataDirectory();

// Attend `duration` avec la meilleure précision disponible : nanosleep (repris après une interruption par signal) sous Linux,
// Sleep + QueryPerformanceCounter sous Windows, attente active pour les très courtes durées. Une durée nulle ou négative
// revient immédiatement. Linux et autres : au moins `duration`. Windows : la partie en millisecondes passe par Sleep() et est
// donc tronquée à la milliseconde (1,9 ms attend 1 ms) ; c'était déjà le comportement du limiteur d'images avant M1 bis, qui
// compense avec son propre seuil d'attente active (`FPSCounter::spinThreshold`).
void preciseSleep(std::chrono::nanoseconds duration);

// Identifiant du processus en cours (getpid, GetCurrentProcessId) : sert à nommer des fichiers temporaires propres au
// processus (cache de shaders écrit par plusieurs programmes à la fois).
std::uint64_t processId();

// Demande une résolution de minuteur système de 1 ms (Windows : timeBeginPeriod(1)). Renvoie true si la demande a été
// acceptée ; à appairer avec endHighResolutionTimer(). Sans effet (false) sur les autres systèmes.
bool beginHighResolutionTimer();
void endHighResolutionTimer();

} // namespace engine::core::system
