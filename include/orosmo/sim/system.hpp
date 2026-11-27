// Systèmes de la simulation (SIM-01, A79 lots L3 et L7) : phase, contexte d'un pas et description d'un système.
//
// En-tête léger (GAME-06) : ce qu'il faut pour ÉCRIRE un système (bibliothèque de jeu, `orosmo/game.hpp`), sans la boucle de
// simulation ni la sérialisation (`sim/simulation.hpp` tire `nlohmann/json` et la sauvegarde de scène). Le type complet des
// entrées (`Input`, charge JSON) reste dans `sim/simulation.hpp` : un système qui lit `StepContext::inputs` l'inclut.
#pragma once

#include <orosmo/core/memory.hpp>
#include <orosmo/core/random.hpp>
#include <orosmo/scene/world.hpp>

#include <nlohmann/json_fwd.hpp> // déclaration seule : la charge d'un événement ne coûte rien ici

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::core
{
using Json = nlohmann::json; // le même alias que core/serialization.hpp
} // namespace orosmo::core

namespace orosmo::sim
{

enum class Phase : std::uint8_t
{
    kInput,      // lecture des entrées du pas
    kUpdate,     // logique
    kPostUpdate, // conséquences (collisions, nettoyage)
};
std::string_view phaseName(Phase phase) noexcept;
std::optional<Phase> parsePhase(std::string_view name) noexcept;

struct Input; // sim/simulation.hpp
struct Event; // sim/simulation.hpp

// Vue en lecture sur un tableau contigu, utilisable avec un type encore incomplet (`std::span` exige un type complet) : les
// fonctions membres ne sont instanciées qu'à l'usage, là où le type est complet. `for (const Input &i : ctx.inputs)`.
template <typename T> class ConstView
{
  public:
    constexpr ConstView() noexcept = default;
    constexpr ConstView(const T *data, std::size_t size) noexcept : data_(data), size_(size) {}

    constexpr const T *data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr const T *begin() const noexcept { return data_; }
    constexpr const T *end() const noexcept { return data_ + size_; }
    constexpr const T &operator[](std::size_t index) const noexcept { return data_[index]; }
    constexpr operator std::span<const T>() const noexcept { return {data_, size_}; }

  private:
    const T *data_ = nullptr;
    std::size_t size_ = 0;
};

struct StepContext
{
    scene::World &world;
    core::Random &random;
    std::uint64_t step;          // numéro du pas joué (0 pour le premier)
    double dt;                   // durée d'un pas en secondes
    ConstView<Input> inputs;     // entrées de ce pas, dans l'ordre d'ajout (type complet : sim/simulation.hpp)
    core::ArenaAllocator &scratch; // mémoire temporaire du pas (types trivialement destructibles), vidée au pas suivant
    std::vector<Event> &events;  // événements émis par ce pas et les précédents non encore repris (type : sim/simulation.hpp)

    // Ajoute un événement daté de ce pas. La forme avec charge demande `nlohmann/json` complet chez l'appelant
    // (sim/simulation.hpp). Définies dans le moteur (src/sim/simulation.cpp).
    void emit(std::string name);
    void emit(std::string name, core::Json data);
};

using SystemFn = std::function<void(StepContext &)>;

struct SystemDesc
{
    std::string name;
    Phase phase = Phase::kUpdate;
    int order = 0;
    SystemFn run;
};

} // namespace orosmo::sim
