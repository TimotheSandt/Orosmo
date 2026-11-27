// UI-08 (A79 lot L8) : point d'entrée d'une vue `.cui` pour le rechargement à chaud. Le C++ généré par `cui` (`<Vue>.gen.cpp`)
// exporte, pour chaque vue, une fonction C `orosmoCuiView_<Vue>()` qui rend une table constante (`CuiViewEntry`) : en développement,
// la vue est compilée dans la bibliothèque de jeu et `app::ViewHost` la retrouve par ce nom dans la version chargée ; en version
// finale, la vue est liée dans l'exécutable et appelée directement (`<Vue>::build`), la fonction exportée ne sert pas.
//
// L'exécutable et la bibliothèque partagent la structure de la vue (`struct <Vue>` de `<Vue>.gen.h`, remplie par l'hôte) :
// `bindings` (empreinte de ses champs et de ses blocs `@cui-cpp`) et `size` disent si la bibliothèque rechargée la lit comme
// l'exécutable compilé avant elle. Sinon, la vue n'est pas reconstruite (il faut relancer le programme reconstruit).
// Aucun état : une table `constexpr` (règle 13, `tools/check_game_static.py`).
#pragma once

#include <cstddef>
#include <cstdint>

namespace orosmo::ui
{

class Ui;
class Widget;

inline constexpr std::uint32_t kCuiViewAbi = 1;
// Préfixe du symbole exporté par vue : "orosmoCuiView_HudView".
inline constexpr const char *kCuiViewSymbolPrefix = "orosmoCuiView_";

struct CuiViewEntry
{
    std::uint32_t abi = 0;       // kCuiViewAbi de la bibliothèque
    std::uint64_t bindings = 0;  // empreinte de la structure de la vue (`<Vue>::cui_bindings`)
    std::size_t size = 0;        // sizeof(<Vue>)
    // Arbre neuf, possédé par l'appelant (`std::unique_ptr<Widget>`), construit depuis `view` (une `<Vue>` de l'hôte).
    Widget *(*build)(const void *view, Ui &ui) = nullptr;
};

} // namespace orosmo::ui

// Même règle que OROSMO_GAME_EXPORT (orosmo/app/game_api.hpp) : sous Windows, une bibliothèque qui exporte un symbole par
// dllexport n'exporte plus les autres, donc les vues le font aussi.
// La structure d'une vue est cachée (visibilité ELF / Mach-O) : quand l'exécutable ET la bibliothèque de jeu contiennent la vue,
// chacun appelle son propre `build` (la bibliothèque rechargée n'appelle pas l'ancien code de l'exécutable, exporté par -rdynamic).
// Sous Windows, une DLL lie déjà ses propres définitions.
#if defined(_WIN32)
#define OROSMO_CUI_VIEW_LOCAL
#else
#define OROSMO_CUI_VIEW_LOCAL __attribute__((visibility("hidden")))
#endif

#if defined(OROSMO_GAME_STATIC)
#define OROSMO_CUI_VIEW_EXPORT extern "C"
#elif defined(_WIN32)
#define OROSMO_CUI_VIEW_EXPORT extern "C" __declspec(dllexport)
#else
#define OROSMO_CUI_VIEW_EXPORT extern "C" __attribute__((visibility("default")))
#endif
