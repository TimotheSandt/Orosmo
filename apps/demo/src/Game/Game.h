#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "World.h"
#include "Camera.h"
#include "Window.h"
#include "Renderer3D.h"
#include "Profiler.h"
#include "UIManager.h"
#include "InputManager.h"

class Game
{
  public:
    Game();
    ~Game();

    void init();
    void stop();

    void run();

    // TOOL-03: after `frames` rendered frames, save <directory>/scene.png (3D) and ui.png, then close.
    void enableCapture(std::string directory, std::uint32_t frames);
    void setSeed(std::uint32_t seed) { terrainSeed = seed; }
    bool captureSucceeded() const noexcept { return captureDirectory.empty() || captureDone; }

  private:
    void processInput();
    void update();
    void render();
    void refreshViewSize();

  private:
    Window window;
    Renderer3D renderer3D;
    Renderer2D rendererUI;

    Camera camera;
    std::unique_ptr<World> world = nullptr;
    bool stopped = false;

    std::optional<std::uint32_t> terrainSeed;
    int viewWidth = 0;
    int viewHeight = 0;
    std::string captureDirectory;
    std::uint32_t captureFrames = 0;
    std::uint32_t renderedFrames = 0;
    bool captureDone = false;
};
