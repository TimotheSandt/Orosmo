// CORE-09 : mathématiques. Vecteurs, matrices et quaternions en float (GPU, rendu local) ;
// positions MONDIALES en double (WorldPosition) ; passage de l'un à l'autre uniquement par les fonctions nommées ci-dessous.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace engine::core
{

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

using DVec2 = glm::dvec2;
using DVec3 = glm::dvec3;
using DMat4 = glm::dmat4;
using DQuat = glm::dquat;

inline constexpr double kPi = 3.14159265358979323846;

// Position dans le monde, en double (précision au millimètre à des milliers de km). Type fort : pas de conversion
// implicite vers Vec3, ce qui empêche d'envoyer par erreur une position mondiale au GPU.
struct WorldPosition
{
    DVec3 value{0.0};

    constexpr WorldPosition() = default;
    constexpr explicit WorldPosition(const DVec3 &v) : value(v) {}
    constexpr WorldPosition(double x, double y, double z) : value(x, y, z) {}

    friend constexpr WorldPosition operator+(const WorldPosition &p, const DVec3 &offset) { return WorldPosition(p.value + offset); }
    friend constexpr WorldPosition operator-(const WorldPosition &p, const DVec3 &offset) { return WorldPosition(p.value - offset); }
    friend constexpr DVec3 operator-(const WorldPosition &a, const WorldPosition &b) { return a.value - b.value; }
    friend constexpr bool operator==(const WorldPosition &a, const WorldPosition &b) { return a.value == b.value; }
};

// Origine flottante : position relative à `origin` (typiquement la caméra), arrondie en float. Soustraction faite
// en double AVANT l'arrondi : l'erreur ne dépend plus de la distance à l'origine du monde.
inline Vec3 toLocal(const WorldPosition &position, const WorldPosition &origin) { return Vec3(position - origin); }
inline WorldPosition toWorld(const WorldPosition &origin, const Vec3 &local) { return origin + DVec3(local); }

// Conversions explicites float/double pour les autres cas (directions, matrices).
inline Vec3 narrow(const DVec3 &v) { return Vec3(v); }
inline DVec3 widen(const Vec3 &v) { return DVec3(v); }
inline Mat4 narrow(const DMat4 &m) { return Mat4(m); }
inline DMat4 widen(const Mat4 &m) { return DMat4(m); }

template <typename T> constexpr T saturate(T v) { return v < T(0) ? T(0) : (v > T(1) ? T(1) : T(v)); }
template <typename T> constexpr T lerp(T a, T b, T t) { return a + (b - a) * t; }
inline bool approxEqual(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps * std::fmax(1.0, std::fmax(std::fabs(a), std::fabs(b))); }
constexpr double radians(double degrees) { return degrees * (kPi / 180.0); }
constexpr double degrees(double radiansValue) { return radiansValue * (180.0 / kPi); }

} // namespace engine::core
