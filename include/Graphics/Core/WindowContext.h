#pragma once

#include <glm/vec4.hpp>

#include "Graphics/Presentation/WindowGraphicsBinding.h"

struct GLFWwindow;

namespace GraphicsWindowContext
{

bool Initialize(GLFWwindow *window, bool enableVsync, int width, int height);
void EnsureContextReady(GLFWwindow *window) noexcept;
void ApplyDefaultFramebufferState(GLFWwindow *window, bool enableVsync, const glm::vec4 &clearColor) noexcept;
void Present(GLFWwindow *window) noexcept;
void Shutdown(GLFWwindow *window) noexcept;

// Implémentation de IWindowGraphicsBinding (couche platform) au-dessus des fonctions ci-dessus ; installée par
// BindGraphicsRuntime pour que Window n'inclue aucun en-tête du RHI.
const IWindowGraphicsBinding &RuntimeBinding() noexcept;

} // namespace GraphicsWindowContext
