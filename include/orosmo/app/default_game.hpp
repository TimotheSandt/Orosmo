// Réglages par défaut d'un projet `orosmo init` (A79 lots L5, L13 et L14), à inclure par le SEUL fichier qui crée l'application :
// leur contenu dépend des options de compilation du projet (`OROSMO_GAME_STATIC`, `OROSMO_CUI_RELOADABLE`, `DEBUG`), et deux
// fichiers compilés avec des options différentes donneraient deux définitions différentes d'une même fonction en ligne.
//
//     return orosmo::app::Application::main(argc, argv, orosmo::app::defaultSettings("Mon jeu"), setup);
//
//   - `defaultGame()` : la version finale (`make release`, `OROSMO_GAME_STATIC`) démarre la table liée dans l'exécutable, la
//     version de développement charge la bibliothèque `game` à côté de l'exécutable et la recharge à chaud ;
//   - `defaultViews()` : en développement (`OROSMO_CUI_RELOADABLE`, vues compilées dans la bibliothèque de jeu), les `.cui` de
//     `<projet>/src` sont observés et `make game BUILD_TYPE=<type>` est lancé dans `<projet>` quand l'un d'eux est enregistré ; le
//     projet et le type sont déduits du chemin de l'exécutable, `<projet>/bin/<type>/` (G363). Sinon : rien (vue liée) ;
//   - `defaultSettings(titre)` : les deux, plus les couches de validation et le journal `logs/log.log` en debug seulement
//     (`DEBUG`) ; sans `DEBUG`, pas de validation et le journal horodaté du dossier de données.
#pragma once

#include <orosmo/app/application.hpp>
#include <orosmo/app/game_api.hpp>
#include <orosmo/app/view_host.hpp>
#include <orosmo/core/system.hpp>

#include <filesystem>
#include <string>
#include <utility>

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

inline ViewHostSettings defaultViews()
{
    ViewHostSettings views;
#if defined(OROSMO_CUI_RELOADABLE)
    const std::filesystem::path bin = core::system::executablePath().parent_path(); // <projet>/bin/<debug|dev>
    const std::filesystem::path project = bin.parent_path().parent_path();
    views.sources = project / "src";
    views.buildCommand = "make game BUILD_TYPE=" + bin.filename().string();
    views.buildDirectory = project;
#endif
    return views;
}

inline ApplicationSettings defaultSettings(std::string title)
{
    ApplicationSettings settings;
    settings.title = std::move(title);
#if defined(DEBUG)
    settings.logFile = "logs/log.log";
#else
    settings.validation = false;
#endif
    settings.game = defaultGame();
    settings.views = defaultViews();
    return settings;
}

} // namespace orosmo::app
