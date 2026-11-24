// ANIM-02 : clips d'animation. Un `Clip` lit une animation glTF (`assets::AnimationData`) : pistes de translation, rotation et
// échelle par nœud, interpolations STEP, LINEAR (slerp pour la rotation) et CUBICSPLINE (Hermite de glTF), et porte des
// événements nommés à des temps donnés. Les pistes `weights` (cibles de forme, ANIM-05) sont ignorées.
#pragma once

#include "orosmo/anim/pose.hpp"
#include "orosmo/assets/model.hpp"
#include "orosmo/core/error.hpp"

#include <string>
#include <vector>

namespace orosmo::anim
{

enum class LoopMode : std::uint8_t
{
    kOnce, // le temps s'arrête à la fin (dernière pose tenue)
    kLoop, // le temps revient à 0
};

struct AnimationEvent
{
    float time = 0.0f;
    std::string name;
};

class Clip
{
  public:
    // kOutOfRange : animation absente ; kParse : piste qui vise un nœud absent ou au nombre de valeurs faux.
    static core::Result<Clip> fromModel(const assets::Model &model, std::size_t animation);
    static core::Result<Clip> fromData(assets::AnimationData data, std::size_t nodeCount);

    const std::string &name() const noexcept { return data_.name; }
    float duration() const noexcept { return data_.duration; }
    std::size_t nodeCount() const noexcept { return nodeCount_; }

    // Événement au temps `time` (borné à [0, duration]) ; gardés triés par temps.
    void addEvent(float time, std::string name);
    const std::vector<AnimationEvent> &events() const noexcept { return events_; }

    // Écrit les nœuds animés de `pose` au temps `time` (borné à [0, duration]) ; les nœuds sans piste ne changent pas.
    // kInvalidArgument si la pose n'a pas `nodeCount()` nœuds.
    core::Result<> sample(float time, Pose &pose) const;

    // Ajoute à `out` les événements franchis en avançant de `from` (exclu) à `to` (inclus). `wrapped` : le temps a fait le tour
    // (boucle) : (from, duration] puis [0, to]. Un `from` négatif inclut les événements à 0 (premier passage).
    void collectEvents(float from, float to, bool wrapped, std::vector<AnimationEvent> &out) const;

  private:
    assets::AnimationData data_;
    std::size_t nodeCount_ = 0;
    std::vector<AnimationEvent> events_;
};

// Temps local d'un clip après avoir avancé : borné (kOnce) ou ramené dans [0, duration) (kLoop). `wrapped` dit si la boucle a
// fait au moins un tour.
struct ClipTime
{
    float time = 0.0f;
    bool wrapped = false;
    bool finished = false; // kOnce : la fin est atteinte
};
ClipTime advanceClipTime(float time, float delta, float duration, LoopMode mode);

} // namespace orosmo::anim
