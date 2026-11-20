// Chemins des ressources d'une application (repris de l'ancien `Utilities/Utilities.h`, comportement inchangé).
//
//   GET_RESOURCE_PATH("fonts/x.ttf")  chemin d'une ressource : "res/fonts/x.ttf" en debug, "fonts/x.ttf" en release (le paquet
//                                     installé pose les ressources à côté de l'exécutable)
//   setWorkingDirectoryToExecutable() le dossier courant devient celui de l'exécutable (les chemins ci-dessus sont relatifs)
//   validateAssets(...)               vérifie la présence de fichiers et journalise ceux qui manquent
//
// Le chemin de l'exécutable et le dossier de données de l'utilisateur sont dans engine/core/system.hpp.
#pragma once

#include <string>
#include <vector>

#ifdef RELEASE
#define GET_RESOURCE_PATH(path) path
#else
#define GET_RESOURCE_PATH(path) "res/" path
#endif

namespace engine::core
{

// Change le dossier courant pour celui de l'exécutable ; journalise une erreur s'il ne peut pas (dossier courant inchangé).
void setWorkingDirectoryToExecutable();

// Vrai si tous les fichiers existent. Chaque absent est journalisé (LOG_ERROR) et, si `missingAssets` est donné, ajouté à la
// liste (vidée d'abord, chemins au format générique).
bool validateAssets(const std::vector<std::string> &assetPaths, std::vector<std::string> *missingAssets = nullptr);

} // namespace engine::core
