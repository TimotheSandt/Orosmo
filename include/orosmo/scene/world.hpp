// ECS-01, ECS-02 (A79 lot L2) : monde d'entités, ECS maison (choix 1 d'A79).
//
//   - Entités : indice + génération (`Entity`), indices libérés réutilisés, génération incrémentée à chaque destruction.
//   - Composants : types enregistrés par NOM avec une description copiée (`ComponentDesc`) ; un stockage contigu par type
//     (`ComponentPool`). Accès non typé par `ComponentId` ou par nom (sauvegarde, inspecteur, bibliothèque de jeu), ou typé
//     par `ComponentType<T>` (taille et alignement vérifiés une fois).
//   - Requêtes : `view(a, b).each([](Entity, A &, B &) {...})` parcourt le plus petit des stockages ; `forEach` non typé.
//   - Hiérarchie : parent, enfants dans l'ordre d'ajout ; détruire une entité détruit ses descendants.
//   - Transformations : composant « Transform » (`transform.hpp`) ; la partie monde n'est recalculée que pour les entités
//     marquées sales (ajout, modification par `setTransform` / `editTransform`, changement de parent) et leurs descendants.
//
// Pas de changement de structure (création, destruction, ajout ou retrait de composant, changement de parent) pendant un
// parcours `each` / `forEach` : vérifié par ENGINE_ASSERT en debug. Un `World` n'est pas thread-safe (un propriétaire). Il est
// copiable (copie profonde, servira la capture d'état de L3).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/component_migration.hpp>
#include <orosmo/scene/component_pool.hpp>
#include <orosmo/scene/entity.hpp>
#include <orosmo/scene/transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orosmo::scene
{

class World;

// Parcours typé de toutes les entités qui ont les composants T... (voir `World::view`). La vue garde les identifiants des
// types, pas leurs stockages : elle reste utilisable après l'enregistrement d'autres types, et devient invalide (sans
// danger) si l'un de ses types est retiré. Elle ne doit pas survivre à son `World`.
template <typename... T>
    requires(sizeof...(T) > 0)
class View
{
  public:
    // Faux si l'un des types n'est pas (ou plus) enregistré : `each` ne fait alors rien.
    bool valid() const noexcept;

    // fn(Entity, T &...) pour chaque entité qui a tous les composants, dans l'ordre dense du plus petit stockage.
    template <typename Fn> void each(Fn &&fn) const;
    std::size_t count() const;

  private:
    friend class World;
    View(World *world, std::array<ComponentId, sizeof...(T)> ids) : world_(world), ids_(ids) {}

    World *world_ = nullptr;
    std::array<ComponentId, sizeof...(T)> ids_{};
};

class World
{
  public:
    World();
    World(const World &) = default;
    World &operator=(const World &) = default;
    World(World &&) noexcept = default;
    World &operator=(World &&) noexcept = default;
    ~World() = default;

    static constexpr std::string_view kTransformName = "Transform";

    // ── Entités ───────────────────────────────────────────────────────────────
    Entity create();
    // Détruit l'entité, ses descendants et tous leurs composants. Faux si l'entité n'existe plus.
    bool destroy(Entity entity);
    bool alive(Entity entity) const noexcept;
    std::size_t entityCount() const noexcept { return liveCount_; }
    // Entités vivantes par indice croissant.
    std::vector<Entity> entities() const;

    // ── Table des entités (sauvegarde, capture : A79 lot L3) ──────────────────
    // Génération de chaque indice et pile des indices libres (le dernier est réutilisé en premier) : de quoi recréer les
    // mêmes identifiants (les références `Entity` des composants restent valides) et la même suite de `create` ensuite.
    struct EntityTable
    {
        std::vector<std::uint32_t> generations; // par indice ; un indice absent de `freeIndices` est vivant
        std::vector<std::uint32_t> freeIndices;
        friend bool operator==(const EntityTable &, const EntityTable &) = default;
    };
    EntityTable entityTable() const;
    // Vide le monde (entités, données, hiérarchie ; les types enregistrés restent) puis recrée les entités de `table`, sans
    // composant ni parent. kInvalidArgument (monde inchangé) : indice libre hors de la table ou en double.
    core::Result<> resetEntities(const EntityTable &table);

    // ── Types de composants ───────────────────────────────────────────────────
    // Copie la description. Même nom et même disposition (`sameLayout`) : rend l'identifiant existant (et prend la nouvelle
    // valeur par défaut). Même nom, autre disposition : kAlreadyExists. Description invalide : kInvalidArgument.
    core::Result<ComponentId> registerComponent(ComponentDesc desc);
    template <core::Reflected T>
        requires ComponentData<T>
    core::Result<ComponentType<T>> registerComponent(std::string name);
    // Pendant un parcours (`each`, `forEach`, `forEachComponent`), enregistrer un NOUVEAU type ou en retirer un est refusé
    // (kBusy) : la table des types ne change pas sous un parcours.
    // Retire le type et ses données de toutes les entités ; ses identifiants deviennent invalides. « Transform » est refusé.
    core::Result<> unregisterComponent(std::string_view name);
    // GAME-03 (lot L6) : remplace la disposition d'un type déjà enregistré par `desc` et migre ses données
    // (`migrateComponentValue`, par nom de champ, dans l'ordre dense gardé). Le type garde son emplacement mais change de
    // génération : les anciens identifiants (et `View`, `ComponentType<T>`) sont refusés. Même disposition : comme
    // `registerComponent`. Erreurs : kNotFound (type inconnu), kInvalidArgument (description invalide, « Transform »), kBusy
    // (pendant un parcours).
    core::Result<ComponentMigration> migrateComponent(ComponentDesc desc);

    // kNotFound si aucun type ne porte ce nom.
    core::Result<ComponentId> componentId(std::string_view name) const;
    // Comme `componentId`, puis vérifie que T a la taille, l'alignement et, s'il est réfléchi, les champs enregistrés.
    template <typename T>
        requires ComponentData<T>
    core::Result<ComponentType<T>> componentType(std::string_view name) const;
    // Nul si l'identifiant n'est pas (ou plus) valide.
    const ComponentDesc *componentDesc(ComponentId id) const noexcept;
    // Types enregistrés, par emplacement croissant.
    std::vector<ComponentId> componentIds() const;
    // Entités qui ont ce composant, dans l'ordre dense du stockage (ordre de parcours) ; vide si l'identifiant est invalide.
    // Valide jusqu'au prochain ajout ou retrait du même type.
    std::span<const Entity> componentEntities(ComponentId id) const noexcept;
    ComponentType<Transform> transformType() const noexcept { return transformType_; }

    // ── Données (non typées) ──────────────────────────────────────────────────
    // Écrit `value` (`desc.size` octets) ou la valeur par défaut, en ajoutant le composant s'il manque. Rend l'emplacement
    // (valide jusqu'au prochain ajout ou retrait du même type). kNotFound : entité morte ou type inconnu.
    core::Result<void *> add(Entity entity, ComponentId id, const void *value = nullptr);
    core::Result<void *> add(Entity entity, std::string_view componentName, const void *value = nullptr);
    bool remove(Entity entity, ComponentId id);
    bool has(Entity entity, ComponentId id) const noexcept;
    void *get(Entity entity, ComponentId id) noexcept;
    const void *get(Entity entity, ComponentId id) const noexcept;
    // fn(id, données) pour chaque composant de l'entité, par emplacement de type croissant (inspecteur, sauvegarde).
    void forEachComponent(Entity entity, const std::function<void(ComponentId, void *)> &fn);
    // fn(entité, pointeurs des composants dans l'ordre de `ids`) pour chaque entité qui les a tous.
    void forEach(std::span<const ComponentId> ids, const std::function<void(Entity, std::span<void *const>)> &fn);

    // ── Données (typées) ──────────────────────────────────────────────────────
    template <typename T> T *add(Entity entity, ComponentType<T> type, const T &value);
    template <typename T> T *add(Entity entity, ComponentType<T> type);
    template <typename T> T *get(Entity entity, ComponentType<T> type) noexcept;
    template <typename T> const T *get(Entity entity, ComponentType<T> type) const noexcept;
    template <typename T> bool remove(Entity entity, ComponentType<T> type) { return remove(entity, type.id); }
    template <typename T> bool has(Entity entity, ComponentType<T> type) const noexcept { return has(entity, type.id); }
    template <typename... T>
        requires(sizeof...(T) > 0)
    View<T...> view(ComponentType<T>... types);

    // ── Hiérarchie ────────────────────────────────────────────────────────────
    // Rattache `child` à `parent` (en dernier enfant), ou le détache si `parent` est nul. Refuse une entité morte
    // (kNotFound) et un cycle (kInvalidArgument : le parent est l'enfant lui-même ou l'un de ses descendants).
    core::Result<> setParent(Entity child, Entity parent);
    Entity parent(Entity entity) const noexcept;
    Entity firstChild(Entity entity) const noexcept;
    Entity nextSibling(Entity entity) const noexcept;
    std::vector<Entity> children(Entity entity) const;

    // ── Transformations ───────────────────────────────────────────────────────
    // Écrit la partie locale et marque l'entité sale (ajoute le composant s'il manque). kNotFound : entité morte.
    core::Result<> setTransform(Entity entity, const Transform &local);
    // Accès en écriture qui marque l'entité sale ; nul si elle n'a pas de Transform.
    Transform *editTransform(Entity entity);
    const Transform *transform(Entity entity) const noexcept;
    // À appeler après une écriture directe (get, view) dans un Transform.
    void markTransformDirty(Entity entity);
    // Recalcule la partie monde des entités sales et de leurs descendants ; rend le nombre de Transform recalculés. Une
    // entité sans Transform transmet à ses enfants la transformation de son plus proche ancêtre qui en a une.
    std::size_t updateTransforms();
    std::size_t dirtyTransformCount() const noexcept { return dirty_.size(); }

  private:
    template <typename... T>
        requires(sizeof...(T) > 0)
    friend class View;

    // Compte les parcours en cours, y compris quand un rappel sort par une exception.
    struct IterationGuard
    {
        explicit IterationGuard(int &counter) noexcept : counter_(counter) { ++counter_; }
        ~IterationGuard() { --counter_; }
        IterationGuard(const IterationGuard &) = delete;
        IterationGuard &operator=(const IterationGuard &) = delete;
        int &counter_;
    };

    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

    struct EntityRecord
    {
        std::uint32_t generation = 0;
        bool alive = false;
        bool dirty = false;
        std::uint32_t parent = kNone;
        std::uint32_t firstChild = kNone;
        std::uint32_t lastChild = kNone;
        std::uint32_t nextSibling = kNone;
        std::uint32_t prevSibling = kNone;
        // updateTransforms : passe où `coveredByDirty` a été calculé (vrai si un ancêtre strict est sale).
        std::uint32_t coverPass = 0;
        bool coveredByDirty = false;
    };

    struct Slot
    {
        std::uint32_t generation = 0;
        bool used = false;
        ComponentPool pool{ComponentDesc{}};
    };

    ComponentPool *pool(ComponentId id) noexcept;
    const ComponentPool *pool(ComponentId id) const noexcept;
    Entity entityAt(std::uint32_t index) const noexcept;
    void detach(std::uint32_t index) noexcept;
    void markDirty(std::uint32_t index);
    void recomputeSubtree(std::uint32_t root, const Transform *parentWorld, std::size_t &count);
    bool coveredByDirtyAncestor(std::uint32_t index);
    void assertNotIterating() const noexcept;

    std::vector<EntityRecord> records_;
    std::vector<std::uint32_t> freeIndices_;
    std::size_t liveCount_ = 0;
    std::vector<Slot> slots_;
    ComponentType<Transform> transformType_;
    std::vector<std::uint32_t> dirty_;
    std::vector<std::pair<std::uint32_t, const Transform *>> stack_; // parcours de updateTransforms, réutilisé
    std::vector<std::uint32_t> path_;                                 // remontée des ancêtres de updateTransforms, réutilisée
    std::uint32_t coverPass_ = 0;
    int iterating_ = 0;
};

// ── Définitions des modèles ─────────────────────────────────────────────────────

namespace detail
{
template <typename T> T *componentCast(void *p) noexcept { return std::launder(static_cast<T *>(p)); }
template <typename T> const T *componentCast(const void *p) noexcept { return std::launder(static_cast<const T *>(p)); }
core::Result<> checkComponentType(const ComponentDesc &desc, std::size_t size, std::size_t alignment,
                                  const std::vector<core::FieldDesc> *fields);
} // namespace detail

template <core::Reflected T>
    requires ComponentData<T>
core::Result<ComponentType<T>> World::registerComponent(std::string name)
{
    auto id = registerComponent(describeComponent<T>(std::move(name)));
    if (!id)
        return std::unexpected(id.error());
    return ComponentType<T>{*id};
}

template <typename T>
    requires ComponentData<T>
core::Result<ComponentType<T>> World::componentType(std::string_view name) const
{
    auto id = componentId(name);
    if (!id)
        return std::unexpected(id.error());
    std::vector<core::FieldDesc> fields;
    if constexpr (core::Reflected<T>)
        fields = core::describeFields<T>();
    auto ok = detail::checkComponentType(*componentDesc(*id), sizeof(T), alignof(T), core::Reflected<T> ? &fields : nullptr);
    if (!ok)
        return std::unexpected(ok.error());
    return ComponentType<T>{*id};
}

template <typename T> T *World::add(Entity entity, ComponentType<T> type, const T &value)
{
    auto p = add(entity, type.id, &value);
    return p ? detail::componentCast<T>(*p) : nullptr;
}

template <typename T> T *World::add(Entity entity, ComponentType<T> type)
{
    auto p = add(entity, type.id, nullptr);
    return p ? detail::componentCast<T>(*p) : nullptr;
}

template <typename T> T *World::get(Entity entity, ComponentType<T> type) noexcept
{
    void *p = get(entity, type.id);
    return p ? detail::componentCast<T>(p) : nullptr;
}

template <typename T> const T *World::get(Entity entity, ComponentType<T> type) const noexcept
{
    const void *p = get(entity, type.id);
    return p ? detail::componentCast<T>(p) : nullptr;
}

template <typename... T>
    requires(sizeof...(T) > 0)
View<T...> World::view(ComponentType<T>... types)
{
    return View<T...>(this, std::array<ComponentId, sizeof...(T)>{types.id...});
}

template <typename... T>
    requires(sizeof...(T) > 0)
bool View<T...>::valid() const noexcept
{
    if (world_ == nullptr)
        return false;
    for (const ComponentId id : ids_)
        if (world_->pool(id) == nullptr)
            return false;
    return true;
}

template <typename... T>
    requires(sizeof...(T) > 0)
template <typename Fn>
void View<T...>::each(Fn &&fn) const
{
    if (!valid())
        return;
    // Stockages résolus à chaque parcours ; la table des types ne change pas pendant le parcours (kBusy).
    std::array<ComponentPool *, sizeof...(T)> pools{};
    for (std::size_t k = 0; k < pools.size(); ++k)
        pools[k] = world_->pool(ids_[k]);
    std::size_t lead = 0;
    for (std::size_t k = 1; k < pools.size(); ++k)
        if (pools[k]->size() < pools[lead]->size())
            lead = k;
    ComponentPool &leader = *pools[lead];
    const World::IterationGuard guard(world_->iterating_);
    const std::span<const Entity> entities = leader.entities();
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        for (std::size_t i = 0; i < entities.size(); ++i)
        {
            const Entity e = entities[i];
            std::array<void *, sizeof...(T)> data{(I == lead ? leader.at(i) : pools[I]->find(e.index))...};
            bool all = true;
            for (void *d : data)
                all = all && d != nullptr;
            if (all)
                fn(e, *detail::componentCast<T>(data[I])...);
        }
    }(std::index_sequence_for<T...>{});
}

template <typename... T>
    requires(sizeof...(T) > 0)
std::size_t View<T...>::count() const
{
    std::size_t n = 0;
    each([&n](Entity, T &...) { ++n; });
    return n;
}

} // namespace orosmo::scene
