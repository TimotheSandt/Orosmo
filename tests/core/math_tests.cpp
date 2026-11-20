#include <doctest/doctest.h>

#include <orosmo/core/math.hpp>

#include <glm/gtc/matrix_transform.hpp>

using namespace orosmo::core;

TEST_SUITE("core/math")
{
    TEST_CASE("a float cannot hold a world position far from the origin, WorldPosition can")
    {
        const double far = 10'000'000.0; // 10 000 km
        const WorldPosition p(far + 0.125, 0.0, 0.0);
        const float asFloat = static_cast<float>(p.value.x);
        CHECK(static_cast<double>(asFloat) != doctest::Approx(far + 0.125).epsilon(1e-12)); // float: erreur ~1 m
        CHECK(p.value.x == far + 0.125);                                                     // double: exact
    }

    TEST_CASE("toLocal subtracts in double before rounding: millimetre precision 10 000 km from the origin")
    {
        const WorldPosition camera(10'000'000.0, 5.0, -3.0);
        const WorldPosition object = camera + DVec3(0.002, 1.5, 0.0); // 2 mm
        const Vec3 local = toLocal(object, camera);
        CHECK(local.x == doctest::Approx(0.002f).epsilon(1e-5));
        CHECK(local.y == doctest::Approx(1.5f));
        CHECK(toWorld(camera, local).value.x == doctest::Approx(object.value.x).epsilon(1e-12));
    }

    TEST_CASE("position difference is a vector, not a position")
    {
        const WorldPosition a(1, 2, 3), b(4, 6, 8);
        const DVec3 d = b - a;
        CHECK(d == DVec3(3, 4, 5));
        CHECK(glm::length(d) == doctest::Approx(std::sqrt(50.0)));
        CHECK(a + d == b);
    }

    TEST_CASE("explicit float/double conversions round-trip small values")
    {
        const Vec3 v(1.5f, -2.25f, 8.f);
        CHECK(narrow(widen(v)) == v);
        const Mat4 m = glm::translate(Mat4(1.f), v);
        CHECK(narrow(widen(m)) == m);
    }

    TEST_CASE("helpers")
    {
        CHECK(saturate(-1.0) == 0.0);
        CHECK(saturate(2.0f) == 1.0f);
        CHECK(lerp(10.0, 20.0, 0.25) == doctest::Approx(12.5));
        CHECK(radians(180.0) == doctest::Approx(kPi));
        CHECK(degrees(kPi) == doctest::Approx(180.0));
        CHECK(approxEqual(1e9, 1e9 + 1.0));
        CHECK_FALSE(approxEqual(1.0, 1.1));
        const Quat q = glm::angleAxis(static_cast<float>(kPi / 2), Vec3(0, 0, 1));
        const Vec3 r = q * Vec3(1, 0, 0);
        CHECK(r.y == doctest::Approx(1.f).epsilon(1e-5));
    }
}
