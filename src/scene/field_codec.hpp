// ECS-03 (A79 lot L3), interne au module : lecture et écriture des champs décrits d'un composant (`core::FieldDesc`, décalage
// et taille) en JSON, et hachage de leurs valeurs. Jamais les octets bruts de la structure : le remplissage n'est ni écrit ni
// haché, et une donnée hors de toute description (cache monde de `Transform`) non plus.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/hash.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/core/serialization.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace orosmo::scene::detail
{

// Objet JSON {champ: valeur} des champs décrits, lus à partir de `base`.
core::Json encodeFields(const std::vector<core::FieldDesc> &fields, const std::byte *base);

// Écrit à partir de `base` les champs présents dans `object` (un champ absent garde la valeur déjà en place). Champ inconnu :
// ignoré, un avertissement « chemin: unknown field ignored » est ajouté à `warnings`. Type incompatible : kParse ; valeur hors
// de la plage du champ ou de son type : kOutOfRange ; `path` préfixe les messages.
core::Result<> decodeFields(const std::vector<core::FieldDesc> &fields, std::byte *base, const core::Json &object,
                            const std::string &path, std::vector<std::string> &warnings);

// Hache les valeurs des champs décrits (motif de bits exact des réels, entiers sur 64 bits).
void hashFields(core::Fnv1a &hash, const std::vector<core::FieldDesc> &fields, const std::byte *base);

} // namespace orosmo::scene::detail
