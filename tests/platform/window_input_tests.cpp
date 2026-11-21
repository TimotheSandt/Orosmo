// PLAT-01 / PLAT-02 : `Window` et `InputManager` cachent GLFW (poignée opaque), exposent DPI, curseur, fermeture et molette.
// Sans affichage (ni xvfb), chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"

#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace platform = orosmo::platform;

namespace
{

// Crée une fenêtre ; renvoie faux (et un message) si le système n'a pas d'affichage.
bool open(platform::Window &window)
{
    if (window.init() != 0)
    {
        if (tests::gpuRequired())
        {
            FAIL("ENGINE_TEST_REQUIRE_GPU is set but the window could not be created");
        }
        MESSAGE("no display: window test skipped");
        return false;
    }
    return true;
}

} // namespace

TEST_SUITE("platform/window")
{
    TEST_CASE("PLAT-01 a window exposes an opaque native handle, its framebuffer size and its DPI scale")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        CHECK(window.getWindow() != nullptr);
        CHECK_FALSE(window.shouldClose());

        int width = 0;
        int height = 0;
        window.getFramebufferSize(width, height);
        CHECK(width > 0);
        CHECK(height > 0);

        const glm::vec2 scale = window.getContentScale();
        CHECK(scale.x >= 1.0f);
        CHECK(scale.y >= 1.0f);
    }

    TEST_CASE("PLAT-01 requestClose makes shouldClose true")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        window.requestClose();
        CHECK(window.shouldClose());
    }

    TEST_CASE("PLAT-01 a window that was never created behaves safely")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        CHECK(window.getWindow() == nullptr);
        CHECK(window.shouldClose());
        window.requestClose();
        window.setCursorMode(platform::CursorMode::kHidden);
        window.setCursorPosition({1.0, 2.0});
        CHECK(window.getCursorPosition() == glm::dvec2(0.0, 0.0));
        int width = -1;
        int height = -1;
        window.getFramebufferSize(width, height);
        CHECK(width > 0);
        CHECK(window.getContentScale() == glm::vec2(1.0f, 1.0f));
    }

    TEST_CASE("PLAT-02 the mouse wheel is accumulated until the next Update, then reset")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        auto *handle = static_cast<GLFWwindow *>(window.getWindow());
        platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());

        // GLFW n'a pas d'API pour simuler la molette : on récupère le rappel installé par InputManager et on l'appelle.
        const GLFWscrollfun callback = glfwSetScrollCallback(handle, nullptr);
        REQUIRE(callback != nullptr);
        callback(handle, 1.0, 2.0);
        callback(handle, 0.5, 1.0);
        glfwSetScrollCallback(handle, callback);

        input.update();
        CHECK(input.getMouseScroll() == glm::vec2(1.5f, 3.0f));
        input.update();
        CHECK(input.getMouseScroll() == glm::vec2(0.0f, 0.0f));
    }
}
