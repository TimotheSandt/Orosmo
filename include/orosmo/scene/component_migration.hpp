// GAME-03 (A79 lot L6, choix 4 modifié) : migration d'un type de composant dont la disposition change, faite par le MOTEUR à
// partir des descriptions copiées à l'enregistrement (`ComponentDesc`), jamais par la bibliothèque qui définit le type : elle
// marche même si l'ancienne version a échoué ou est déjà déchargée.
//
// Un seul chemin : chaque valeur est écrite champ par champ avec l'ANCIENNE description (`componentToJson`, comme la
// sauvegarde de scène), puis relue PAR NOM DE CHAMP dans la valeur par défaut de la NOUVELLE description.
//   - kCopied (« migration par réflexion ») : chaque ancien champ existe encore, même nom, même type et même taille (des champs
//     ont été ajoutés, déplacés, ou seule la version change). Champs communs copiés à l'identique, nouveaux à leur valeur par
//     défaut ; aucune perte.
//   - kRestored (« repli sauvegarde/restauration ») : un champ a été retiré ou a changé de type. La valeur sauvegardée est
//     restaurée champ par champ : champ retiré ignoré, champ dont le type change converti si sa valeur se relit dans le
//     nouveau type (entier vers réel…), sinon laissé à sa valeur par défaut ; chaque perte est un avertissement.
#pragma once

#include <orosmo/core/reflection.hpp>
#include <orosmo/scene/component.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orosmo::scene
{

enum class MigrationKind : std::uint8_t
{
    kCopied,
    kRestored,
};

const char *migrationKindName(MigrationKind kind) noexcept;

// Compte rendu de `World::migrateComponent` (et de `app::GameModule::lastMigrations`).
struct ComponentMigration
{
    std::string name;
    ComponentId id; // nouvel identifiant (même emplacement, génération suivante : les anciens identifiants sont refusés)
    MigrationKind kind = MigrationKind::kCopied;
    int fromVersion = 1;
    int toVersion = 1;
    std::size_t values = 0;            // valeurs migrées (entités qui avaient le composant)
    std::vector<std::string> warnings; // pertes du repli, une fois par champ (« Nom.champ: ... »)
};

// kCopied si chaque champ de `from` a dans `to` un champ de même nom, même type et même taille (sous-champs comparés de même),
// sinon kRestored.
MigrationKind classifyMigration(const std::vector<core::FieldDesc> &from, const std::vector<core::FieldDesc> &to);

// Migre une valeur : `src` (`from.size` octets, disposition `from`) vers `dst` (`to.size` octets), qui doit déjà contenir la
// valeur de départ (en général la valeur par défaut de `to`). Les pertes sont ajoutées à `warnings` (sans doublon).
void migrateComponentValue(const ComponentDesc &from, const void *src, const ComponentDesc &to, void *dst,
                           std::vector<std::string> &warnings);

} // namespace orosmo::scene
