// Source de la logique de jeu d'un projet `orosmo init` (A79 lots L5 et L13), à inclure par le SEUL fichier qui crée
// l'application : la version finale (`make release`, `OROSMO_GAME_STATIC`) démarre la table liée dans l'exécutable, la version
// de développement charge la bibliothèque `game` à côté de l'exécutable et la recharge à chaud.
//
//     settings.game = orosmo::app::defaultGame();
#pragma once

#include <orosmo/app/application.hpp>
#include <orosmo/app/game_api.hpp>

namespace orosmo::app
{

inline GameSource defaultGame()
{
#if defined(OROSMO_GAME_STATIC)
    return GameSource::fromApi(orosmoGameApi());
#else
    return GameSource::fromLibrary("game");
#endif
}

} // namespace orosmo::app
