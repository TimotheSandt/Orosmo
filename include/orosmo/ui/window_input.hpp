// Entrées de l'interface lues sur une fenêtre du moteur (platform) : souris, molette, texte, touches d'édition avec la répétition du
// clavier, raccourcis ("Ctrl+S"), Maj ; presse-papiers du système. Les applications n'ont plus à traduire les touches elles-mêmes.
#pragma once

#include "engine/ui/ui.hpp"

class Window;

namespace engine::ui
{

// Entrées de l'image. À appeler après `Window::NewFrame` (qui met à jour l'InputManager de la fenêtre).
InputFrame readWindowInput(Window &window, float deltaTime);

// Les champs de texte utilisent le presse-papiers du système (Ctrl+C / X / V). `window` doit vivre plus longtemps que `ui`.
void useSystemClipboard(Ui &ui, Window &window);

} // namespace engine::ui
