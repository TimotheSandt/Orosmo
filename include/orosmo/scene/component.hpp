// ECS-01 (A79 lot L2, choix 1 et 2) : description d'un type de composant, enregistrée PAR NOM dans un `World`.
//
// Le moteur ne garde aucun type C++, aucune table virtuelle ni identifiant de type issu de la bibliothèque qui définit le
// composant : seulement son nom, sa taille, son alignement, sa version, une description de ses champs (`core::FieldDesc`,
// décalages et tailles) et les octets de sa valeur par défaut, tous copiés et possédés par le moteur. Le stockage survit
// ainsi au déchargement de la bibliothèque de jeu (GAME-01, lot L5) et la description sert la sauvegarde (L3), la migration
// (L6) et l'inspecteur (L10). Conséquence : un composant est une donnée trivialement copiable (déplacé par `memcpy`, sans
// constructeur ni destructeur appelé par le moteur).
//
//   struct Velocity { orosmo::core::Vec3 value{0.f}; };
//   template <> struct orosmo::core::Reflect<Velocity> {
//       static constexpr auto fields() { return std::make_tuple(field("value", &Velocity::value)); }
//   };
//   auto velocity = world.registerComponent<Velocity>("Velocity");   // Result<ComponentType<Velocity>>
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/reflection.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace orosmo::scene
{

struct ComponentDesc
{
    std::string name;
    std::size_t size = 0;      // octets, multiple de `alignment`
    std::size_t alignment = 1; // puissance de deux, au plus kMaxAlignment
    int version = 1;           // version des données (migration, L3 et L6)
    std::vector<core::FieldDesc> fields;
    std::vector<std::byte> defaultValue; // `size` octets, ou vide : composant initialisé à zéro

    static constexpr std::size_t kMaxAlignment = 4096;
    static constexpr std::size_t kMaxSize = std::size_t{1} << 20; // 1 Mio : un composant est une petite donnée


    // Même disposition (nom, taille, alignement, version ; nom, type, décalage, taille et sous-champs de chaque champ ; les
    // textes d'aide et les plages n'en font pas partie) : un nouvel enregistrement met alors seulement la description à jour.
    bool sameLayout(const ComponentDesc &other) const;
};

// Erreur kInvalidArgument qui nomme le champ fautif : nom vide, taille nulle ou non multiple de l'alignement, alignement
// invalide, valeur par défaut de mauvaise taille, champ qui déborde de sa structure, champ chaîne ou tableau dynamique (non
// trivialement copiable).
core::Result<> validateComponentDesc(const ComponentDesc &desc);
// Comparaison de disposition de deux listes de champs (voir `sameLayout`).
bool sameFieldLayout(const std::vector<core::FieldDesc> &a, const std::vector<core::FieldDesc> &b);

// Type de composant enregistré dans un `World` : emplacement dans la table et génération (un identifiant gardé après le retrait
// du type est refusé, même si l'emplacement a été réutilisé).
struct ComponentId
{
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    constexpr bool isNull() const noexcept { return index == kInvalidIndex; }
    friend constexpr bool operator==(const ComponentId &, const ComponentId &) = default;
};

// `ComponentId` dont la taille et l'alignement ont été vérifiés contre T (`World::componentType<T>`, `registerComponent<T>`).
template <typename T> struct ComponentType
{
    ComponentId id;
};

template <typename T>
concept ComponentData = std::is_trivially_copyable_v<T> && std::is_class_v<T>;

// Description d'un type C++ réfléchi. `Reflect<T>::kVersion` donne la version s'il existe (sinon 1) ; la valeur par défaut
// est `T{}` (octets de remplissage à zéro).
template <core::Reflected T>
    requires ComponentData<T>
ComponentDesc describeComponent(std::string name)
{
    ComponentDesc d;
    d.name = std::move(name);
    d.size = sizeof(T);
    d.alignment = alignof(T);
    if constexpr (requires { core::Reflect<T>::kVersion; })
        d.version = core::Reflect<T>::kVersion;
    d.fields = core::describeFields<T>();
    if constexpr (std::is_default_constructible_v<T>)
    {
        // T{} construit dans un tampon mis à zéro : octets de remplissage (et unique octet d'un type vide) à zéro, donc
        // définis. Copie directe des octets, sans resize puis memcpy (GCC 16 voit un faux débordement du memset de resize
        // pour un type d'un octet, -Werror=array-bounds sous MinGW). T est trivialement copiable : rien à détruire.
        alignas(T) std::byte storage[sizeof(T)]{};
        ::new (static_cast<void *>(storage)) T{};
        d.defaultValue.assign(storage, storage + sizeof(T));
    }
    return d;
}

} // namespace orosmo::scene
