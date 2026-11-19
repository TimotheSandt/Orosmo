// UI-07 : fond en neuf parties. Les quatre coins gardent leur taille (en texels × `scale`) ; les bords et le centre remplissent le
// reste, étirés, répétés (dernière répétition coupée, sans déformation) ou supprimés. Si le widget est plus petit que deux
// coins, les coins sont réduits ensemble (proportionnellement).
#pragma once

#include "engine/renderer/draw_list.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/widget.hpp"

#include <vector>

namespace engine::ui
{

// Sommets (x, y, u, v ; 6 par quadrilatère) de `image` posée sur `rect`. Les coordonnées de texture sont rentrées d'un
// demi-texel dans chaque morceau : le filtrage linéaire ne mélange pas deux morceaux voisins de l'image.
std::vector<float> nineSliceVertices(const ImageStyle &image, const Rect &rect);

// Ajoute l'image à la liste (une seule commande, quel que soit le nombre de morceaux).
void appendNineSlice(renderer::DrawList &list, const ImageStyle &image, const Rect &rect, const Color &tint);

} // namespace engine::ui
