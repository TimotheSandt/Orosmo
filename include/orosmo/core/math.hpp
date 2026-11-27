// CORE-09 : mathématiques. Vecteurs, matrices et quaternions en float (GPU, rendu local) ;
// positions MONDIALES en double (WorldPosition) ; passage de l'un à l'autre uniquement par les fonctions nommées ci-dessous.
// Les types seuls (et `WorldPosition`) sont dans `math_types.hpp` (en-tête léger) ; ici, les fonctions de glm et les conversions.
#pragma once

#include <orosmo/core/math_types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace orosmo::core
{

inline constexpr double kPi = 3.14159265358979323846;

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

} // namespace orosmo::core
