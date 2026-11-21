#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace orosmo::core
{

// Minuterie GPU vue par le Profiler (couche core). Le Profiler ne connaît pas le RHI : le runtime graphique (couche rhi)
// fournit une IGpuTimerFactory quand on lui lie un backend (voir GraphicsRuntime.cpp). Sans fournisseur, ou si la
// fabrique ne renvoie aucun minuteur (pas de périphérique), le Profiler mesure avec l'horloge CPU.
class IGpuTimer
{
  public:
    virtual ~IGpuTimer() = default;

    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual bool IsReady() const = 0;
    virtual std::chrono::nanoseconds GetElapsedTime() const = 0;
};

class IGpuTimerFactory
{
  public:
    virtual ~IGpuTimerFactory() = default;

    // nullptr si aucun minuteur n'est disponible maintenant (par exemple pas de périphérique actif).
    virtual std::unique_ptr<IGpuTimer> CreateTimer(const std::string &name) const = 0;
};

} // namespace orosmo::core
