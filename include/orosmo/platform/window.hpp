#pragma once

#include <chrono>
#include <functional>
#include <cstdint>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "orosmo/core/fps_counter.hpp"

namespace orosmo::platform
{

class InputManager;

// Mode du curseur (le GLFW sous-jacent reste caché dans src/platform/).
enum class CursorMode : std::uint8_t
{
    kNormal,
    kHidden,
    kDisabled
};

enum class WindowState : std::uint8_t
{
    kWindowed,
    kBorderless,
    kFullscreen,
    kFullscreenUnfocused
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
    int init(const std::function<void()> &beforeCreate = {});
    bool newFrame();
    void close();
    // Termine GLFW (à la fin du programme, après la destruction de toutes les fenêtres et du périphérique graphique).
    static void terminate();

    // Window state
    void changeWindowState(WindowState state);
    void toggleFullscreen();
    void toggleBorderless();

    // Setters
    void setClearColor(glm::vec4 color) { this->parameters.clearColor = color; }
    void setMaxFPS(int fps) { this->parameters.maxFPS = fps; }
    void setTrueEvery(double ms) { this->parameters.trueEveryms = ms; }

    // Getters
    int getWidth() const { return this->parameters.width; }
    int getHeight() const { return this->parameters.height; }
    glm::vec4 getClearColor() const { return this->parameters.clearColor; }
    int *getWidthPtr() { return &this->parameters.width; }
    int *getHeightPtr() { return &this->parameters.height; }
    bool shouldClose() const;
    // Demande la fermeture (la boucle principale sort au prochain `shouldClose`).
    void requestClose();
    double getAspectRatio() const { return (double)this->parameters.width / (double)this->parameters.height; }
    // Poignée native opaque, à passer telle quelle à `rhi::SurfaceDesc::nativeWindow` et à `InputManager::getInstance`.
    void *getWindow() const { return this->window; }
    // Taille réelle en pixels du tampon d'image (≠ taille de la fenêtre sur les écrans à échelle). Gardée par le rappel de
    // GLFW : la lire ne coûte pas d'aller-retour avec le serveur d'affichage (X11 : XGetWindowAttributes à chaque appel).
    void getFramebufferSize(int &width, int &height) const;
    // Facteur d'échelle de contenu (DPI) : 1 = 96 dpi ; 2 sur un écran « Retina ». (1, 1) sans fenêtre.
    glm::vec2 getContentScale() const;
    // Curseur (coordonnées en pixels de la fenêtre, origine en haut à gauche). La position est gardée par le rappel de GLFW
    // (sans aller-retour X11 : XQueryPointer) ; hors de la fenêtre, elle reste la dernière connue tant qu'aucun bouton n'est tenu.
    void setCursorMode(CursorMode mode);
    glm::dvec2 getCursorPosition() const;
    void setCursorPosition(glm::dvec2 position);
    // Presse-papiers du système (texte UTF-8) ; vide sans fenêtre.
    std::string getClipboardText() const;
    void setClipboardText(const std::string &text) const;

    // Performance
    int getFrame() const { return this->fpsCounter.getFrame(); }
    double getFPS() const { return this->fpsCounter.getFPS(); }
    double getAverageFPS() const { return this->fpsCounter.getAverageFPS(); }
    double getMaxFPS() const { return this->fpsCounter.getMaxFPS(); }
    double getMinFPS() const { return this->fpsCounter.getMinFPS(); }

    double getElapseTimeSecond() const { return this->fpsCounter.getElapseTimeInSeconds(); }
    double getAverageElapseTimeSecond() const { return this->fpsCounter.getAverageElapseTimeInSeconds(); }
    double getMaxElapseTimeSecond() const { return this->fpsCounter.getMaxElapseTimeInSeconds(); }
    double getMinElapseTimeSecond() const { return this->fpsCounter.getMinElapseTimeInSeconds(); }

    double getElapseTimeMillisecond() const { return this->fpsCounter.getElapseTimeInMilliseconds(); }
    double getAverageElapseTimeMillisecond() const { return this->fpsCounter.getAverageElapseTimeInMilliseconds(); }
    double getMaxElapseTimeMillisecond() const { return this->fpsCounter.getMaxElapseTimeInMilliseconds(); }
    double getMinElapseTimeMillisecond() const { return this->fpsCounter.getMinElapseTimeInMilliseconds(); }

  private:
    void swap(Window &other) noexcept;

    // Window state
    void saveWindowedParameters();
    void postWindowStateChange() const;

    void activateFullscreen();
    void activateWindowed();
    void activateBorderless();

    // Callbacks
    void setupCallbacks();
    void callbackFocus(int focused);
    void callbackResize(int width, int height);
    void callbackPosition(int x, int y);
    void refreshCachedState(); // relit la taille du tampon d'image et le curseur (création, changement de mode du curseur)

    void clearCallbacks();
    bool isWindowHealthy() const;

  private:
    void *window = nullptr; // GLFWwindow*, opaque hors de src/platform/

    WindowParameters parameters;

    // Valeurs tenues à jour par les rappels de GLFW (voir getFramebufferSize, getCursorPosition).
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glm::dvec2 cursorPosition{0.0, 0.0};
    int cursorModeValue = -1; // dernier mode posé par setCursorMode (-1 : aucun)

    InputManager *inputManager;

    // Binding utilisé à init() : close() libère le contexte avec le même, même si le binding global a changé entretemps.

    orosmo::core::FPSCounter fpsCounter;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastTime;
};

// Taille du tampon d'image et position du curseur d'une fenêtre native (`Window::getWindow()`), lues dans la Window qui la
// possède (valeurs gardées par les rappels de GLFW, sans aller-retour avec le serveur d'affichage) ; relues chez GLFW pour une
// fenêtre native qui n'appartient à aucune Window. Utilisées chaque image par le RHI et InputManager.
void framebufferSizeOf(void *nativeWindow, int &width, int &height);
glm::dvec2 cursorPositionOf(void *nativeWindow);

} // namespace orosmo::platform
