// Monde de debug de la démo (UI-04, A79 lot L10) : une valeur modifiée dans l'inspecteur est rendue à la caméra ; une direction
// qui rendrait la vue indéfinie (nulle ou parallèle à `up`) est refusée.
#include <doctest/doctest.h>

#include "Game/debug_world.hpp"

#include <glm/geometric.hpp>

TEST_SUITE("apps/demo-debug-world")
{
    TEST_CASE("an edited direction is applied unless it makes the view undefined")
    {
        demo::DebugWorld debug;
        REQUIRE(debug.init().has_value());
        const auto type = debug.world().componentType<demo::DemoCamera>("DemoCamera");
        REQUIRE(type.has_value());
        orosmo::renderer::Camera camera;
        const glm::vec3 initial = camera.orientation();

        auto edit = [&](const glm::vec3 &direction)
        {
            debug.pull(camera);
            demo::DemoCamera *c = debug.world().get(debug.cameraEntity(), *type);
            REQUIRE(c != nullptr);
            c->direction = direction;
            return debug.push(camera);
        };
        CHECK_FALSE(edit(glm::vec3(0.0f)));
        CHECK_FALSE(edit(glm::vec3(0.0f, 1.0f, 0.0f)));
        CHECK_FALSE(edit(glm::vec3(0.0f, -3.0f, 0.0f)));
        CHECK((camera.orientation() == initial));
        CHECK(edit(glm::vec3(2.0f, 0.0f, 0.0f)));
        CHECK((camera.orientation() == glm::vec3(1.0f, 0.0f, 0.0f)));
        CHECK(edit(glm::vec3(0.0f, 1.0f, 1.0f)));
        CHECK(camera.orientation().y == doctest::Approx(0.7071f).epsilon(0.001));
    }
}
