#include "Graphics/Presentation/WindowGraphicsBinding.h"

#include <atomic>

namespace
{
std::atomic<const IWindowGraphicsBinding *> activeBinding{nullptr};
}

void SetWindowGraphicsBinding(const IWindowGraphicsBinding *binding) noexcept { activeBinding.store(binding); }

const IWindowGraphicsBinding *GetWindowGraphicsBinding() noexcept { return activeBinding.load(); }
