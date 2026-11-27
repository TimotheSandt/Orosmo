// ECS-03 (A79 lot L3) : sauvegarde et chargement d'un `World`, en JSON lisible ou en binaire compact (MessagePack), par
// CORE-12 (`core/serialization.hpp`), versionnés.
//
// Document : {"version": kSceneFormatVersion, "data": {"entities": {"generations": [...], "free": [...]},
//             "hierarchy": [[enfant, parent], ...], "components": [{"name", "version", "entities": [...], "values": [...]}]}}
//   - Table des entités entière (générations, pile des indices libres) : les identifiants sont les mêmes après chargement, donc
//     une référence `Entity` dans un composant reste valide, et les `create` suivants rendent les mêmes indices.
//   - Hiérarchie : paires dans l'ordre des enfants (ordre des frères gardé).
//   - Composants : par type, dans l'ordre dense du stockage (gardé au chargement : l'ordre de parcours, donc le résultat d'une
//     simulation rejouée, ne change pas). Chaque valeur est un objet {champ: valeur} écrit à partir de la description des
//     champs (`ComponentDesc::fields`), jamais les octets bruts. Réels écrits au plus court qui se relit à l'identique ; NaN et
//     infinis en chaînes "nan", "inf", "-inf".
//
// Chargement : par NOM de type, dans un monde où les types sont déjà enregistrés (le moteur ou la bibliothèque de jeu les
// enregistre d'abord). Champ manquant : valeur par défaut du type ; champ inconnu ou type de composant non enregistré : ignoré,
// avec un avertissement (journal et `SceneLoadReport`). Version de composant plus ancienne : migrations du type
// (`SceneMigrations::components`), appliquées à chaque valeur ; plus récente, ou migration absente : kVersionMismatch. Tout
// ou rien : en cas d'erreur, le monde est inchangé. Le cache monde de `Transform` (non décrit) est recalculé à la fin.
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/serialization.hpp>
#include <orosmo/scene/world.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace orosmo::scene
{

// Version du format du document de scène (pas des composants : chacun a la sienne, `ComponentDesc::version`).
inline constexpr int kSceneFormatVersion = 1;

struct SceneMigrations
{
    core::MigrationTable format;                                         // document : version n -> n + 1 (clé n)
    std::map<std::string, core::MigrationTable, std::less<>> components; // par nom de type : valeur v -> v + 1 (clé v)
};

struct SceneLoadReport
{
    std::vector<std::string> warnings; // champs inconnus, types non enregistrés (aussi journalisés)
    std::size_t entities = 0;
    std::size_t components = 0; // valeurs de composants chargées
};

core::Json saveScene(const World &world);
core::Result<SceneLoadReport> loadScene(World &world, const core::Json &document, const SceneMigrations &migrations = {});

std::vector<std::uint8_t> saveSceneBinary(const World &world);
core::Result<SceneLoadReport> loadSceneBinary(World &world, std::span<const std::uint8_t> bytes, const SceneMigrations &migrations = {});

// Fichier : JSON si l'extension est « .json », MessagePack sinon. kIo si le fichier ne s'ouvre pas.
core::Result<> saveSceneFile(const World &world, const std::filesystem::path &path);
core::Result<SceneLoadReport> loadSceneFile(World &world, const std::filesystem::path &path, const SceneMigrations &migrations = {});

// Composant seul (inspecteur, migration) : objet {champ: valeur} écrit à partir de la description, et lecture dans `data`
// (`desc.size` octets ; les champs absents gardent la valeur en place).
core::Json componentToJson(const ComponentDesc &desc, const void *data);
core::Result<> componentFromJson(const ComponentDesc &desc, void *data, const core::Json &value, std::vector<std::string> &warnings);

} // namespace orosmo::scene
