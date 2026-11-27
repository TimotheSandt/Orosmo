// SIM-01 (A79 lot L3) : empreinte de l'état d'un `World`, pour vérifier qu'une simulation rejouée, ou capturée puis restaurée,
// retrouve exactement le même état, et pour figer cet état dans un test (même valeur sur toutes les plateformes).
//
// Hache les VALEURS DES CHAMPS DÉCRITS (`ComponentDesc::fields`), jamais les octets bruts (remplissage non initialisé, cache
// monde de `Transform` non décrit) : table des entités (générations, pile des indices libres), hiérarchie (parents et ordre
// des enfants), puis chaque type de composant non vide, par nom croissant (indépendant de l'ordre d'enregistrement) : nom,
// version, entités dans l'ordre dense du stockage (l'ordre de parcours fait partie de l'état rejoué) et valeurs. Réels par
// motif de bits exact. FNV-1a 64 bits (`core::Fnv1a`), non cryptographique.
#pragma once

#include <orosmo/core/hash.hpp>
#include <orosmo/scene/world.hpp>

#include <cstdint>

namespace orosmo::scene
{

std::uint64_t fingerprint(const World &world);
// Ajoute l'état du monde à une empreinte en cours (état d'une simulation : monde + générateur + numéro de pas).
void hashWorld(core::Fnv1a &hash, const World &world);

} // namespace orosmo::scene
