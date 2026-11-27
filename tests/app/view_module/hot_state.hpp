// État lié à la vue de test HotView (tests/app/view_module/<v>/HotView.cui) : tenu par l'hôte (le test), il survit au
// rechargement de la bibliothèque qui contient la vue (UI-08, A79 lot L8).
#pragma once

#include <string>

struct HotState
{
    std::string title = "hot";
    bool flag = false;
    int presses = 0;

    void press() { ++presses; }
};
