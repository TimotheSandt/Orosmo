// ANIM-03 : animation de propriétés par courbes. Une `Curve<T>` (float, vec2, vec3, vec4) interpole des clés en palier, en linéaire
// ou en Hermite cubique (tangentes données par clé) ; hors des clés, la première ou la dernière valeur est tenue. Une
// `PropertyAnimation` associe des courbes nommées à des fonctions qui posent la valeur (position de caméra, couleur de lumière,
// paramètre de matériau, transformation...) et les applique toutes à un temps donné. Déterministe.
#pragma once

#include "orosmo/core/error.hpp"
#include "orosmo/core/math.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace orosmo::anim
{

enum class CurveInterpolation : std::uint8_t
{
    kStep,
    kLinear,
    kCubic, // Hermite : tangente de sortie de la clé k, tangente d'entrée de la clé k+1 (unités : valeur par seconde)
};

template <typename T> struct CurveKey
{
    float time = 0.0f;
    T value{};
    T inTangent{};
    T outTangent{};
};

template <typename T> class Curve
{
  public:
    explicit Curve(CurveInterpolation interpolation = CurveInterpolation::kLinear) : interpolation_(interpolation) {}

    // Insère en gardant l'ordre des temps ; une clé au même temps remplace l'ancienne.
    void addKey(const CurveKey<T> &key)
    {
        const auto at = std::lower_bound(keys_.begin(), keys_.end(), key.time, [](const CurveKey<T> &k, float t) { return k.time < t; });
        if (at != keys_.end() && at->time == key.time)
        {
            *at = key;
            return;
        }
        keys_.insert(at, key);
    }
    void addKey(float time, const T &value) { addKey(CurveKey<T>{time, value, T{}, T{}}); }

    // Valeur au temps `time` ; T{} sans clé.
    T evaluate(float time) const
    {
        if (keys_.empty())
        {
            return T{};
        }
        if (keys_.size() == 1 || time <= keys_.front().time)
        {
            return keys_.front().value;
        }
        if (time >= keys_.back().time)
        {
            return keys_.back().value;
        }
        const auto upper = std::upper_bound(keys_.begin(), keys_.end(), time, [](float t, const CurveKey<T> &k) { return t < k.time; });
        const CurveKey<T> &k1 = *upper;
        const CurveKey<T> &k0 = *(upper - 1);
        const float dt = k1.time - k0.time;
        const float t = dt > 0.0f ? (time - k0.time) / dt : 0.0f;
        switch (interpolation_)
        {
            case CurveInterpolation::kStep: return k0.value;
            case CurveInterpolation::kLinear: return k0.value + (k1.value - k0.value) * t;
            case CurveInterpolation::kCubic:
            {
                const float t2 = t * t;
                const float t3 = t2 * t;
                return k0.value * (2.0f * t3 - 3.0f * t2 + 1.0f) + k0.outTangent * ((t3 - 2.0f * t2 + t) * dt) +
                       k1.value * (-2.0f * t3 + 3.0f * t2) + k1.inTangent * ((t3 - t2) * dt);
            }
        }
        return k0.value;
    }

    float duration() const noexcept { return keys_.empty() ? 0.0f : keys_.back().time; }
    const std::vector<CurveKey<T>> &keys() const noexcept { return keys_; }
    CurveInterpolation interpolation() const noexcept { return interpolation_; }

  private:
    CurveInterpolation interpolation_;
    std::vector<CurveKey<T>> keys_;
};

class PropertyAnimation
{
  public:
    template <typename T> using Setter = std::function<void(const T &)>;

    // Une piste par nom ; kAlreadyExists si le nom est pris, kInvalidArgument sans fonction.
    template <typename T> core::Result<> bind(std::string name, Curve<T> curve, Setter<T> setter)
    {
        if (!setter)
        {
            return core::makeError(core::ErrorCode::kInvalidArgument, "PropertyAnimation::bind: '" + name + "' has no setter");
        }
        if (has(name))
        {
            return core::makeError(core::ErrorCode::kAlreadyExists, "PropertyAnimation::bind: '" + name + "' is already bound");
        }
        Track track{std::move(name), Typed<T>{std::move(curve), std::move(setter)}};
        (applying_ > 0 ? pendingBind_ : tracks_).push_back(std::move(track));
        return {};
    }
    // Contrat de rappel : un setter peut appeler bind/unbind sur cette animation ; la modification prend effet à la fin de
    // l'`apply` en cours (la passe courante parcourt les pistes telles qu'elles étaient au début). Hors `apply`, effet immédiat.
    void unbind(const std::string &name)
    {
        if (applying_ > 0)
        {
            std::erase_if(pendingBind_, [&](const Track &t) { return t.name == name; }); // liaison demandée pendant cette passe
            pendingUnbind_.push_back(name);
            return;
        }
        std::erase_if(tracks_, [&](const Track &t) { return t.name == name; });
    }

    // Pose la valeur de chaque piste au temps `time`, dans l'ordre d'ajout.
    void apply(float time) const
    {
        struct Pass // RAII : le compteur et les modifications différées survivent à une exception d'un setter
        {
            const PropertyAnimation &self;
            explicit Pass(const PropertyAnimation &a) : self(a) { ++self.applying_; }
            ~Pass() { self.endPass(); }
        } pass(*this);
        const std::size_t count = tracks_.size(); // tracks_ ne change pas pendant la passe (bind/unbind différés)
        for (std::size_t i = 0; i < count; ++i)
        {
            std::visit([time](const auto &typed) { typed.setter(typed.curve.evaluate(time)); }, tracks_[i].typed);
        }
    }
    // Plus grande durée des pistes.
    float duration() const
    {
        float d = 0.0f;
        for (const Track &t : tracks_)
        {
            d = std::max(d, std::visit([](const auto &typed) { return typed.curve.duration(); }, t.typed));
        }
        return d;
    }
    std::size_t trackCount() const noexcept { return tracks_.size(); }

  private:
    template <typename T> struct Typed
    {
        Curve<T> curve;
        Setter<T> setter;
    };
    struct Track
    {
        std::string name;
        std::variant<Typed<float>, Typed<core::Vec2>, Typed<core::Vec3>, Typed<core::Vec4>> typed;
    };
    void endPass() const noexcept
    {
        if (--applying_ == 0)
        {
            for (const std::string &n : pendingUnbind_)
            {
                std::erase_if(tracks_, [&](const Track &t) { return t.name == n; });
            }
            pendingUnbind_.clear();
            for (Track &t : pendingBind_)
            {
                tracks_.push_back(std::move(t));
            }
            pendingBind_.clear();
        }
    }
    bool has(const std::string &name) const
    {
        const auto same = [&](const Track &t) { return t.name == name; };
        return std::ranges::any_of(tracks_, same) || std::ranges::any_of(pendingBind_, same);
    }
    // mutable : apply() est const mais applique à sa fin les bind/unbind demandés par les setters.
    mutable std::vector<Track> tracks_;
    mutable std::vector<Track> pendingBind_;
    mutable std::vector<std::string> pendingUnbind_;
    mutable int applying_ = 0;
};

} // namespace orosmo::anim
