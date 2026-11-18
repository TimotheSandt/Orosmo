// M1 bis : la fenêtre (couche platform) ne connaît plus le RHI. Elle passe par une IWindowGraphicsBinding que le runtime
// graphique enregistre quand on lui lie un backend. Ces tests n'ouvrent aucune fenêtre.
#include <doctest/doctest.h>

#include "Graphics/Backends/OpenGL/OpenGLGraphicsBackend.h"
#include "Graphics/Core/GraphicsRuntime.h"
#include "Graphics/Presentation/WindowGraphicsBinding.h"

#include <memory>

namespace
{
struct RecordingBinding final : IWindowGraphicsBinding
{
    mutable int calls = 0;
    bool IsRuntimeSupported() const noexcept override { return true; }
    bool RequiresCurrentContext() const noexcept override { return false; }
    bool Initialize(GLFWwindow *, bool, int, int) const override { ++calls; return true; }
    void EnsureContextReady(GLFWwindow *) const noexcept override { ++calls; }
    void ApplyDefaultFramebufferState(GLFWwindow *, bool, const glm::vec4 &) const noexcept override { ++calls; }
    void Present(GLFWwindow *) const noexcept override { ++calls; }
    void Shutdown(GLFWwindow *) const noexcept override { ++calls; }
    void SetViewport(int, int, int, int) const noexcept override { ++calls; }
    void CheckErrors(const char *) const noexcept override { ++calls; }
};

struct BindingGuard
{
    const IWindowGraphicsBinding *previous = GetWindowGraphicsBinding();
    ~BindingGuard() { SetWindowGraphicsBinding(previous); }
};
} // namespace

TEST_SUITE("WindowGraphicsBinding")
{
    TEST_CASE("the binding is a replaceable pointer owned by the platform layer")
    {
        BindingGuard guard;
        RecordingBinding fake;
        SetWindowGraphicsBinding(&fake);
        CHECK(GetWindowGraphicsBinding() == &fake);
        GetWindowGraphicsBinding()->Present(nullptr);
        GetWindowGraphicsBinding()->SetViewport(0, 0, 1, 1);
        CHECK(fake.calls == 2);
        SetWindowGraphicsBinding(nullptr);
        CHECK(GetWindowGraphicsBinding() == nullptr);
    }

    TEST_CASE("binding the graphics runtime installs the RHI binding, which follows the active API")
    {
        BindingGuard guard;
        SetWindowGraphicsBinding(nullptr);

        const OpenGLGraphicsBackend backend;
        const std::unique_ptr<IGraphicsDevice> device = backend.CreateDevice({});
        BindGraphicsRuntime({.api = GraphicsAPI::OpenGL, .backend = &backend, .device = device.get()});

        const IWindowGraphicsBinding *binding = GetWindowGraphicsBinding();
        REQUIRE(binding != nullptr);
        CHECK(binding->IsRuntimeSupported());
        CHECK(binding->RequiresCurrentContext()); // OpenGL : le contexte doit être courant

        ClearGraphicsRuntime();
        binding = GetWindowGraphicsBinding();
        REQUIRE(binding != nullptr); // reste installé (Window::Close doit encore pouvoir libérer le contexte)
        CHECK_FALSE(binding->IsRuntimeSupported());
        CHECK_FALSE(binding->RequiresCurrentContext());
        // Sans fenêtre, les opérations sont sans effet et ne plantent pas.
        binding->Present(nullptr);
        binding->EnsureContextReady(nullptr);
        binding->Shutdown(nullptr);
    }
}
