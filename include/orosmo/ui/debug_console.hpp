// UI-04 : console de développement (outils de debug, A79 lot L10). Données et commandes seulement : la vue est construite par
// `debug::DebugTools` (orosmo::ui, sans Dear ImGui, A79 choix 6).
//
//   - Commandes enregistrées PAR NOM (`add`) ; une ligne « nom arg1 "arg 2" » les appelle (`execute`). Une commande rend un texte
//     (affiché) ou une erreur (`core::Result`), jamais d'exception ; une commande inconnue rend `kNotFound`.
//   - Journal : `pollLog` recopie les nouveaux messages du `core::Logger` (avertissements et erreurs marqués).
//   - Lignes gardées : les kMaxLines dernières ; `revision` change à chaque modification (la vue ne se refait qu'alors).
//
//   console.add("hud", "hud <off|fps|full|graph> : niveau du panneau", [&](std::span<const std::string> args) -> core::Result<std::string> {...});
//   console.execute("hud full");
#pragma once

#include "orosmo/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orosmo::ui::debug
{

enum class ConsoleLineKind : std::uint8_t
{
    kInput,   // ligne tapée (« > ... »)
    kOutput,  // résultat d'une commande, message du journal
    kWarning, // avertissement du journal
    kError,   // erreur d'une commande, erreur du journal
};

struct ConsoleLine
{
    std::string text;
    ConsoleLineKind kind = ConsoleLineKind::kOutput;
};

using ConsoleCommand = std::function<core::Result<std::string>(std::span<const std::string> args)>;

// Découpe une ligne de commande : mots séparés par des espaces, "entre guillemets" pour un argument avec espaces (\" et \\ y
// sont des échappements). Guillemet non fermé : kInvalidArgument.
core::Result<std::vector<std::string>> splitCommandLine(std::string_view line);

class Console
{
  public:
    static constexpr std::size_t kMaxLines = 500;

    // Enregistre « help » (liste des commandes) et « clear » (efface les lignes).
    Console();
    // Ni copie ni déplacement : « help » et « clear » gardent un pointeur vers leur console.
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;
    Console(Console &&) = delete;
    Console &operator=(Console &&) = delete;

    // Nom : non vide, sans espace ni guillemet (kInvalidArgument) ; déjà pris : kAlreadyExists.
    core::Result<> add(std::string name, std::string help, ConsoleCommand command);
    bool remove(std::string_view name);
    bool contains(std::string_view name) const;
    std::vector<std::string> names() const; // triés

    // Exécute une ligne : elle est recopiée (« > ligne »), puis le texte rendu (s'il n'est pas vide) ou l'erreur. Ligne vide : rien.
    core::Result<std::string> execute(std::string_view line);

    void print(std::string text, ConsoleLineKind kind = ConsoleLineKind::kOutput);
    void clear();
    const std::deque<ConsoleLine> &lines() const noexcept { return lines_; }
    std::uint64_t revision() const noexcept { return revision_; }

    // Recopie les messages du journal arrivés depuis l'appel précédent (au premier appel : ceux arrivés depuis la construction).
    // Vrai si une ligne a été ajoutée.
    bool pollLog();

  private:
    struct Entry
    {
        std::string help;
        // Partagée avec l'appel en cours : la commande garde son état d'un appel à l'autre et peut se retirer pendant l'appel.
        std::shared_ptr<ConsoleCommand> command;
    };
    std::map<std::string, Entry, std::less<>> commands_;
    std::deque<ConsoleLine> lines_;
    std::uint64_t revision_ = 0;
    std::uint64_t logSequence_ = 0;
};

} // namespace orosmo::ui::debug
