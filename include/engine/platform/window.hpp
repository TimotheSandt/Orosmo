#pragma once

#include <chrono>
#include <functional>
#include <cstdint>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "FPSCounter.h"

class InputManager;

// Mode du curseur (le GLFW sous-jacent reste caché dans src/platform/).
enum class CursorMode : std::uint8_t
{
    Normal,
    Hidden,
    Disabled
};

enum WindowState : std::uint8_t
{
    WINDOWED,
    BORDERLESS,
    FULLSCREEN,
    FULLSCREEN_UNFOCUSED
};

struct WindowParameters
{
    // Window
    std::string title;
    WindowState windowState;
    int width, height;
    int posX, posY;

    // Timing
    int maxFPS;
    bool vsync;

    // Color
    glm::vec4 clearColor;
    double trueEveryms;

    // Windowed
    int windowedWidth, windowedHeight;
    int windowedPosX, windowedPosY;

    bool taskbarVisible; // TODO
};

class Window
{
  public:
    Window();
    Window(std::string title, int width, int height);
    Window(std::string title, int width, int height, WindowState windowState);
    Window(WindowParameters parameters);

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    Window(Window &&) noexcept;
    Window &operator=(Window &&) noexcept;

    ~Window();

    // Crée la fenêtre. `beforeCreate` (facultatif) pose les attributs de contexte du graphique juste avant la création (RHI :
    // `rhi::prepareWindowHints`) : la fenêtre elle-même ne connaît aucune API graphique.
    int Init(const std::function<void()> &beforeCreate = {});
    bool NewFrame();
    void Close();
    // Termine GLFW (à la fin du programme, après la destruction de toutes les fenêtres et du périphérique graphique).
    static void Terminate();

    // Window state
    void ChangeWindowState(WindowState state);
    void ToggleFullscreen();
    void ToggleBorderless();

    // Setters
    void SetClearColor(glm::vec4 color) { this->parameters.clearColor = color; }
    void SetMaxFPS(int fps) { this->parameters.maxFPS = fps; }
    void SetTrueEvery(double ms) { this->parameters.trueEveryms = ms; }

    // Getters
    int GetWidth() const { return this->parameters.width; }
    int GetHeight() const { return this->parameters.height; }
    glm::vec4 GetClearColor() const { return this->parameters.clearColor; }
    int *GetWidthptr() { return &this->parameters.width; }
    int *GetHeightptr() { return &this->parameters.height; }
    bool ShouldClose() const;
    // Demande la fermeture (la boucle principale sort au prochain `ShouldClose`).
    void RequestClose();
    double GetAspectRatio() const { return (double)this->parameters.width / (double)this->parameters.height; }
    // Poignée native opaque, à passer telle quelle à `rhi::SurfaceDesc::nativeWindow` et à `InputManager::GetInstance`.
    void *GetWindow() const { return this->window; }
    // Taille réelle en pixels du tampon d'image (≠ taille de la fenêtre sur les écrans à échelle).
    void GetFramebufferSize(int &width, int &height) const;
    // Facteur d'échelle de contenu (DPI) : 1 = 96 dpi ; 2 sur un écran « Retina ». (1, 1) sans fenêtre.
    glm::vec2 GetContentScale() const;
    // Curseur (coordonnées en pixels de la fenêtre, origine en haut à gauche).
    void SetCursorMode(CursorMode mode);
    glm::dvec2 GetCursorPosition() const;
    void SetCursorPosition(glm::dvec2 position);

    // Performance
    int GetFrame() const { return this->fpsCounter.getFrame(); }
    double GetFPS() const { return this->fpsCounter.getFPS(); }
    double GetAverageFPS() const { return this->fpsCounter.getAverageFPS(); }
    double GetMaxFPS() const { return this->fpsCounter.getMaxFPS(); }
    double GetMinFPS() const { return this->fpsCounter.getMinFPS(); }

    double GetElapseTimeSecond() const { return this->fpsCounter.getElapseTimeInSeconds(); }
    double GetAverageElapseTimeSecond() const { return this->fpsCounter.getAverageElapseTimeInSeconds(); }
    double GetMaxElapseTimeSecond() const { return this->fpsCounter.getMaxElapseTimeInSeconds(); }
    double GetMinElapseTimeSecond() const { return this->fpsCounter.getMinElapseTimeInSeconds(); }

    double GetElapseTimeMillisecond() const { return this->fpsCounter.getElapseTimeInMilliseconds(); }
    double GetAverageElapseTimeMillisecond() const { return this->fpsCounter.getAverageElapseTimeInMilliseconds(); }
    double GetMaxElapseTimeMillisecond() const { return this->fpsCounter.getMaxElapseTimeInMilliseconds(); }
    double GetMinElapseTimeMillisecond() const { return this->fpsCounter.getMinElapseTimeInMilliseconds(); }

  private:
    void Swap(Window &other) noexcept;

    // Window state
    void SaveWindowedParameters();
    void PostWindowStateChange() const;

    void ActivateFullscreen();
    void ActivateWindowed();
    void ActivateBorderless();

    // Callbacks
    void SetupCallbacks();
    void CallbackFocus(int focused);
    void CallbackResize(int width, int height);
    void CallbackPosition(int x, int y);

    void ClearCallbacks();
    bool IsWindowHealthy() const;

  private:
    void *window = nullptr; // GLFWwindow*, opaque hors de src/platform/

    WindowParameters parameters;

    InputManager *inputManager;

    // Binding utilisé à Init() : Close() libère le contexte avec le même, même si le binding global a changé entretemps.

    FPSCounter fpsCounter;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastTime;
};
