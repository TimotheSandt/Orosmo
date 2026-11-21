#include <orosmo/platform/window.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <orosmo/platform/input_manager.hpp>
#include <orosmo/core/profiler.hpp>
#include <orosmo/core/log.hpp>
#include <orosmo/core/paths.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

// Fenêtre : ne connaît aucune API graphique. Le graphique s'y accroche de l'extérieur : l'appelant fournit un rappel qui pose les
// attributs de contexte avant la création (`init`), puis le RHI adopte la fenêtre (`SurfaceDesc::nativeWindow`) et la présente.
#ifdef DEBUG
#define WINDOW_CHECK_ERRORS(context)                                                                                                       \
    do                                                                                                                                     \
    {                                                                                                                                      \
        const char *glfwErrorDescription = nullptr;                                                                                        \
        if (const int glfwErrorCode = glfwGetError(&glfwErrorDescription); glfwErrorCode != GLFW_NO_ERROR)                                \
            LOG_ERROR(glfwErrorCode, context, ": ", glfwErrorDescription != nullptr ? glfwErrorDescription : "");                         \
    } while (false)
#else
#define WINDOW_CHECK_ERRORS(context) ((void)0)
#endif

namespace orosmo::platform
{

namespace
{
GLFWwindow *native(void *handle) { return static_cast<GLFWwindow *>(handle); }
// Initialise GLFW une fois : rappel d'erreur journalisé (reprend celui de l'ancien backend OpenGL).
bool initializeGlfw()
{
    static const bool initialized = []()
    {
#ifdef _WIN32
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOOPENFILEERRORBOX);
#endif
        glfwSetErrorCallback([](int errorCode, const char *description) { LOG_ERROR(errorCode, "GLFW Error: ", description); });
        if (!glfwInit())
        {
            LOG_FATAL(-1, "Failed to initialize GLFW");
            return false;
        }
        return true;
    }();
    return initialized;
}
} // namespace

Window::Window()
{
    this->window = nullptr;
    this->inputManager = nullptr;
    this->parameters.title = "Window";
    this->parameters.width = 800;
    this->parameters.height = 600;
    this->parameters.posX = 100;
    this->parameters.posY = 100;
    this->parameters.maxFPS = 0;
    this->parameters.vsync = false;
#ifdef DEBUG
    this->parameters.windowState = WindowState::kWindowed;
#else
    this->parameters.windowState = WindowState::kFullscreen;
#endif // DEBUG
    this->parameters.clearColor = glm::vec4(0.07f, 0.13f, 0.17f, 1.0f);
    this->parameters.trueEveryms = 500;
    this->lastTime = this->fpsCounter.getLastTime();

    // Windowed
    this->parameters.windowedWidth = this->parameters.width;
    this->parameters.windowedHeight = this->parameters.height;
    this->parameters.windowedPosX = this->parameters.posX;
    this->parameters.windowedPosY = this->parameters.posY;
}

Window::Window(std::string title, int width, int height) : Window()
{
    this->parameters.title = std::move(title);
    this->parameters.width = width;
    this->parameters.height = height;
    this->parameters.windowedWidth = width;
    this->parameters.windowedHeight = height;
}

Window::Window(std::string title, int width, int height, WindowState windowState) : Window(std::move(title), width, height)
{
    this->parameters.windowState = windowState;
}

Window::Window(WindowParameters parameters) : Window()
{
    this->parameters = std::move(parameters);
    this->parameters.windowedWidth = this->parameters.width;
    this->parameters.windowedHeight = this->parameters.height;
    this->parameters.windowedPosX = this->parameters.posX;
    this->parameters.windowedPosY = this->parameters.posY;
}

Window::~Window() { this->close(); }

Window::Window(Window &&other) noexcept { this->swap(other); }

Window &Window::operator=(Window &&other) noexcept
{
    if (this != &other)
    {
        this->close();
        this->swap(other);
    }
    return *this;
}

void Window::swap(Window &other) noexcept
{
    std::swap(this->window, other.window);
    std::swap(this->parameters, other.parameters);
    std::swap(this->inputManager, other.inputManager);
    std::swap(this->fpsCounter, other.fpsCounter);
    std::swap(this->lastTime, other.lastTime);
}

int Window::init(const std::function<void()> &beforeCreate)
{
    if (!initializeGlfw())
    {
        return -1;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    if (beforeCreate)
    {
        beforeCreate(); // attributs de contexte du graphique (RHI : prepareWindowHints)
    }

    this->window = glfwCreateWindow(this->parameters.width, this->parameters.height, this->parameters.title.c_str(), nullptr, nullptr);
    if (!this->window)
    {
        LOG_ERROR(1, "Failed to create GLFW window");
        return -1;
    }

    glfwGetWindowPos(native(this->window), &this->parameters.posX, &this->parameters.posY);
    WINDOW_CHECK_ERRORS("glfwGetWindowPos");

    glfwSetWindowUserPointer(native(this->window), this);
    WINDOW_CHECK_ERRORS("glfwSetWindowUserPointer");

    this->setupCallbacks();

    this->changeWindowState(this->parameters.windowState);
    this->inputManager = &InputManager::getInstance(this->window);

    return 0;
}

void Window::close()
{
    if (!this->window)
    {
        return;
    }

    this->clearCallbacks();

    InputManager::removeInstance(this->window);
    this->inputManager = nullptr;

    glfwDestroyWindow(native(this->window));
    this->window = nullptr;

#ifdef _WIN32
    ShowWindow(FindWindowA("Shell_TrayWnd", nullptr), SW_SHOW);
#endif
}

void Window::terminate() { glfwTerminate(); }

bool Window::shouldClose() const { return this->window == nullptr || glfwWindowShouldClose(native(this->window)) != 0; }

void Window::requestClose()
{
    if (this->window)
    {
        glfwSetWindowShouldClose(native(this->window), GLFW_TRUE);
    }
}

void Window::getFramebufferSize(int &width, int &height) const
{
    width = this->parameters.width;
    height = this->parameters.height;
    if (this->window)
    {
        glfwGetFramebufferSize(native(this->window), &width, &height);
    }
}

glm::vec2 Window::getContentScale() const
{
    float x = 1.0f;
    float y = 1.0f;
    if (this->window)
    {
        glfwGetWindowContentScale(native(this->window), &x, &y);
    }
    return {x, y};
}

void Window::setCursorMode(CursorMode mode)
{
    if (!this->window)
    {
        return;
    }
    const int value = mode == CursorMode::kHidden ? GLFW_CURSOR_HIDDEN : mode == CursorMode::kDisabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    glfwSetInputMode(native(this->window), GLFW_CURSOR, value);
}

std::string Window::getClipboardText() const
{
    if (!this->window)
        return {};
    const char *text = glfwGetClipboardString(native(this->window));
    return text != nullptr ? std::string(text) : std::string();
}

void Window::setClipboardText(const std::string &text) const
{
    if (this->window)
        glfwSetClipboardString(native(this->window), text.c_str());
}

glm::dvec2 Window::getCursorPosition() const
{
    glm::dvec2 position{0.0, 0.0};
    if (this->window)
    {
        glfwGetCursorPos(native(this->window), &position.x, &position.y);
    }
    return position;
}

void Window::setCursorPosition(glm::dvec2 position)
{
    if (this->window)
    {
        glfwSetCursorPos(native(this->window), position.x, position.y);
    }
}

bool Window::newFrame()
{
    orosmo::core::Profiler::Profile("PollEvents", &glfwPollEvents);
    orosmo::core::Profiler::Process();
    this->inputManager->update();

    if (!orosmo::core::Profiler::Profile("HealthCheck", &Window::isWindowHealthy, this))
    {
        LOG_WARNING("Window is not healthy");
        return false;
    }

    this->fpsCounter.newFrame(this->parameters.maxFPS);

    if (this->parameters.trueEveryms == 0)
    {
        this->fpsCounter.updateStat();
        return true;
    }

    auto now = this->fpsCounter.getTime();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->lastTime).count();
    if (elapsed >= static_cast<long long>(this->parameters.trueEveryms))
    {
        this->fpsCounter.updateStat();
        this->lastTime = now;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// Window state functions ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

void Window::changeWindowState(WindowState state)
{
    switch (state)
    {
        case WindowState::kFullscreen:
        case WindowState::kFullscreenUnfocused:
            this->activateFullscreen();
            break;
        case WindowState::kBorderless:
            this->activateBorderless();
            break;
        case WindowState::kWindowed:
        default:
            this->activateWindowed();
            break;
    }
}

void Window::toggleFullscreen()
{
    if (this->parameters.windowState == WindowState::kFullscreen || this->parameters.windowState == WindowState::kFullscreenUnfocused)
    {
        this->activateWindowed();
    }
    else
    {
        this->activateFullscreen();
    }
}

void Window::toggleBorderless()
{
    if (this->parameters.windowState == WindowState::kBorderless)
    {
        this->activateWindowed();
    }
    else
    {
        this->activateBorderless();
    }
}

void Window::activateFullscreen()
{
    if (this->parameters.windowState == WindowState::kFullscreen)
    {
        return;
    }

    if (!isWindowHealthy())
    {
        LOG_WARNING("Window not healthy, skipping fullscreen toggle");
        return;
    }

    if (this->parameters.windowState == WindowState::kWindowed)
    {
        this->saveWindowedParameters();
    }

    // Get primary monitor
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (!monitor)
    {
        LOG_ERROR(1, "Failed to get primary monitor");
        return;
    }

    // Get monitor's video mode
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (!mode)
    {
        LOG_ERROR(1, "Failed to get video mode");
        return;
    }

    // Switch to fullscreen
    glfwSetWindowMonitor(native(this->window), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    WINDOW_CHECK_ERRORS("glfwSetWindowMonitor");

    // update internal parameters
    this->parameters.width = mode->width;
    this->parameters.height = mode->height;
    this->parameters.windowState = WindowState::kFullscreen;

    postWindowStateChange();
    LOG_DEBUG("Switched to fullscreen: ", mode->width, "x", mode->height, " @ ", mode->refreshRate, "Hz");
}

void Window::activateWindowed()
{
    if (this->parameters.windowState == WindowState::kWindowed)
    {
        return;
    }

    if (!isWindowHealthy())
    {
        LOG_WARNING("Window not healthy, skipping windowed toggle");
        return;
    }

#ifdef _WIN32
    ShowWindow(FindWindowA("Shell_TrayWnd", nullptr), SW_SHOW);
#endif

    glfwSetWindowMonitor(native(this->window), nullptr, this->parameters.windowedPosX, this->parameters.windowedPosY,
                         this->parameters.windowedWidth, this->parameters.windowedHeight, GLFW_DONT_CARE);
    WINDOW_CHECK_ERRORS("glfwSetWindowMonitor");
    glfwSetWindowAttrib(native(this->window), GLFW_DECORATED, GLFW_TRUE);
    WINDOW_CHECK_ERRORS("glfwSetWindowAttrib");

    // update internal parameters
    this->parameters.width = this->parameters.windowedWidth;
    this->parameters.height = this->parameters.windowedHeight;
    this->parameters.windowState = WindowState::kWindowed;

    postWindowStateChange();
    LOG_DEBUG("Switched to windowed: ", this->parameters.windowedWidth, "x", this->parameters.windowedHeight);
}

void Window::activateBorderless()
{
    if (this->parameters.windowState == WindowState::kBorderless)
    {
        return;
    }
    if (!isWindowHealthy())
    {
        LOG_WARNING("Window not healthy, skipping borderless toggle");
        return;
    }

    if (this->parameters.windowState == WindowState::kWindowed)
    {
        saveWindowedParameters();
    }

    // Get primary monitor
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (!monitor)
    {
        LOG_ERROR(1, "Failed to get primary monitor");
        return;
    }

    // Get monitor's video mode
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (!mode)
    {
        LOG_ERROR(1, "Failed to get video mode");
        return;
    }

    int width = mode->width;
    int height = mode->height + 1;

    // On Windows, we need to hide the taskbar
#ifdef _WIN32
    ShowWindow(FindWindowA("Shell_TrayWnd", nullptr), SW_HIDE);
#endif

    glfwSetWindowAttrib(native(this->window), GLFW_DECORATED, GLFW_FALSE);
    WINDOW_CHECK_ERRORS("glfwSetWindowAttrib");
    glfwSetWindowMonitor(native(this->window), nullptr, 0, 0, width, height, GLFW_DONT_CARE);
    WINDOW_CHECK_ERRORS("glfwSetWindowMonitor");

    // update internal parameters
    this->parameters.width = width;
    this->parameters.height = height;
    this->parameters.windowState = WindowState::kBorderless;

    postWindowStateChange();
    LOG_DEBUG("Switched to borderless: ", width, "x", height);
}

void Window::saveWindowedParameters()
{
    if (!this->window)
    {
        return;
    }

    glfwGetWindowSize(native(this->window), &this->parameters.windowedWidth, &this->parameters.windowedHeight);
    WINDOW_CHECK_ERRORS("glfwGetWindowSize");
    glfwGetWindowPos(native(this->window), &this->parameters.windowedPosX, &this->parameters.windowedPosY);
    WINDOW_CHECK_ERRORS("glfwGetWindowPos");
}

void Window::postWindowStateChange() const
{
    // Rien à reconfigurer côté graphique : le RHI relit la taille du framebuffer au début de chaque image.
}

///////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////        CALLBACK        ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

void Window::setupCallbacks()
{
    // Callback pour gérer la perte de focus (peut aider avec la touche Windows)
    glfwSetWindowFocusCallback(native(this->window),
                               [](GLFWwindow *window, int focused)
                               {
                                   try
                                   {
                                       Window *windowObj = static_cast<Window *>(glfwGetWindowUserPointer(window));
                                       if (!windowObj)
                                       {
                                           return;
                                       }
                                       windowObj->callbackFocus(focused);
                                   }
                                   catch (...)
                                   {
                                       // Ignorer les erreurs dans ce callback
                                   }
                               });

    glfwSetWindowSizeCallback(native(this->window),
                              [](GLFWwindow *window, int width, int height)
                              {
                                  try
                                  {
                                      Window *windowObj = static_cast<Window *>(glfwGetWindowUserPointer(window));
                                      if (!windowObj)
                                      {
                                          return;
                                      }
                                      windowObj->callbackResize(width, height);
                                  }
                                  catch (...)
                                  {
                                      // Ignorer les erreurs dans ce callback
                                  }
                              });

    glfwSetWindowPosCallback(native(this->window),
                             [](GLFWwindow *window, int x, int y)
                             {
                                 try
                                 {
                                     Window *windowObj = static_cast<Window *>(glfwGetWindowUserPointer(window));
                                     if (!windowObj)
                                     {
                                         return;
                                     }
                                     windowObj->callbackPosition(x, y);
                                 }
                                 catch (...)
                                 {
                                     // Ignorer les erreurs dans ce callback
                                 }
                             });
}

void Window::callbackResize(int width, int height)
{
    this->parameters.width = width;
    this->parameters.height = height;
}

void Window::callbackPosition(int x, int y)
{
    this->parameters.posX = x;
    this->parameters.posY = y;
}

void Window::callbackFocus(int focused)
{
    (void)focused;

    LOG_DEBUGGING("Window focus changed");

#ifdef _WIN32
    if (this->parameters.windowState == WindowState::kBorderless)
    {
        ShowWindow(FindWindowA("Shell_TrayWnd", nullptr), SW_HIDE);
    }
    else
    {
        ShowWindow(FindWindowA("Shell_TrayWnd", nullptr), SW_SHOW);
    }
#endif
}

void Window::clearCallbacks()
{
    glfwSetWindowFocusCallback(native(this->window), nullptr);
    glfwSetWindowSizeCallback(native(this->window), nullptr);
    glfwSetWindowPosCallback(native(this->window), nullptr);
    glfwSetKeyCallback(native(this->window), nullptr);
    glfwSetFramebufferSizeCallback(native(this->window), nullptr);
    glfwSetWindowIconifyCallback(native(this->window), nullptr);
    glfwSetWindowMaximizeCallback(native(this->window), nullptr);
    glfwSetWindowCloseCallback(native(this->window), nullptr);
}

// Fonction pour vérifier l'état de la fenêtre
bool Window::isWindowHealthy() const
{
    if (!this->window)
    {
        return false;
    }

    return true;
}

} // namespace orosmo::platform
