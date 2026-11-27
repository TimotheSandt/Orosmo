// Bibliothèque de jeu de test qui contient une vue .cui (UI-08, A79 lot L8) : apps/demo/Makefile la lie avec le C++ généré de
// tests/app/view_module/<v>/HotView.cui, une bibliothèque par version (`bin/tests/test_view_<v><ext>`), chargées et rechargées
// par tests/app/view_host_tests.cpp. La logique ne fait rien : seule la vue change d'une version à l'autre.
#include <orosmo/game.hpp>

namespace
{
bool init(orosmo::app::GameContext *) { return true; }

constexpr OrosmoGameApi kApi{orosmo::app::kGameAbiVersion, "view-test", &init, nullptr, nullptr, nullptr};
} // namespace

OROSMO_GAME_EXPORT const OrosmoGameApi *orosmoGameApi() { return &kApi; }
