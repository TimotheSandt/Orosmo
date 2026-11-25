// ANIM-02 : machine à états d'animation. Chaque état joue un clip (boucle ou une fois, vitesse) ; les transitions passent d'un
// état à un autre par fondu enchaîné de `duration` secondes, sur condition (paramètre booléen ou déclencheur consommé) et/ou à la
// fin du clip. Les événements des clips franchis pendant `update` sont rendus par `takeEvents`. Déterministe : même suite de
// `update` et de paramètres, même pose.
//
// Une transition commencée va à son terme : pendant un fondu, aucune autre transition n'est évaluée.
#pragma once

#include "orosmo/anim/clip.hpp"
#include "orosmo/anim/pose.hpp"
#include "orosmo/core/error.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace orosmo::anim
{

struct AnimState
{
    std::string name;
    const Clip *clip = nullptr; // non possédé : doit survivre à la machine
    LoopMode loop = LoopMode::kLoop;
    float speed = 1.0f;
};

struct AnimTransition
{
    std::string from; // vide : depuis n'importe quel état (sauf `to`)
    std::string to;
    float duration = 0.2f; // fondu, secondes (0 : coupe nette)
    std::string condition{}; // booléen vrai ou déclencheur posé ; vide : aucune condition
    bool atEnd = false;    // seulement quand le clip de l'état d'origine a fini (kOnce) ou fait un tour (kLoop)
};

struct FiredEvent
{
    std::string state;
    std::string name;
};

class StateMachine
{
  public:
    // `rest` : pose de départ de chaque évaluation (nœuds sans piste).
    explicit StateMachine(Pose rest) : rest_(std::move(rest)), pose_(rest_) {}

    // kAlreadyExists (nom pris), kInvalidArgument (clip absent ou de taille différente de la pose).
    core::Result<> addState(AnimState state);
    // kNotFound (état inconnu), kInvalidArgument (ni condition ni `atEnd` : elle ne partirait jamais).
    core::Result<> addTransition(AnimTransition transition);
    // Entre dans l'état sans fondu, temps 0.
    core::Result<> start(const std::string &state);

    void setBool(const std::string &name, bool value) { bools_[name] = value; }
    bool getBool(const std::string &name) const;
    // Déclencheur : vrai jusqu'à ce qu'une transition le consomme.
    void setTrigger(const std::string &name) { triggers_.insert(name); }

    // Avance de `dt` secondes : temps des états, transitions, pose, événements. kInvalidArgument avant `start`.
    core::Result<> update(float dt);

    const Pose &pose() const noexcept { return pose_; }
    const std::string &current() const noexcept { return current_; }
    float currentTime() const noexcept { return currentTime_; }
    bool inTransition() const noexcept { return previous_.has_value(); }
    // Poids de l'état courant dans le fondu (1 hors transition).
    float blendWeight() const noexcept;
    std::vector<FiredEvent> takeEvents() { return std::exchange(events_, {}); }

  private:
    struct Playing
    {
        std::string state;
        float time = 0.0f;
    };
    const AnimState *find(const std::string &name) const;
    bool conditionMet(const AnimTransition &t) const;
    core::Result<> evaluate();

    Pose rest_;
    Pose pose_;
    std::vector<AnimState> states_;
    std::vector<AnimTransition> transitions_;
    std::map<std::string, bool> bools_;
    std::set<std::string> triggers_;
    std::string current_;
    float currentTime_ = 0.0f;
    bool currentEnded_ = false; // le clip courant a fini ou bouclé pendant ce `update`
    std::optional<Playing> previous_; // état quitté pendant un fondu
    float fadeElapsed_ = 0.0f;
    float fadeDuration_ = 0.0f;
    std::vector<FiredEvent> events_;
};

} // namespace orosmo::anim
