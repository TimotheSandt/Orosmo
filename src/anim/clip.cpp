#include "orosmo/anim/clip.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace orosmo::anim
{

using core::ErrorCode;
using core::makeError;

namespace
{

core::Vec3 vec3At(const std::vector<float> &v, std::size_t i) { return {v[i], v[i + 1], v[i + 2]}; }
core::Quat quatAt(const std::vector<float> &v, std::size_t i) { return {v[i + 3], v[i], v[i + 1], v[i + 2]}; } // glTF : x, y, z, w

template <typename T> T hermite(const T &v0, const T &out0, const T &in1, const T &v1, float t, float dt)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + (t3 - 2.0f * t2 + t) * dt * out0 + (-2.0f * t3 + 3.0f * t2) * v1 + (t3 - t2) * dt * in1;
}

// Valeur d'une piste (3 ou 4 composantes) au temps `time`.
template <typename T, typename Read, typename Lerp>
T evaluate(const assets::AnimationChannel &ch, float time, Read read, Lerp lerp)
{
    const std::size_t keys = ch.times.size();
    const std::size_t n = ch.componentCount;
    const bool cubic = ch.interpolation == assets::Interpolation::kCubicSpline;
    // Valeur de la clé k (en cubique : groupe du milieu, après la tangente d'entrée).
    auto value = [&](std::size_t k) { return read(ch.values, cubic ? (k * 3 + 1) * n : k * n); };
    if (keys == 1 || time <= ch.times.front())
    {
        return value(0);
    }
    if (time >= ch.times.back())
    {
        return value(keys - 1);
    }
    const auto upper = std::upper_bound(ch.times.begin(), ch.times.end(), time);
    const std::size_t k1 = static_cast<std::size_t>(upper - ch.times.begin());
    const std::size_t k0 = k1 - 1;
    const float dt = ch.times[k1] - ch.times[k0];
    const float t = dt > 0.0f ? (time - ch.times[k0]) / dt : 0.0f;
    switch (ch.interpolation)
    {
        case assets::Interpolation::kStep: return value(k0);
        case assets::Interpolation::kLinear: return lerp(value(k0), value(k1), t);
        case assets::Interpolation::kCubicSpline:
        {
            const T out0 = read(ch.values, (k0 * 3 + 2) * n);
            const T in1 = read(ch.values, (k1 * 3) * n);
            return hermite(value(k0), out0, in1, value(k1), t, dt);
        }
    }
    return value(k0);
}

} // namespace

core::Result<Clip> Clip::fromModel(const assets::Model &model, std::size_t animation)
{
    if (animation >= model.animations.size())
    {
        return makeError(ErrorCode::kOutOfRange, "Clip::fromModel: animation " + std::to_string(animation) + " does not exist");
    }
    return fromData(model.animations[animation], model.nodes.size());
}

core::Result<Clip> Clip::fromData(assets::AnimationData data, std::size_t nodeCount)
{
    for (const assets::AnimationChannel &ch : data.channels)
    {
        if (ch.path == assets::AnimationPath::kWeights)
        {
            continue;
        }
        const std::size_t expected = ch.path == assets::AnimationPath::kRotation ? 4 : 3;
        const std::size_t groups = ch.interpolation == assets::Interpolation::kCubicSpline ? 3 : 1;
        if (ch.node < 0 || static_cast<std::size_t>(ch.node) >= nodeCount || ch.times.empty() || ch.componentCount != expected ||
            ch.values.size() != ch.times.size() * groups * expected || !std::is_sorted(ch.times.begin(), ch.times.end()))
        {
            return makeError(ErrorCode::kParse, "Clip: animation '" + data.name + "' has a channel with a bad node, key count or component count");
        }
        data.duration = std::max(data.duration, ch.times.back());
    }
    Clip clip;
    clip.data_ = std::move(data);
    clip.nodeCount_ = nodeCount;
    return clip;
}

void Clip::addEvent(float time, std::string name)
{
    AnimationEvent e{std::clamp(time, 0.0f, data_.duration), std::move(name)};
    const auto at = std::upper_bound(events_.begin(), events_.end(), e.time, [](float t, const AnimationEvent &x) { return t < x.time; });
    events_.insert(at, std::move(e));
}

core::Result<> Clip::sample(float time, Pose &pose) const
{
    if (pose.nodes.size() != nodeCount_)
    {
        return makeError(ErrorCode::kInvalidArgument, "Clip::sample: the pose has " + std::to_string(pose.nodes.size()) + " nodes, the clip " +
                                                          std::to_string(nodeCount_));
    }
    time = std::clamp(time, 0.0f, data_.duration);
    const auto mix3 = [](const core::Vec3 &a, const core::Vec3 &b, float t) { return glm::mix(a, b, t); };
    for (const assets::AnimationChannel &ch : data_.channels)
    {
        Transform &node = pose.nodes[static_cast<std::size_t>(ch.node)];
        switch (ch.path)
        {
            case assets::AnimationPath::kTranslation: node.translation = evaluate<core::Vec3>(ch, time, vec3At, mix3); break;
            case assets::AnimationPath::kScale: node.scale = evaluate<core::Vec3>(ch, time, vec3At, mix3); break;
            case assets::AnimationPath::kRotation:
                node.rotation = glm::normalize(evaluate<core::Quat>(ch, time, quatAt, [](const core::Quat &a, const core::Quat &b, float t) {
                    return glm::slerp(a, b, t);
                }));
                break;
            case assets::AnimationPath::kWeights: break; // ANIM-05
        }
    }
    return {};
}

void Clip::collectEvents(float from, float to, std::size_t laps, std::vector<AnimationEvent> &out) const
{
    for (const AnimationEvent &e : events_)
    {
        std::size_t count = 0;
        if (laps == 0)
        {
            count = (e.time > from && e.time <= to) ? 1 : 0;
        }
        else
        {
            count = (e.time > from ? 1 : 0) + (laps - 1) + (e.time <= to ? 1 : 0);
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            out.push_back(e);
        }
    }
}

ClipTime advanceClipTime(float time, float delta, float duration, LoopMode mode)
{
    ClipTime out;
    float t = time + delta;
    if (duration <= 0.0f)
    {
        out.finished = mode == LoopMode::kOnce;
        return out;
    }
    if (mode == LoopMode::kOnce)
    {
        out.time = std::clamp(t, 0.0f, duration);
        out.finished = t >= duration;
        return out;
    }
    if (t >= duration || t < 0.0f)
    {
        out.wrapped = true;
        out.laps = static_cast<std::size_t>(t < 0.0f ? std::ceil(-t / duration) : std::floor(t / duration));
        out.laps = std::max<std::size_t>(out.laps, 1);
        t = std::fmod(t, duration);
        if (t < 0.0f)
        {
            t += duration;
        }
    }
    out.time = t;
    return out;
}

} // namespace orosmo::anim
