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
    if (auto opened = window.init(); !opened)
    {
        CHECK((opened.error().code == orosmo::core::ErrorCode::kUnsupported)); // sans affichage : refus propre (A75.8)
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
    TEST_CASE("G201 WindowParameters has a defined value for every member, the same as Window::Window()")
    {
        const platform::WindowParameters p{};
        CHECK(p.title == "Window");
        CHECK(p.width == 800);
        CHECK(p.height == 600);
        CHECK(p.windowedWidth == p.width);
        CHECK(p.windowedPosX == p.posX);
        CHECK_FALSE(p.vsync);
        CHECK(p.taskbarVisible);
        CHECK(p.trueEveryms == doctest::Approx(500.0));
        const platform::WindowParameters copy = p; // la copie lisait un booléen non initialisé (UBSan)
        CHECK(copy.taskbarVisible == p.taskbarVisible);
    }

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
        // A75.16 : ni lecture d'événements ni image sans fenêtre (newFrame déréférençait un InputManager nul).
        CHECK_FALSE(window.pollEvents());
        CHECK_FALSE(window.newFrame());
        CHECK(window.getFrame() == 0);
    }

    TEST_CASE("A75.8 Window::init returns a Result: a size of zero is refused without opening anything")
    {
        platform::Window window("platform-test", 0, 64, platform::WindowState::kWindowed);
        const orosmo::core::Result<> opened = window.init();
        REQUIRE_FALSE(opened.has_value());
        CHECK((opened.error().code == orosmo::core::ErrorCode::kInvalidArgument));
        CHECK(window.getWindow() == nullptr);
        platform::Window negative("platform-test", 96, -1);
        CHECK_FALSE(negative.init().has_value());
    }

    TEST_CASE("A75.8 a second init of an open window is refused and keeps the window")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        void *const handle = window.getWindow();
        const orosmo::core::Result<> again = window.init();
        REQUIRE_FALSE(again.has_value());
        CHECK((again.error().code == orosmo::core::ErrorCode::kAlreadyExists));
        CHECK(window.getWindow() == handle);
    }

    TEST_CASE("A75.8 (choice 9) Window::input() is the InputManager of getInstance(nativeWindow)")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        CHECK(&window.input() == &platform::InputManager::getInstance(window.getWindow()));
    }

    TEST_CASE("A75.16 pollEvents reads the events and updates the inputs, without counting a frame; newFrame does both")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        auto *handle = static_cast<GLFWwindow *>(window.getWindow());
        const GLFWscrollfun callback = glfwSetScrollCallback(handle, nullptr);
        REQUIRE(callback != nullptr);
        callback(handle, 0.0, 2.0);
        glfwSetScrollCallback(handle, callback);

        const int frame = window.getFrame();
        CHECK(window.pollEvents());
        CHECK(window.input().getMouseScroll() == glm::vec2(0.0f, 2.0f)); // InputManager::update a eu lieu
        CHECK(window.getFrame() == frame);                                // aucune image comptée
        CHECK(window.newFrame());
        CHECK(window.input().getMouseScroll() == glm::vec2(0.0f, 0.0f));
        CHECK(window.getFrame() == frame + 1);
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

    TEST_CASE("PLAT-01 the framebuffer size and the cursor are kept by GLFW callbacks, read without a display round trip")
    {
        platform::Window window("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(window))
        {
            return;
        }
        auto *handle = static_cast<GLFWwindow *>(window.getWindow());
        int glfwWidth = 0;
        int glfwHeight = 0;
        glfwGetFramebufferSize(handle, &glfwWidth, &glfwHeight);
        int width = 0;
        int height = 0;
        platform::framebufferSizeOf(window.getWindow(), width, height);
        CHECK(width == glfwWidth);
        CHECK(height == glfwHeight);

        // GLFW n'a pas d'API pour simuler un redimensionnement ni un mouvement : les rappels installés par Window sont appelés.
        const GLFWframebuffersizefun resize = glfwSetFramebufferSizeCallback(handle, nullptr);
        REQUIRE(resize != nullptr);
        resize(handle, 123, 45);
        glfwSetFramebufferSizeCallback(handle, resize);
        window.getFramebufferSize(width, height);
        CHECK(width == 123);
        CHECK(height == 45);

        const GLFWcursorposfun moved = glfwSetCursorPosCallback(handle, nullptr);
        REQUIRE(moved != nullptr);
        moved(handle, 12.5, 7.0);
        glfwSetCursorPosCallback(handle, moved);
        CHECK(window.getCursorPosition() == glm::dvec2(12.5, 7.0));
        CHECK(platform::cursorPositionOf(window.getWindow()) == glm::dvec2(12.5, 7.0));
        platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());
        input.update();
        CHECK(input.getMousePosition() == glm::vec2(12.5f, 7.0f));

        // Un déplacement demandé par le programme ne déclenche pas de rappel : la valeur gardée suit quand même, et c'est la position
        // réellement appliquée (GLFW ignore la demande sans le focus, comme ici avec une fenêtre cachée : revue CodeRabbit PR #12).
        window.setCursorPosition({20.0, 30.0});
        glm::dvec2 applied{};
        glfwGetCursorPos(handle, &applied.x, &applied.y);
        CHECK(window.getCursorPosition() == applied);
        CHECK(platform::cursorPositionOf(window.getWindow()) == applied);
    }

    TEST_CASE("PLAT-01 a moved Window keeps receiving the callbacks of its native window")
    {
        platform::Window first("platform-test", 96, 64, platform::WindowState::kWindowed);
        if (!open(first))
        {
            return;
        }
        platform::Window moved(std::move(first));
        auto *handle = static_cast<GLFWwindow *>(moved.getWindow());
        const GLFWcursorposfun callback = glfwSetCursorPosCallback(handle, nullptr);
        REQUIRE(callback != nullptr);
        callback(handle, 3.0, 4.0);
        glfwSetCursorPosCallback(handle, callback);
        CHECK(moved.getCursorPosition() == glm::dvec2(3.0, 4.0));
        CHECK(platform::cursorPositionOf(moved.getWindow()) == glm::dvec2(3.0, 4.0));
    }
}
