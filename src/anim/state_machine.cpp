#include "orosmo/anim/state_machine.hpp"

#include <algorithm>

namespace orosmo::anim
{

using core::ErrorCode;
using core::makeError;

const AnimState *StateMachine::find(const std::string &name) const
{
    const auto it = std::ranges::find_if(states_, [&](const AnimState &s) { return s.name == name; });
    return it != states_.end() ? &*it : nullptr;
}

core::Result<> StateMachine::addState(AnimState state)
{
    if (find(state.name) != nullptr)
    {
        return makeError(ErrorCode::kAlreadyExists, "StateMachine: state '" + state.name + "' already exists");
    }
    if (state.clip == nullptr || state.clip->nodeCount() != rest_.nodes.size())
    {
        return makeError(ErrorCode::kInvalidArgument, "StateMachine: state '" + state.name + "' has no clip or a clip for another node count");
    }
    states_.push_back(std::move(state));
    return {};
}

core::Result<> StateMachine::addTransition(AnimTransition transition)
{
    if ((!transition.from.empty() && find(transition.from) == nullptr) || find(transition.to) == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "StateMachine: transition '" + transition.from + "' -> '" + transition.to + "' names an unknown state");
    }
    if (transition.condition.empty() && !transition.atEnd)
    {
        return makeError(ErrorCode::kInvalidArgument, "StateMachine: transition to '" + transition.to + "' has neither a condition nor atEnd");
    }
    transition.duration = std::max(transition.duration, 0.0f);
    transitions_.push_back(std::move(transition));
    return {};
}

core::Result<> StateMachine::start(const std::string &state)
{
    const AnimState *s = find(state);
    if (s == nullptr)
    {
        return makeError(ErrorCode::kNotFound, "StateMachine::start: unknown state '" + state + "'");
    }
    current_ = state;
    currentTime_ = 0.0f;
    previous_.reset();
    std::vector<AnimationEvent> first;
    s->clip->collectEvents(-1.0f, 0.0f, 0, first); // événements à t = 0
    for (AnimationEvent &e : first)
    {
        events_.push_back({current_, std::move(e.name)});
    }
    return evaluate();
}

bool StateMachine::getBool(const std::string &name) const
{
    const auto it = bools_.find(name);
    return it != bools_.end() && it->second;
}

float StateMachine::blendWeight() const noexcept
{
    if (!previous_)
    {
        return 1.0f;
    }
    return fadeDuration_ > 0.0f ? std::clamp(fadeElapsed_ / fadeDuration_, 0.0f, 1.0f) : 1.0f;
}

bool StateMachine::conditionMet(const AnimTransition &t) const
{
    if (t.atEnd && !currentEnded_)
    {
        return false;
    }
    return t.condition.empty() || getBool(t.condition) || triggers_.contains(t.condition);
}

core::Result<> StateMachine::update(float dt)
{
    const AnimState *cur = find(current_);
    if (cur == nullptr)
    {
        return makeError(ErrorCode::kInvalidArgument, "StateMachine::update: call start() first");
    }
    dt = std::max(dt, 0.0f);
    // Temps de l'état courant (et de l'état quitté pendant un fondu) ; événements franchis.
    const ClipTime next = advanceClipTime(currentTime_, dt * cur->speed, cur->clip->duration(), cur->loop);
    std::vector<AnimationEvent> fired;
    cur->clip->collectEvents(currentTime_, next.time, next.laps, fired);
    for (AnimationEvent &e : fired)
    {
        events_.push_back({current_, std::move(e.name)});
    }
    currentEnded_ = next.wrapped || next.finished;
    currentTime_ = next.time;
    if (previous_)
    {
        if (const AnimState *prev = find(previous_->state))
        {
            previous_->time = advanceClipTime(previous_->time, dt * prev->speed, prev->clip->duration(), prev->loop).time;
        }
        fadeElapsed_ += dt;
        if (fadeElapsed_ >= fadeDuration_)
        {
            previous_.reset();
        }
    }
    // Transitions (hors fondu), dans l'ordre d'ajout : la première qui part gagne.
    if (!previous_)
    {
        for (const AnimTransition &t : transitions_)
        {
            if ((!t.from.empty() && t.from != current_) || t.to == current_ || !conditionMet(t))
            {
                continue;
            }
            triggers_.erase(t.condition);
            if (t.duration > 0.0f)
            {
                previous_ = Playing{current_, currentTime_};
            }
            fadeElapsed_ = 0.0f;
            fadeDuration_ = t.duration;
            current_ = t.to;
            currentTime_ = 0.0f;
            currentEnded_ = false;
            std::vector<AnimationEvent> first;
            find(current_)->clip->collectEvents(-1.0f, 0.0f, 0, first);
            for (AnimationEvent &e : first)
            {
                events_.push_back({current_, std::move(e.name)});
            }
            break;
        }
    }
    return evaluate();
}

core::Result<> StateMachine::evaluate()
{
    Pose target = rest_;
    if (auto r = find(current_)->clip->sample(currentTime_, target); !r)
    {
        return r;
    }
    if (!previous_)
    {
        pose_ = std::move(target);
        return {};
    }
    Pose source = rest_;
    if (auto r = find(previous_->state)->clip->sample(previous_->time, source); !r)
    {
        return r;
    }
    auto mixed = blendPoses(source, target, blendWeight());
    if (!mixed)
    {
        return std::unexpected(mixed.error());
    }
    pose_ = std::move(*mixed);
    return {};
}

} // namespace orosmo::anim
