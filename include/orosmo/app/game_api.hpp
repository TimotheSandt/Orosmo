// Interface C entre le moteur et la bibliothèque de jeu (GAME-01, A79 lot L5).
//
// La bibliothèque de jeu exporte UNE fonction de liaison C :
//
//     OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
//
// qui rend une table constante (`constexpr`, en lecture seule dans le binaire) : version d'ABI et quatre points d'entrée.
// Le moteur la cherche par son nom (`kGameApiSymbol`) dans la copie chargée (`platform::DynamicLibrary`) en développement, ou
// l'appelle directement dans la version finale (`OROSMO_GAME_STATIC` : la logique est liée statiquement dans l'exécutable,
// GAME-07). Seul ce nom passe par l'éditeur de liens dynamique : le reste (`GameContext`, composants, systèmes, actions) est du
// C++ ordinaire résolu dans l'exécutable hôte (même moteur, même compilateur ; liaison de A79 L1).
//
// Règle 13 : la bibliothèque ne garde AUCUN état (ni variable globale ou `static` modifiable, ni `thread_local`) ; tout passe par
// le contexte, dont le moteur est propriétaire. `tools/check_game_static.py` le vérifie sur le binaire.
#pragma once

#include <cstdint>
#include <string_view>

namespace orosmo::app
{
class GameContext;

// Version de l'interface. Une bibliothèque compilée avec une autre valeur est refusée (kVersionMismatch). À incrémenter à tout
// changement de `OrosmoGameApi` ou de la disposition de `GameContext`.
inline constexpr std::uint32_t kGameAbiVersion = 1;

// Nom du symbole cherché dans la bibliothèque.
inline constexpr std::string_view kGameApiSymbol = "orosmoGameApi";
} // namespace orosmo::app

extern "C"
{
    // Points d'entrée du jeu. `context` est valide pendant l'appel ; la bibliothèque ne le garde pas.
    //   init     : à chaque chargement d'une version (premier chargement et chaque rechargement) ; enregistre par nom les
    //              composants, systèmes et actions. Faux = la version ne démarre pas : en rechargement, l'ancienne reste active.
    //   update   : une fois par image (`GameModule::update`), hors des pas de simulation (facultatif, nul permis).
    //   onReload : sur la NOUVELLE version, après un `init` réussi qui remplace une version déjà active (facultatif).
    //   shutdown : à l'arrêt définitif du module (`GameModule::stop`, destruction), jamais au rechargement : l'état est dans le
    //              moteur et survit (facultatif).
    struct OrosmoGameApi
    {
        std::uint32_t abiVersion; // = orosmo::app::kGameAbiVersion, lu avant tout appel
        const char *name;         // nom du jeu (journal), peut être nul
        bool (*init)(orosmo::app::GameContext *context);
        void (*update)(orosmo::app::GameContext *context, double dt);
        void (*onReload)(orosmo::app::GameContext *context);
        void (*shutdown)(orosmo::app::GameContext *context);
    };
}

// Déclaration de la fonction de liaison, avec l'export qui convient : rien en version finale statique (OROSMO_GAME_STATIC,
// fonction ordinaire de l'exécutable), sinon exportée par la bibliothèque dynamique.
#if defined(OROSMO_GAME_STATIC)
#define OROSMO_GAME_EXPORT extern "C"
#elif defined(_WIN32)
#define OROSMO_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define OROSMO_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi();
