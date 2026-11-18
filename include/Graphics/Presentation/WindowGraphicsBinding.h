#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/vec4.hpp>

// Ce que la fenêtre (couche platform) demande au graphique (couche rhi) : créer le contexte ou la surface, présenter,
// reconfigurer l'état du framebuffer par défaut. Définie ici, du côté platform, pour que Window n'inclue aucun en-tête du
// RHI ; le runtime graphique installe l'implémentation quand on lui lie un backend (voir GraphicsRuntime.cpp).
class IWindowGraphicsBinding
{
  public:
    virtual ~IWindowGraphicsBinding() = default;

    // Un backend pris en charge par les fenêtres (OpenGL ou Vulkan) est lié.
    virtual bool IsRuntimeSupported() const noexcept = 0;
    // Le contexte graphique doit être courant sur le fil appelant (OpenGL) : Window le vérifie à chaque image.
    virtual bool RequiresCurrentContext() const noexcept = 0;

    virtual bool Initialize(GLFWwindow *window, bool enableVsync, int width, int height) const = 0;
    virtual void EnsureContextReady(GLFWwindow *window) const noexcept = 0;
    virtual void ApplyDefaultFramebufferState(GLFWwindow *window, bool enableVsync, const glm::vec4 &clearColor) const noexcept = 0;
    virtual void Present(GLFWwindow *window) const noexcept = 0;
    virtual void Shutdown(GLFWwindow *window) const noexcept = 0;
    virtual void SetViewport(int x, int y, int width, int height) const noexcept = 0;
    // Journalise les erreurs de l'API graphique (no-op hors DEBUG chez l'appelant).
    virtual void CheckErrors(const char *context) const noexcept = 0;
};

// Installe (ou retire avec nullptr) l'implémentation. Le pointeur n'est pas possédé.
void SetWindowGraphicsBinding(const IWindowGraphicsBinding *binding) noexcept;
const IWindowGraphicsBinding *GetWindowGraphicsBinding() noexcept;
