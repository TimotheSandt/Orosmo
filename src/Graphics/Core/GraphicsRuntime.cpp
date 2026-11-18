#include "Graphics/Core/GraphicsRuntime.h"

#include "Graphics/Core/GraphicsResources.h"
#include "Graphics/Core/WindowContext.h"
#include "Graphics/Presentation/WindowGraphicsBinding.h"
#include "Profiler/Profiler.h"

#include <memory>

namespace
{

GraphicsRuntimeBindings runtimeBindings{};

// Adaptateur : minuterie du Profiler (core) au-dessus d'une requête d'horodatage du RHI.
class TimestampQueryTimer final : public IGpuTimer
{
  public:
    explicit TimestampQueryTimer(std::unique_ptr<IGPUTimestampQueryResource> query) : query(std::move(query)) {}

    void Begin() override { query->Begin(); }
    void End() override { query->End(); }
    bool IsReady() const override { return query->IsReady(); }
    std::chrono::nanoseconds GetElapsedTime() const override { return query->GetElapsedTime(); }

  private:
    std::unique_ptr<IGPUTimestampQueryResource> query;
};

// Fournisseur installé dans le Profiler : demande le périphérique actif à chaque appel, exactement comme le faisait
// Profiler::ProfileGPU avant M1 bis (sans périphérique ou sans requête : nullptr, donc mesure CPU).
class RuntimeGpuTimerFactory final : public IGpuTimerFactory
{
  public:
    std::unique_ptr<IGpuTimer> CreateTimer(const std::string &name) const override
    {
        const IGraphicsDevice *device = TryGetActiveGraphicsDevice();
        if (device == nullptr)
            return nullptr;
        std::unique_ptr<IGPUTimestampQueryResource> query = device->CreateTimestampQuery({.debugName = name});
        if (query == nullptr)
            return nullptr;
        return std::make_unique<TimestampQueryTimer>(std::move(query));
    }
};

const RuntimeGpuTimerFactory gpuTimerFactory;

} // namespace

void BindGraphicsRuntime(const GraphicsRuntimeBindings &bindings) noexcept
{
    runtimeBindings = bindings;
    // Le fournisseur reste installé après ClearGraphicsRuntime : il ne produit simplement plus de minuteur sans périphérique.
    Profiler::SetGpuTimerFactory(&gpuTimerFactory);
    // Idem pour la fenêtre : l'adaptateur reste installé (Window::Close doit pouvoir libérer le contexte après Clear).
    SetWindowGraphicsBinding(&GraphicsWindowContext::RuntimeBinding());
}

void ClearGraphicsRuntime() noexcept { runtimeBindings = {}; }

GraphicsAPI GetActiveGraphicsAPI() noexcept { return runtimeBindings.api; }

const IGraphicsBackend *TryGetActiveGraphicsBackend() noexcept { return runtimeBindings.backend; }

const IGraphicsDevice *TryGetActiveGraphicsDevice() noexcept { return runtimeBindings.device; }

bool IsGraphicsAPIActive(GraphicsAPI api) noexcept { return runtimeBindings.backend != nullptr && runtimeBindings.api == api; }
