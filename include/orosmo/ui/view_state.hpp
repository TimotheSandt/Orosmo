// UI-08 (A79 lot L8) : remplacement d'un arbre de widgets en gardant l'état d'interaction, pour le rechargement à chaud des vues
// `.cui` (`app::ViewHost`). L'état est relevé par identifiant de widget (`Widget::setId`, `.id("...")` dans le langage) : focus
// (et son anneau visible), défilement (ScrollView, TextArea), sections ouvertes (Section) et onglet choisi non lié (Tabs). Un widget
// sans identifiant, ou dont l'identifiant a disparu de la nouvelle vue, repart de son état initial. Les valeurs liées (`$state.x`)
// sont dans l'état de l'application : elles n'ont pas besoin d'être gardées ici.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/ui/widget.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace orosmo::ui
{

class Ui;

struct ViewState
{
    std::string focused; // identifiant du widget focalisé (vide : aucun, ou sans identifiant)
    bool focusVisible = false;
    // Par identifiant (le premier dans l'ordre de l'arbre si plusieurs widgets le partagent, comme `Widget::find`).
    std::map<std::string, RetainedState, std::less<>> widgets;
};

// Relève l'état de l'arbre de `ui` (racine et sous-arbres détachés : corps de boîte de dialogue).
ViewState captureViewState(const Ui &ui);
// Applique `state` à l'arbre courant de `ui` : avant sa première mise en page, le défilement est borné par la suivante.
void restoreViewState(Ui &ui, const ViewState &state);
// Relève l'état, remplace la racine (`Ui::setRoot`), puis applique l'état au nouvel arbre. Hors de `Ui::update` seulement :
// kBusy pendant une mise à jour (le remplacement y serait différé, l'état appliqué à l'ancien arbre), et la racine est alors
// rendue intacte à l'appelant par `root`.
core::Result<> replaceRoot(Ui &ui, std::unique_ptr<Widget> &root);
// Nombre de widgets du sous-arbre (lui-même compris, sous-arbres détachés compris).
std::size_t countWidgets(const Widget &root);

} // namespace orosmo::ui
