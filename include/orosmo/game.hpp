// En-tête unique et léger de la logique de jeu (GAME-06, A79 lot L7) : ce qu'il faut pour écrire la bibliothèque de jeu
// (src/game/ d'un projet), compilée en quelques secondes à chaque modification.
//
//   - interface C avec le moteur (`OrosmoGameApi`, `OROSMO_GAME_EXPORT`, `app::kGameAbiVersion`) ;
//   - contexte (`app::GameContext` : composants, systèmes et actions enregistrés par nom) ;
//   - monde et composants (`scene::World`, `scene::ComponentType`, `Transform`), réflexion (`core::Reflect`) ;
//   - systèmes (`sim::SystemDesc`, `sim::StepContext`, `sim::Phase`), générateur `core::Random`, `ctx.emit(nom)` (un
//     événement avec charge JSON demande `sim/simulation.hpp`) ;
//   - types mathématiques (`core::Vec3`, `core::Quat`…) sans la bibliothèque de fonctions de glm.
//
// Il N'inclut PAS : le rendu (`renderer`, `rhi`), la plateforme, l'interface (`ui`), la boucle de simulation
// (`sim::Simulation`) ni la sérialisation (`nlohmann/json`), ni `glm/glm.hpp`. Un jeu qui en a besoin les inclut lui-même,
// au prix d'une compilation plus longue. `tools/check_headers.py` vérifie cette liste.
#pragma once

#include <orosmo/app/game_api.hpp>
#include <orosmo/app/game_context.hpp>
#include <orosmo/core/error.hpp>
#include <orosmo/core/math_types.hpp>
#include <orosmo/core/random.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/scene/component.hpp>
#include <orosmo/scene/entity.hpp>
#include <orosmo/scene/transform.hpp>
#include <orosmo/scene/world.hpp>
#include <orosmo/sim/system.hpp>
