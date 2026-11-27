// CORE-09 : types mathématiques seuls (vecteurs, matrices, quaternions et position mondiale), sans la bibliothèque de fonctions
// de glm (`glm/glm.hpp`, `gtc/*`). En-tête léger (GAME-06, A79 lot L7) : la réflexion, les composants et `orosmo/game.hpp`
// n'ont besoin que des types et de leurs opérateurs ; les fonctions (`normalize`, `cross`, matrices de projection…) et les
// conversions de `core/math.hpp` demandent `core/math.hpp`.
#pragma once

#include <glm/ext/matrix_double3x3.hpp>
#include <glm/ext/matrix_double4x4.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_double2.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

namespace orosmo::core
{

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

using DVec2 = glm::dvec2;
using DVec3 = glm::dvec3;
using DMat3 = glm::dmat3;
using DMat4 = glm::dmat4;
using DQuat = glm::dquat;

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

} // namespace orosmo::core
