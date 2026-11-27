// A75.10 et A75.12 (M8, lot L12b) : fabriques de lumières à une seule convention de direction (sens de propagation), portée au
// lieu des coefficients, angles en degrés ; `Camera::setForward` et `lookAt`. Sans GPU (l'image est comparée par
// tests/renderer/forward_plus_tests.cpp).
#include <doctest/doctest.h>

#include <orosmo/renderer/camera.hpp>
#include <orosmo/renderer/forward_plus.hpp>
#include <orosmo/renderer/light.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>

namespace rd = orosmo::renderer;

namespace
{
bool near(const glm::vec3 &a, const glm::vec3 &b, float epsilon = 1e-5f) { return glm::length(a - b) <= epsilon; }
} // namespace

TEST_SUITE("renderer/light")
{
    TEST_CASE("A75.10 directionalLight(desc): direction is where the light goes; same Light as the old factory with the opposite vector")
    {
        const rd::Light sun = rd::directionalLight({.direction = {-2.0f, -3.0f, 0.5f}, .color = {1.0f, 0.5f, 0.25f}, .intensity = 2.0f});
        CHECK((sun.type == rd::LightType::kDirectional));
        // L'ancienne fabrique (et le tampon GPU) prend la direction vers la lumière : même lumière, donc même image.
        CHECK(sun == rd::directionalLight({2.0f, 3.0f, -0.5f}, {1.0f, 0.5f, 0.25f}, 2.0f));
        CHECK(rd::lightDirection(sun) == glm::vec3(-2.0f, -3.0f, 0.5f));
        // Défauts : lumière blanche d'intensité 1, venant du zénith.
        const rd::Light noon = rd::directionalLight(rd::DirectionalLightDesc{});
        CHECK(noon == rd::directionalLight({0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1.0f));
    }

    TEST_CASE("A75.10 spotLight(desc): the same direction convention as the sun, angles in degrees, range instead of coefficients")
    {
        const rd::Light spot = rd::spotLight({.position = {0, 8, 0},
                                              .direction = {0, -1, 0},
                                              .color = {1, 1, 1},
                                              .intensity = 1.0f,
                                              .range = 20.0f,
                                              .outerAngleDegrees = 30.0f,
                                              .innerAngleDegrees = 25.0f});
        CHECK((spot.type == rd::LightType::kSpot));
        CHECK(spot.direction == glm::vec3(0, -1, 0)); // le tampon GPU garde le sens du faisceau pour un projecteur
        CHECK(rd::lightDirection(spot) == glm::vec3(0, -1, 0));
        CHECK(spot.outerCone == doctest::Approx(std::cos(glm::radians(30.0f))));
        CHECK(spot.innerCone == doctest::Approx(std::cos(glm::radians(25.0f))));
        CHECK(rd::lightRange(spot) == doctest::Approx(20.0f).epsilon(1e-5));
        // Même lumière que l'ancienne fabrique avec les coefficients équivalents : même image.
        CHECK(spot == rd::spotLight({0, 8, 0}, {0, -1, 0}, {1, 1, 1}, 1.0f, spot.outerCone, spot.innerCone, spot.a, spot.b));

        // Cône intérieur plus large que l'extérieur : ramené à l'extérieur (bord net), jamais inversé.
        const rd::Light sharp = rd::spotLight({.outerAngleDegrees = 20.0f, .innerAngleDegrees = 40.0f});
        CHECK(sharp.innerCone == sharp.outerCone);
        CHECK(sharp.outerCone == doctest::Approx(std::cos(glm::radians(20.0f))));
    }

    TEST_CASE("A75.10 pointLight(desc): the range is where the light stops (lightRange), whatever its color and intensity")
    {
        for (const float range : {0.5f, 3.0f, 60.0f, 1000.0f})
        {
            for (const float intensity : {0.1f, 1.0f, 25.0f})
            {
                const rd::Light light = rd::pointLight({.position = {1, 2, 3}, .color = {1.0f, 0.5f, 0.2f}, .intensity = intensity, .range = range});
                CAPTURE(range);
                CAPTURE(intensity);
                CHECK((light.type == rd::LightType::kPoint));
                CHECK(light.position == glm::vec3(1, 2, 3));
                CHECK(light.strength == intensity);
                CHECK(light.b == 0.0f); // atténuation en carré inverse, fenêtrée jusqu'à la portée
                CHECK(rd::lightRange(light) == doctest::Approx(range).epsilon(1e-5));
                CHECK(rd::lightAttenuation(range * 1.001f, light.a, light.b, rd::lightRange(light)) == 0.0f);
                CHECK(rd::lightAttenuation(range * 0.5f, light.a, light.b, rd::lightRange(light)) > 0.0f);
            }
        }
        // Portée infinie : sans atténuation (comme a = b = 0 de l'ancienne fabrique).
        const rd::Light sky = rd::pointLight({.range = std::numeric_limits<float>::infinity()});
        CHECK(sky == rd::pointLight({0, 0, 0}, {1, 1, 1}, 1.0f, 0.0f, 0.0f));
        CHECK(rd::lightRange(sky) == rd::kLightMaxRange);
        // Portée finie au-delà de kLightMaxRange : bornée, mais l'atténuation reste (revue de la PR #24).
        const rd::Light far = rd::pointLight({.range = 2.0e6f});
        CHECK(far.a > 0.0f);
        CHECK(rd::lightRange(far) == doctest::Approx(rd::kLightMaxRange).epsilon(1e-4));
        CHECK(rd::lightAttenuation(5.0e5f, far.a, far.b, rd::lightRange(far)) < 0.05f);
        // Portée nulle ou négative : bornée à 1 mm (la lumière n'éclaire presque rien, sans division par zéro).
        const rd::Light tiny = rd::pointLight({.range = -4.0f});
        CHECK(std::isfinite(tiny.a));
        CHECK(rd::lightRange(tiny) == doctest::Approx(1e-3f).epsilon(1e-4));
        // Ponctuelle : aucune direction.
        CHECK(rd::lightDirection(tiny) == glm::vec3(0.0f));
    }
}

TEST_SUITE("renderer/camera")
{
    TEST_CASE("A75.12 setForward takes a direction (normalized); a null vector keeps the current one")
    {
        rd::Camera camera;
        camera.setForward({0.0f, 0.0f, 5.0f});
        CHECK(camera.forward() == glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK(camera.orientation() == camera.forward());
        camera.setForward(glm::vec3(0.0f));
        CHECK(camera.forward() == glm::vec3(0.0f, 0.0f, 1.0f));
        // Même vue que setOrientation avec la direction normalisée (l'ancien chemin reste).
        rd::Camera old;
        old.setOrientation(glm::normalize(glm::vec3(1.0f, -2.0f, 3.0f)));
        rd::Camera fresh;
        fresh.setForward({1.0f, -2.0f, 3.0f});
        CHECK(near(fresh.forward(), old.orientation()));
        CHECK(glm::length(fresh.forward()) == doctest::Approx(1.0f));
    }

    TEST_CASE("A75.12 lookAt(target) turns the camera towards a point: it projects to the center of the screen")
    {
        rd::Camera camera;
        camera.setPosition({0.0f, 7.0f, 9.0f});
        camera.lookAt({1.0f, 0.0f, -2.0f});
        CHECK(near(camera.forward(), glm::normalize(glm::vec3(1.0f, -7.0f, -11.0f))));
        const glm::vec4 clip = camera.matrix() * glm::vec4(1.0f, 0.0f, -2.0f, 1.0f);
        CHECK(clip.w > 0.0f); // devant la caméra
        CHECK(clip.x / clip.w == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(clip.y / clip.w == doctest::Approx(0.0f).epsilon(1e-5));
        // Même vue que glm::lookAt.
        const glm::mat4 expected = glm::lookAt(glm::vec3(0.0f, 7.0f, 9.0f), glm::vec3(1.0f, 0.0f, -2.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                CHECK(camera.viewMatrix()[c][r] == doctest::Approx(expected[c][r]).epsilon(1e-5));
            }
        }
        // Viser sa propre position : rien ne change.
        const glm::vec3 before = camera.forward();
        camera.lookAt(camera.position());
        CHECK(camera.forward() == before);
    }
}
