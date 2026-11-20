// PLAT-01 / PLAT-02 : `Window` et `InputManager` cachent GLFW (poignée opaque), exposent DPI, curseur, fermeture et molette.
// Sans affichage (ni xvfb), chaque test s'arrête avec un avertissement.
#include <doctest/doctest.h>

#include "support/test_support.hpp"

#include <engine/platform/input_manager.hpp>
#include <engine/platform/window.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{

// Crée une fenêtre ; renvoie faux (et un message) si le système n'a pas d'affichage.
bool open(Window &window)
{
    if (window.Init() != 0)
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
        Window window("platform-test", 96, 64, WindowState::WINDOWED);
        if (!open(window))
        {
            return;
        }
        CHECK(window.GetWindow() != nullptr);
        CHECK_FALSE(window.ShouldClose());

        int width = 0;
        int height = 0;
        window.GetFramebufferSize(width, height);
        CHECK(width > 0);
        CHECK(height > 0);

        const glm::vec2 scale = window.GetContentScale();
        CHECK(scale.x >= 1.0f);
        CHECK(scale.y >= 1.0f);
    }

    TEST_CASE("PLAT-01 RequestClose makes ShouldClose true")
    {
        Window window("platform-test", 96, 64, WindowState::WINDOWED);
        if (!open(window))
        {
            return;
        }
        window.RequestClose();
        CHECK(window.ShouldClose());
    }

    TEST_CASE("PLAT-01 a window that was never created behaves safely")
    {
        Window window("platform-test", 96, 64, WindowState::WINDOWED);
        CHECK(window.GetWindow() == nullptr);
        CHECK(window.ShouldClose());
        window.RequestClose();
        window.SetCursorMode(CursorMode::Hidden);
        window.SetCursorPosition({1.0, 2.0});
        CHECK(window.GetCursorPosition() == glm::dvec2(0.0, 0.0));
        int width = -1;
        int height = -1;
        window.GetFramebufferSize(width, height);
        CHECK(width > 0);
        CHECK(window.GetContentScale() == glm::vec2(1.0f, 1.0f));
    }

    TEST_CASE("PLAT-02 the mouse wheel is accumulated until the next Update, then reset")
    {
        Window window("platform-test", 96, 64, WindowState::WINDOWED);
        if (!open(window))
        {
            return;
        }
        auto *handle = static_cast<GLFWwindow *>(window.GetWindow());
        InputManager &input = InputManager::GetInstance(window.GetWindow());

        // GLFW n'a pas d'API pour simuler la molette : on récupère le rappel installé par InputManager et on l'appelle.
        const GLFWscrollfun callback = glfwSetScrollCallback(handle, nullptr);
        REQUIRE(callback != nullptr);
        callback(handle, 1.0, 2.0);
        callback(handle, 0.5, 1.0);
        glfwSetScrollCallback(handle, callback);

        input.Update();
        CHECK(input.GetMouseScroll() == glm::vec2(1.5f, 3.0f));
        input.Update();
        CHECK(input.GetMouseScroll() == glm::vec2(0.0f, 0.0f));
    }
}
